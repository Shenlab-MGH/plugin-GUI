#!/usr/bin/env python3
"""Build a deterministic Open Ephys Agent Windows portable release bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import sys
import tempfile
import zipfile


PLATFORM = "windows-x64"
RUNTIME_DLLS = {"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"}
RELEASES = {
    "oe-agent-v1.0.2-r0.1.0": {
        "agent_version": "0.1.0",
        "baseline": "1.0.2",
        "official_commit": "c91afebcfb0678a667fb93f6312ed33c56ec640f",
        "official_tag": "v1.0.2",
    },
    "oe-agent-v1.1.0-r0.1.0": {
        "agent_version": "0.1.0",
        "baseline": "1.1.0",
        "official_commit": "c2ce076f5b2d4182222f9d0fd9bb9b97a60582e7",
        "official_tag": "v1.1.0",
    },
}
ACTION_PINS = {
    "checkout": "11d5960a326750d5838078e36cf38b85af677262",
    "download-artifact": "d3f86a106a0bac45b974a628896c90dbdf5c8093",
    "setup-msbuild": "30375c66a4eea26614e0d39710365f22f8b0af57",
    "upload-artifact": "ea165f8d65b6e75b540449e92b4886f43607fa02",
}
ALLOWED_BUILD_FILES = {"icon-small.png", "open-ephys.exe"}
IGNORED_LINKER_FILES = {"open-ephys.exp", "open-ephys.lib"}
ALLOWED_RUNTIME_TREES = {
    "configs": {".png", ".xml"},
    "plugins": {".dll"},
    "resources": {".dat", ".npy", ".oebin"},
    "shared": {".bit", ".dll"},
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


def parse_assignments(values: list[str], label: str) -> dict[str, str]:
    assignments: dict[str, str] = {}
    for value in values:
        name, separator, assigned = value.partition("=")
        if not separator or not name or not assigned or name in assignments:
            raise PackagingError(f"invalid or duplicate {label}: {value}")
        assignments[name] = assigned
    return assignments


def approved_build_files(build_directory: Path) -> list[Path]:
    approved: list[Path] = []
    seen_names: set[str] = set()
    for path in sorted(build_directory.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(build_directory)
        name = relative.as_posix()
        casefolded = name.casefold()
        if casefolded in seen_names:
            raise PackagingError(f"duplicate case-insensitive Release path: {name}")
        seen_names.add(casefolded)
        if path.is_symlink():
            raise PackagingError(f"symbolic links are not allowed in Release output: {name}")
        if name in IGNORED_LINKER_FILES:
            continue
        if name in ALLOWED_BUILD_FILES:
            approved.append(path)
            continue
        if (
            len(relative.parts) > 1
            and relative.parts[0] in ALLOWED_RUNTIME_TREES
            and path.suffix.lower() in ALLOWED_RUNTIME_TREES[relative.parts[0]]
        ):
            approved.append(path)
            continue
        raise PackagingError(f"unexpected Release build product: {name}")

    approved_names = {
        path.relative_to(build_directory).as_posix() for path in approved
    }
    missing = ALLOWED_BUILD_FILES - approved_names
    if missing:
        raise PackagingError(
            "Release build is missing required runtime files: "
            + ", ".join(sorted(missing))
        )
    return approved


def validate_inputs(
    repository_root: Path,
    build_directory: Path,
    repository: str,
    commit: str,
    release_tag: str,
    runtime_dlls: list[Path],
    runtime_versions: dict[str, str],
    actions: dict[str, str],
    toolchains: dict[str, str],
    invocation: str,
    runner_image: str,
    runner_architecture: str,
) -> tuple[dict[str, str], str, list[Path]]:
    release = RELEASES.get(release_tag)
    if release is None:
        raise PackagingError(f"unsupported release version: {release_tag}")

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
    if set(runtime_versions) != RUNTIME_DLLS:
        raise PackagingError("runtime DLL versions must cover the exact runtime DLL set")
    if actions != ACTION_PINS:
        raise PackagingError("Action identities do not match the reviewed release pins")
    if set(toolchains) != {"cmake", "msbuild"}:
        raise PackagingError("toolchains must identify exactly cmake and msbuild")
    if invocation not in {"tag", "workflow_dispatch"}:
        raise PackagingError(f"unsupported invocation: {invocation}")
    if not runner_image or not runner_architecture:
        raise PackagingError("runner image and architecture are required")
    return release, gui_version, approved_build_files(build_directory)


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
    runtime_versions = parse_assignments(
        args.runtime_dll_version, "runtime DLL version"
    )
    actions = parse_assignments(args.action, "Action identity")
    toolchains = parse_assignments(args.toolchain, "toolchain identity")
    release, gui_version, build_files = validate_inputs(
        repository_root,
        build_directory,
        args.repository,
        args.commit,
        args.release_tag,
        runtime_dlls,
        runtime_versions,
        actions,
        toolchains,
        args.invocation,
        args.runner_image,
        args.runner_architecture,
    )

    release_version = args.release_tag.removeprefix("oe-agent-")
    artifact_name = f"open-ephys-agent-{release_version}-{PLATFORM}.zip"
    provenance = {
        "agent_version": release["agent_version"],
        "artifact": artifact_name,
        "build_environment": {
            "actions": actions,
            "runner": {
                "architecture": args.runner_architecture,
                "image": args.runner_image,
            },
            "toolchains": {
                **toolchains,
                "python": platform.python_version(),
            },
        },
        "commit": args.commit.lower(),
        "gui_version": gui_version,
        "invocation": args.invocation,
        "official_baseline": release["baseline"],
        "official_commit": release["official_commit"],
        "official_tag": release["official_tag"],
        "platform": PLATFORM,
        "release_eligible": args.invocation == "tag",
        "release_tag": args.release_tag,
        "repository": args.repository,
        "runtime_dlls": [
            {
                "file_version": runtime_versions[runtime.name],
                "name": runtime.name,
                "sha256": hashlib.sha256(runtime.read_bytes()).hexdigest(),
            }
            for runtime in sorted(runtime_dlls, key=lambda path: path.name)
        ],
        "validation_level": {"hardware": False, "scientific": False},
    }
    encoded_provenance = provenance_bytes(provenance)

    entries = {
        f"open-ephys/{path.relative_to(build_directory).as_posix()}": path.read_bytes()
        for path in build_files
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
    parser.add_argument("--release-tag", required=True)
    parser.add_argument(
        "--invocation", choices=("tag", "workflow_dispatch"), required=True
    )
    parser.add_argument("--runner-image", required=True)
    parser.add_argument("--runner-architecture", required=True)
    parser.add_argument("--runtime-dll", action="append", type=Path, required=True)
    parser.add_argument("--runtime-dll-version", action="append", required=True)
    parser.add_argument("--action", action="append", required=True)
    parser.add_argument("--toolchain", action="append", required=True)
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
