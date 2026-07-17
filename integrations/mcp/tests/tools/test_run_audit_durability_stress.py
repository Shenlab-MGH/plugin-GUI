from __future__ import annotations

import base64
import importlib.util
import subprocess
from pathlib import Path

import pytest


SCRIPT = Path(__file__).parents[2] / "tools" / "run_audit_durability_stress.py"
SPEC = importlib.util.spec_from_file_location("audit_durability_stress", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
stress = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stress)


def test_prepare_root_creates_and_requires_owned_sentinel(tmp_path: Path) -> None:
    repo = tmp_path / "repo"
    root = repo / "Build-runtime" / "audit-durability-stress-test"

    stress.prepare_stress_root(root, repo_root=repo)
    sentinel = root / stress.ROOT_SENTINEL_NAME
    assert sentinel.read_text(encoding="utf-8") == stress.ROOT_SENTINEL_CONTENT

    (root / "old-runtime-file").write_text("replace me", encoding="utf-8")
    stress.prepare_stress_root(root, repo_root=repo)
    assert sentinel.read_text(encoding="utf-8") == stress.ROOT_SENTINEL_CONTENT
    assert not (root / "old-runtime-file").exists()


@pytest.mark.parametrize("case", ["repo", "home", "deep", "unprefixed"])
def test_prepare_root_rejects_dangerous_paths(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, case: str
) -> None:
    repo = tmp_path / "repo"
    home = tmp_path / "home"
    monkeypatch.setattr(stress.Path, "home", classmethod(lambda cls: home))
    candidates = {
        "repo": repo,
        "home": home,
        "deep": repo
        / "Build-runtime"
        / "audit-durability-stress-parent"
        / "nested",
        "unprefixed": repo / "Build-runtime" / "other",
    }

    with pytest.raises(ValueError, match="unsafe stress root"):
        stress.prepare_stress_root(candidates[case], repo_root=repo)


def test_prepare_root_never_deletes_existing_directory_without_sentinel(
    tmp_path: Path,
) -> None:
    repo = tmp_path / "repo"
    root = repo / "Build-runtime" / "audit-durability-stress-existing"
    root.mkdir(parents=True)
    protected = root / "must-survive"
    protected.write_text("important", encoding="utf-8")

    with pytest.raises(ValueError, match="sentinel"):
        stress.prepare_stress_root(root, repo_root=repo)

    assert protected.read_text(encoding="utf-8") == "important"


def test_prepare_root_accepts_direct_child_of_explicit_safe_parent(
    tmp_path: Path,
) -> None:
    repo = tmp_path / "repo"
    safe_parent = tmp_path / "approved-runtime"
    safe_parent.mkdir()
    root = safe_parent / "audit-durability-stress-explicit"

    stress.prepare_stress_root(root, repo_root=repo, safe_parent=safe_parent)

    assert (root / stress.ROOT_SENTINEL_NAME).is_file()


@pytest.mark.parametrize("encoding", ["raw", "hex", "base64"])
def test_secret_scan_detects_raw_hex_and_base64(
    tmp_path: Path, encoding: str
) -> None:
    key = bytes(range(32))
    encoded = {
        "raw": key,
        "hex": key.hex().encode("ascii"),
        "base64": base64.b64encode(key),
    }[encoding]
    leaked = tmp_path / f"leaked-{encoding}.bin"
    leaked.write_bytes(b"prefix:" + encoded + b":suffix")

    with pytest.raises(AssertionError, match=encoding):
        stress.assert_secret_not_persisted(key, paths=[tmp_path])


def test_secret_scan_covers_sqlite_sidecars_artifacts_and_evidence(
    tmp_path: Path,
) -> None:
    key = b"k" * 32
    root = tmp_path / "runtime"
    artifacts = root / "artifacts"
    artifacts.mkdir(parents=True)
    files = [
        root / "audit.db",
        root / "audit.db-wal",
        root / "audit.db-shm",
        root / "audit.seal.json",
        artifacts / "artifact.bin",
        tmp_path / "evidence.json",
    ]
    for path in files:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"clean")

    stress.assert_secret_not_persisted(key, paths=[root, files[-1]])
    files[-1].write_text(key.hex(), encoding="ascii")
    with pytest.raises(AssertionError, match="hex"):
        stress.assert_secret_not_persisted(key, paths=[root, files[-1]])


def test_provenance_includes_source_and_runtime_versions() -> None:
    provenance = stress.collect_provenance()

    assert len(provenance["source_commit"]) == 40
    assert isinstance(provenance["source_dirty"], bool)
    assert len(provenance["script_sha256"]) == 64
    assert provenance["python_version"]
    assert provenance["sqlite_version"]
    assert provenance["os_version"]


def test_provenance_treats_ignored_build_runtime_output_as_clean(
    tmp_path: Path,
) -> None:
    repo = tmp_path / "repo"
    repo.mkdir()
    subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
    subprocess.run(
        ["git", "config", "user.email", "stress-test@example.invalid"],
        cwd=repo,
        check=True,
    )
    subprocess.run(
        ["git", "config", "user.name", "Stress Test"], cwd=repo, check=True
    )
    script = repo / "script.py"
    script.write_text("print('test')\n", encoding="utf-8")
    (repo / ".gitignore").write_text("Build-runtime/\n", encoding="utf-8")
    subprocess.run(["git", "add", "."], cwd=repo, check=True)
    subprocess.run(["git", "commit", "-qm", "fixture"], cwd=repo, check=True)
    ignored = repo / "Build-runtime" / "audit-durability-stress-test"
    ignored.mkdir(parents=True)
    (ignored / "audit.db").write_bytes(b"runtime")

    provenance = stress.collect_provenance(repo_root=repo, script_path=script)

    assert provenance["source_dirty"] is False
