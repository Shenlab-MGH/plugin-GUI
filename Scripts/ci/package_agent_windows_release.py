#!/usr/bin/env python3
"""Build a deterministic Open Ephys Agent Windows portable release bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile
import zipfile


PLATFORM = "windows-x64"
RUNTIME_DLLS = {"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"}
RELEASES = {
    "v1.0.2-r0.1.0": {
        "agent_version": "0.1.0",
        "baseline": "1.0.2",
        "official_commit": "c91afebcfb0678a667fb93f6312ed33c56ec640f",
        "official_tag": "v1.0.2",
    },
    "v1.1.0-r0.1.0": {
        "agent_version": "0.1.0",
        "baseline": "1.1.0",
        "official_commit": "c2ce076f5b2d4182222f9d0fd9bb9b97a60582e7",
        "official_tag": "v1.1.0",
    },
}
GUI_VERSION = re.compile(
    r"^\s*set\s*\(\s*GUI_VERSION\s+([^\s\)]+)\s*\)", re.MULTILINE
)
COMMIT_SHA = re.compile(r"[0-9a-fA-F]{40}")
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


class PackagingError(ValueError):
    """Raised when release inputs do not satisfy the packaging contract."""


def read_gui_version(repository_root: Path) -> str:
    cmake_file = repository_root / "CMakeLists.txt"
    try:
        source = cmake_file.read_text(encoding="utf-8")
    except OSError as error:
        raise PackagingError(f"cannot read {cmake_file}: {error}") from error
    match = GUI_VERSION.search(source)
    if match is None:
        raise PackagingError(f"GUI_VERSION is missing from {cmake_file}")
    return match.group(1)


def validate_inputs(
    repository_root: Path,
    build_directory: Path,
    repository: str,
    commit: str,
    release_version: str,
    runtime_dlls: list[Path],
) -> tuple[dict[str, str], str]:
    release = RELEASES.get(release_version)
    if release is None:
        raise PackagingError(f"unsupported release version: {release_version}")

    gui_version = read_gui_version(repository_root)
    if release["baseline"] != gui_version:
        raise PackagingError(
            f"release baseline {release['baseline']} does not match GUI_VERSION {gui_version}"
        )
    if not re.fullmatch(r"[^/\s]+/[^/\s]+", repository):
        raise PackagingError(f"invalid GitHub repository: {repository}")
    if COMMIT_SHA.fullmatch(commit) is None:
        raise PackagingError("commit must be a full 40-character Git SHA")
    if not build_directory.is_dir():
        raise PackagingError(f"Release build directory does not exist: {build_directory}")
    if not (repository_root / "LICENSE").is_file():
        raise PackagingError(f"LICENSE does not exist in {repository_root}")

    runtime_by_name = {path.name: path for path in runtime_dlls}
    if set(runtime_by_name) != RUNTIME_DLLS or len(runtime_dlls) != len(RUNTIME_DLLS):
        raise PackagingError(
            "runtime DLLs must be exactly: " + ", ".join(sorted(RUNTIME_DLLS))
        )
    for path in runtime_by_name.values():
        if not path.is_file():
            raise PackagingError(f"runtime DLL does not exist: {path}")
    return release, gui_version


def provenance_bytes(provenance: dict[str, object]) -> bytes:
    return (json.dumps(provenance, indent=2, sort_keys=True) + "\n").encode("utf-8")


def write_zip(artifact: Path, entries: dict[str, bytes]) -> None:
    with tempfile.NamedTemporaryFile(
        dir=artifact.parent, prefix=artifact.name, suffix=".tmp", delete=False
    ) as temporary:
        temporary_path = Path(temporary.name)
    try:
        with zipfile.ZipFile(
            temporary_path, mode="w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
        ) as archive:
            for name in sorted(entries):
                info = zipfile.ZipInfo(name, ZIP_TIMESTAMP)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                archive.writestr(info, entries[name])
        temporary_path.replace(artifact)
    finally:
        temporary_path.unlink(missing_ok=True)


def package_release(args: argparse.Namespace) -> tuple[Path, Path, Path]:
    repository_root = args.repository_root.resolve()
    build_directory = args.build_directory.resolve()
    runtime_dlls = [path.resolve() for path in args.runtime_dll]
    release, gui_version = validate_inputs(
        repository_root,
        build_directory,
        args.repository,
        args.commit,
        args.release_version,
        runtime_dlls,
    )

    artifact_name = f"open-ephys-agent-{args.release_version}-{PLATFORM}.zip"
    provenance = {
        "agent_version": release["agent_version"],
        "artifact": artifact_name,
        "commit": args.commit.lower(),
        "gui_version": gui_version,
        "official_baseline": release["baseline"],
        "official_commit": release["official_commit"],
        "official_tag": release["official_tag"],
        "platform": PLATFORM,
        "repository": args.repository,
        "validation_level": {"hardware": False, "scientific": False},
    }
    encoded_provenance = provenance_bytes(provenance)

    entries = {
        f"open-ephys/{path.relative_to(build_directory).as_posix()}": path.read_bytes()
        for path in build_directory.rglob("*")
        if path.is_file()
    }
    entries["open-ephys/LICENSE"] = (repository_root / "LICENSE").read_bytes()
    for runtime in runtime_dlls:
        entries[f"open-ephys/{runtime.name}"] = runtime.read_bytes()
    entries["open-ephys/provenance.json"] = encoded_provenance

    output_directory = args.output_directory.resolve()
    output_directory.mkdir(parents=True, exist_ok=True)
    artifact = output_directory / artifact_name
    checksum_file = artifact.with_suffix(".zip.sha256")
    provenance_file = artifact.with_suffix(".provenance.json")
    write_zip(artifact, entries)
    provenance_file.write_bytes(encoded_provenance)
    digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
    checksum_file.write_text(f"{digest}  {artifact.name}\n", encoding="ascii")
    return artifact, checksum_file, provenance_file


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository-root", type=Path, required=True)
    parser.add_argument("--build-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--release-version", required=True)
    parser.add_argument("--runtime-dll", action="append", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    try:
        artifact, checksum_file, provenance_file = package_release(parse_args())
    except (OSError, PackagingError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    print(
        json.dumps(
            {
                "artifact": str(artifact),
                "checksum": str(checksum_file),
                "provenance": str(provenance_file),
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
