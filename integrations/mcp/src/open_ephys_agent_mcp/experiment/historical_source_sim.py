"""Read-only validation of historical Source Sim delivery evidence."""

from __future__ import annotations

import ast
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import stat
import struct
from typing import Any, Iterable


PRESETS = (
    "All Shanks 1-96",
    "All Shanks 97-192",
    "All Shanks 193-288",
    "All Shanks 289-384",
    "All Shanks 385-480",
    "All Shanks 481-576",
    "All Shanks 577-672",
    "All Shanks 673-768",
)
MANIFEST_INVENTORY = frozenset(
    {
        "video/full_workflow_realtime.mp4",
        "video/full_workflow.mp4",
        "video/trace.jsonl",
        "data/run_report.json",
    }
)
MANIFEST_PATTERN = re.compile(r"^([0-9A-Fa-f]{64})  ([^\r\n]+)$")
SUFFIX_PATTERN = re.compile(r" \(\d+\)$")
SECRET_KEY_PATTERN = re.compile(
    r"(?:^|[_-])(token|password|secret|api[_-]?key|credential|private[_-]?key|bearer)(?:$|[_-])",
    re.IGNORECASE,
)
SECRET_VALUE_PATTERN = re.compile(
    r"(?:\bBearer\s+|\bsk-[A-Za-z0-9_-]{8,}|(?:password|token|api[_-]?key)\s*[:=])",
    re.IGNORECASE,
)
REPARSE_POINT = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)


class HistoricalEvidenceError(ValueError):
    """Fail-closed historical evidence validation error."""

    def __init__(self, code: str) -> None:
        self.code = code
        super().__init__(code)


def _fail(code: str) -> None:
    raise HistoricalEvidenceError(code)


def _is_reparse(path: Path) -> bool:
    try:
        status = path.lstat()
    except OSError:
        _fail("PATH_INSPECTION_FAILED")
    return stat.S_ISLNK(status.st_mode) or bool(
        getattr(status, "st_file_attributes", 0) & REPARSE_POINT
    )


def _safe_existing(root: Path, path: Path, *, file: bool | None = None) -> Path:
    try:
        relative = path.relative_to(root)
    except ValueError:
        _fail("PATH_OUTSIDE_EVIDENCE_ROOT")
    cursor = root
    for component in relative.parts:
        cursor = cursor / component
        if not cursor.exists() and not cursor.is_symlink():
            _fail("REQUIRED_FILE_MISSING")
        if _is_reparse(cursor):
            _fail("REPARSE_POINT_FORBIDDEN")
    try:
        cursor.resolve(strict=True).relative_to(root.resolve(strict=True))
    except (OSError, ValueError):
        _fail("PATH_OUTSIDE_EVIDENCE_ROOT")
    if file is True and not cursor.is_file():
        _fail("REQUIRED_FILE_MISSING")
    if file is False and not cursor.is_dir():
        _fail("REQUIRED_DIRECTORY_MISSING")
    return cursor


def _reported_path(root: Path, value: Any) -> Path:
    if not isinstance(value, str) or not value:
        _fail("REPORTED_PATH_INVALID")
    candidate = Path(value)
    if not candidate.is_absolute():
        candidate = root / candidate
    return _safe_existing(root, Path(os.path.abspath(candidate)))


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def _snapshot_tree(root: Path) -> dict[str, tuple[bool, int, int]]:
    snapshot: dict[str, tuple[bool, int, int]] = {}
    pending = [root]
    while pending:
        directory = pending.pop()
        try:
            entries = list(os.scandir(directory))
        except OSError:
            _fail("PATH_INSPECTION_FAILED")
        for entry in entries:
            path = Path(entry.path)
            try:
                status = entry.stat(follow_symlinks=False)
            except OSError:
                _fail("PATH_INSPECTION_FAILED")
            if stat.S_ISLNK(status.st_mode) or bool(
                getattr(status, "st_file_attributes", 0) & REPARSE_POINT
            ):
                _fail("REPARSE_POINT_FORBIDDEN")
            is_directory = stat.S_ISDIR(status.st_mode)
            key = path.relative_to(root).as_posix().casefold()
            if key in snapshot:
                _fail("CASE_COLLISION")
            snapshot[key] = (
                (True, 0, 0)
                if is_directory
                else (False, status.st_size, status.st_mtime_ns)
            )
            if is_directory:
                pending.append(path)
    return snapshot


def _check_report_secrets(value: Any) -> None:
    if isinstance(value, dict):
        for key, nested in value.items():
            if not isinstance(key, str) or SECRET_KEY_PATTERN.search(key):
                _fail("SECRET_LIKE_REPORT_CONTENT")
            _check_report_secrets(nested)
    elif isinstance(value, list):
        for nested in value:
            _check_report_secrets(nested)
    elif isinstance(value, str) and SECRET_VALUE_PATTERN.search(value):
        _fail("SECRET_LIKE_REPORT_CONTENT")


def _load_report(root: Path) -> dict[str, Any]:
    path = _safe_existing(root, root / "data" / "run_report.json", file=True)
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        _fail("RUN_REPORT_INVALID")
    if not isinstance(report, dict):
        _fail("RUN_REPORT_INVALID")
    _check_report_secrets(report)
    return report


def _parse_manifest(root: Path) -> list[dict[str, Any]]:
    manifest = _safe_existing(root, root / "SHA256SUMS.txt", file=True)
    try:
        lines = manifest.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError):
        _fail("HASH_MANIFEST_MALFORMED")
    entries: dict[str, str] = {}
    for line in lines:
        match = MANIFEST_PATTERN.fullmatch(line)
        if match is None:
            _fail("HASH_MANIFEST_MALFORMED")
        expected, raw_path = match.groups()
        pure = PurePosixPath(raw_path)
        windows = PureWindowsPath(raw_path)
        if (
            pure.is_absolute()
            or windows.is_absolute()
            or any(part in {"", ".", ".."} for part in pure.parts)
            or "\\" in raw_path
        ):
            _fail("HASH_MANIFEST_PATH_INVALID")
        normalized = pure.as_posix()
        key = normalized.casefold()
        if key in entries:
            _fail("HASH_MANIFEST_DUPLICATE")
        entries[key] = normalized
        path = _safe_existing(root, root.joinpath(*pure.parts), file=True)
        if _sha256(path) != expected.lower():
            _fail("HASH_MISMATCH")
    observed = set(entries.values())
    if observed != MANIFEST_INVENTORY:
        _fail("HASH_MANIFEST_INVENTORY_MISMATCH")
    return [
        {
            "path": relative,
            "bytes": (root / relative).stat().st_size,
            "sha256": _sha256(root / relative),
        }
        for relative in sorted(observed)
    ]


def _npy_values(path: Path, descriptor: str) -> tuple[int, Iterable[int | float]]:
    try:
        with path.open("rb") as handle:
            if handle.read(6) != b"\x93NUMPY":
                _fail("NPY_INVALID")
            major, _minor = handle.read(2)
            if major not in {1, 2, 3}:
                _fail("NPY_INVALID")
            length_size = 2 if major == 1 else 4
            header_length = struct.unpack(
                "<H" if length_size == 2 else "<I", handle.read(length_size)
            )[0]
            header = ast.literal_eval(handle.read(header_length).decode("latin1"))
            if (
                header.get("descr") != descriptor
                or header.get("fortran_order") is not False
                or not isinstance(header.get("shape"), tuple)
                or len(header["shape"]) != 1
                or type(header["shape"][0]) is not int
                or header["shape"][0] < 0
            ):
                _fail("NPY_INVALID")
            count = header["shape"][0]
            data = handle.read()
    except (OSError, SyntaxError, ValueError, struct.error, UnicodeError):
        _fail("NPY_INVALID")
    if len(data) != count * 8:
        _fail("NPY_COUNT_MISMATCH")
    code = "q" if descriptor == "<i8" else "d"
    return count, (item[0] for item in struct.iter_unpack(f"<{code}", data))


def _file_record(root: Path, path: Path) -> dict[str, Any]:
    return {
        "path": path.relative_to(root).as_posix(),
        "bytes": path.stat().st_size,
        "sha256": _sha256(path),
    }


def _validate_structure(
    path: Path, stream_directory: Path, record_node_id: int
) -> None:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        _fail("STRUCTURE_OEBIN_MISMATCH")
    continuous = payload.get("continuous") if isinstance(payload, dict) else None
    if not isinstance(continuous, list) or len(continuous) != 1:
        _fail("STRUCTURE_OEBIN_MISMATCH")
    stream = continuous[0]
    if (
        payload.get("GUI version") != "1.0.2"
        or not isinstance(stream, dict)
        or stream.get("folder_name") != f"{stream_directory.name}/"
        or stream.get("sample_rate") != 30000.0
        or stream.get("stream_name") != "ProbeA"
        or stream.get("recorded_processor") != "Record Node"
        or stream.get("recorded_processor_id") != record_node_id
        or stream.get("num_channels") != 384
        or not isinstance(stream.get("channels"), list)
        or len(stream["channels"]) != 384
    ):
        _fail("STRUCTURE_OEBIN_MISMATCH")


def _single_directory(paths: list[Path], code: str) -> Path:
    if len(paths) != 1:
        _fail(code)
    return paths[0]


def _validate_part(
    root: Path, part: Any, index: int, seen_directories: set[str]
) -> dict[str, Any]:
    if not isinstance(part, dict) or part.get("part") != index:
        _fail("PART_SEQUENCE_INVALID")
    expected_preset = PRESETS[index - 1]
    if part.get("preset") != expected_preset or part.get("preset_readback") != expected_preset:
        _fail("PRESET_SEQUENCE_INVALID")
    validation = part.get("validation")
    if not isinstance(validation, dict):
        _fail("STRUCTURAL_REPORT_INVALID")
    raw_part_root = part.get("recording_root")
    if isinstance(raw_part_root, str) and SUFFIX_PATTERN.search(Path(raw_part_root).name):
        _fail("SUFFIXED_NATIVE_DIRECTORY")
    part_root = _reported_path(root, raw_part_root)
    if validation.get("recording_root") != part.get("recording_root"):
        _fail("STRUCTURAL_REPORT_INVALID")
    directory_key = str(part_root).casefold()
    if directory_key in seen_directories:
        _fail("DUPLICATE_NATIVE_DIRECTORY")
    seen_directories.add(directory_key)
    if part_root.parent != root / "data" or part.get("name") != part_root.name:
        _fail("NATIVE_DIRECTORY_INVALID")

    settings = _reported_path(root, validation.get("settings_path"))
    if settings.name != "settings.xml" or settings.parent.parent != part_root:
        _fail("SETTINGS_PATH_INVALID")
    record_node_match = re.fullmatch(r"Record Node (\d+)", settings.parent.name)
    if record_node_match is None:
        _fail("SETTINGS_PATH_INVALID")
    if validation.get("recording_units") != 1:
        _fail("RECORDING_UNIT_AMBIGUOUS")
    experiments = [path for path in settings.parent.glob("experiment*") if path.is_dir()]
    experiment = _single_directory(experiments, "RECORDING_UNIT_AMBIGUOUS")
    _safe_existing(root, experiment, file=False)
    recordings = [path for path in experiment.glob("recording*") if path.is_dir()]
    unit = _single_directory(recordings, "RECORDING_UNIT_AMBIGUOUS")
    _safe_existing(root, unit, file=False)
    stream_directories = [path for path in (unit / "continuous").glob("*") if path.is_dir()]
    stream_directory = _single_directory(stream_directories, "STREAM_DIRECTORY_AMBIGUOUS")
    _safe_existing(root, stream_directory, file=False)

    structure = _safe_existing(root, unit / "structure.oebin", file=True)
    continuous = _safe_existing(root, stream_directory / "continuous.dat", file=True)
    sample_numbers = _safe_existing(root, stream_directory / "sample_numbers.npy", file=True)
    timestamps = _safe_existing(root, stream_directory / "timestamps.npy", file=True)
    required_files = (settings, structure, continuous, sample_numbers, timestamps)
    if any(path.stat().st_size <= 0 for path in required_files):
        _fail("REQUIRED_FILE_EMPTY")
    _validate_structure(structure, stream_directory, int(record_node_match.group(1)))

    streams = validation.get("streams")
    if not isinstance(streams, list) or len(streams) != 1 or not isinstance(streams[0], dict):
        _fail("STRUCTURAL_REPORT_INVALID")
    stream = streams[0]
    if (
        validation.get("ok") is not True
        or validation.get("errors") != []
        or validation.get("presets") != [expected_preset, "NONE"]
        or stream.get("name") != "ProbeA"
        or stream.get("channels") != 384
        or stream.get("sample_rate") != 30000.0
        or stream.get("nonunit_sample_steps") != 0
        or stream.get("invalid_timestamps") != 0
        or stream.get("errors") != []
    ):
        _fail("STRUCTURAL_REPORT_INVALID")
    if continuous.stat().st_size != stream.get("continuous_bytes"):
        _fail("CONTINUOUS_BYTES_MISMATCH")
    sample_count, sample_values = _npy_values(sample_numbers, "<i8")
    timestamp_count, timestamp_values = _npy_values(timestamps, "<f8")
    samples = list(sample_values)
    timestamps_data = list(timestamp_values)
    if sample_count != timestamp_count or continuous.stat().st_size != sample_count * 384 * 2:
        _fail("SAMPLE_COUNT_MISMATCH")
    if any(right - left != 1 for left, right in zip(samples, samples[1:])):
        _fail("STRUCTURAL_REPORT_INVALID")
    if any(not math.isfinite(value) for value in timestamps_data):
        _fail("STRUCTURAL_REPORT_INVALID")
    duration = stream.get("duration_seconds")
    if not isinstance(duration, (int, float)) or abs(duration - sample_count / 30000.0) > 1e-9:
        _fail("STRUCTURAL_REPORT_INVALID")
    return {
        "part": index,
        "preset": expected_preset,
        "native_directory": part_root.relative_to(root).as_posix(),
        "recording_unit": unit.relative_to(root).as_posix(),
        "continuous_bytes": continuous.stat().st_size,
        "sample_count": sample_count,
        "files": [_file_record(root, path) for path in required_files],
    }


def validate_historical_source_sim(evidence_root: str | Path) -> dict[str, Any]:
    """Validate a historical delivery without mutating it or qualifying this fork."""
    root = Path(evidence_root).absolute()
    if not root.is_dir() or _is_reparse(root):
        _fail("EVIDENCE_ROOT_INVALID")
    initial_snapshot = _snapshot_tree(root)
    report = _load_report(root)
    manifest = _parse_manifest(root)
    if _reported_path(root, report.get("output")) != root / "data":
        _fail("REPORT_OUTPUT_MISMATCH")
    identity = report.get("runtime_identity")
    if (
        report.get("ok") is not True
        or report.get("errors") != []
        or report.get("final_mode") != "IDLE"
        or not isinstance(identity, dict)
        or identity.get("is_simulator") is not True
        or identity.get("firmware") != "SIM 0.0"
        or identity.get("probe_part_number") != "NP2013"
    ):
        _fail("SIMULATOR_IDENTITY_INVALID")
    parts = report.get("parts")
    if not isinstance(parts, list) or len(parts) != 8:
        _fail("PART_COUNT_INVALID")
    seen_directories: set[str] = set()
    part_results = [
        _validate_part(root, part, index, seen_directories)
        for index, part in enumerate(parts, start=1)
    ]
    evidence = {"manifest": manifest, "parts": part_results}
    delivery_hash = hashlib.sha256(
        json.dumps(evidence, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()
    result = {
        "schema_version": "oe-agent-historical-source-sim/v0.0.1",
        "provenance": "historical_official_gui_simulation",
        "qualifies_current_fork": False,
        "delivery_sha256": delivery_hash,
        "simulator": {
            "firmware": "SIM 0.0",
            "probe_part_number": "NP2013",
            "channels": 384,
            "sample_rate": 30000,
        },
        "parts": part_results,
        "sha256_manifest": manifest,
    }
    if _snapshot_tree(root) != initial_snapshot:
        _fail("EVIDENCE_MUTATED_DURING_VALIDATION")
    return result
