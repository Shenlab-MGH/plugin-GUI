"""Read-only validation of historical Source Sim delivery evidence."""

from __future__ import annotations

import ast
import ctypes
from ctypes import wintypes
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import stat
import struct
from typing import Any, Iterable
import xml.etree.ElementTree as ET


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
SECRET_VALUE_PATTERN = re.compile(
    r"(?:\bBearer\s+|\bsk-[A-Za-z0-9_-]{8,}|\bgh[pousr]_[A-Za-z0-9]{20,}|"
    r"\bAKIA[A-Z0-9]{16}\b|\beyJ[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+|"
    r"-----BEGIN [A-Z ]*PRIVATE KEY-----|(?:password|passwd|token|api[_-]?key)\s*[:=])",
    re.IGNORECASE,
)
SECRET_KEY_WORDS = (
    "accesstoken", "authorization", "apikey", "bearer", "cookie",
    "credential", "password", "passwd", "privatekey", "secret", "token",
)
REPARSE_POINT = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
MAX_REPORT_BYTES = 4 * 1024 * 1024
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_SETTINGS_BYTES = 8 * 1024 * 1024
MAX_STRUCTURE_BYTES = 16 * 1024 * 1024
MAX_NPY_BYTES = 512 * 1024 * 1024
MAX_NPY_HEADER_BYTES = 64 * 1024
MAX_NPY_COUNT = 50_000_000


class _WindowsFileInfo(ctypes.Structure):
    _fields_ = [
        ("dwFileAttributes", wintypes.DWORD),
        ("ftCreationTime", wintypes.FILETIME),
        ("ftLastAccessTime", wintypes.FILETIME),
        ("ftLastWriteTime", wintypes.FILETIME),
        ("dwVolumeSerialNumber", wintypes.DWORD),
        ("nFileSizeHigh", wintypes.DWORD),
        ("nFileSizeLow", wintypes.DWORD),
        ("nNumberOfLinks", wintypes.DWORD),
        ("nFileIndexHigh", wintypes.DWORD),
        ("nFileIndexLow", wintypes.DWORD),
    ]


if os.name == "nt":
    _KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _KERNEL32.CreateFileW.argtypes = [
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    ]
    _KERNEL32.CreateFileW.restype = wintypes.HANDLE
    _KERNEL32.GetFileInformationByHandle.argtypes = [wintypes.HANDLE, wintypes.LPVOID]
    _KERNEL32.GetFileInformationByHandle.restype = wintypes.BOOL
    _KERNEL32.GetFinalPathNameByHandleW.argtypes = [
        wintypes.HANDLE, wintypes.LPWSTR, wintypes.DWORD, wintypes.DWORD,
    ]
    _KERNEL32.GetFinalPathNameByHandleW.restype = wintypes.DWORD
    _KERNEL32.SetFilePointerEx.argtypes = [
        wintypes.HANDLE, ctypes.c_longlong, ctypes.POINTER(ctypes.c_longlong), wintypes.DWORD,
    ]
    _KERNEL32.SetFilePointerEx.restype = wintypes.BOOL
    _KERNEL32.ReadFile.argtypes = [
        wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID,
    ]
    _KERNEL32.ReadFile.restype = wintypes.BOOL
    _KERNEL32.CloseHandle.argtypes = [wintypes.HANDLE]
    _KERNEL32.CloseHandle.restype = wintypes.BOOL
else:
    _KERNEL32 = None


class _BoundHandle:
    def __init__(self, path: Path, root: Path, directory: bool) -> None:
        self.path = path
        self.directory = directory
        self.handle: int | None = None
        self.fd: int | None = None
        if os.name == "nt":
            assert _KERNEL32 is not None
            flags = 0x00200000 | (0x02000000 if directory else 0)
            handle = _KERNEL32.CreateFileW(str(path), 0x80000000, 0x00000001, None, 3, flags, None)
            invalid = ctypes.c_void_p(-1).value
            if handle == invalid:
                raise OSError(ctypes.get_last_error(), "CreateFileW failed", str(path))
            self.handle = int(handle)
            info = _WindowsFileInfo()
            if not _KERNEL32.GetFileInformationByHandle(handle, ctypes.byref(info)):
                self.close()
                raise OSError(ctypes.get_last_error(), "GetFileInformationByHandle failed")
            if info.dwFileAttributes & REPARSE_POINT:
                self.close()
                _fail("REPARSE_POINT_FORBIDDEN")
            size = (info.nFileSizeHigh << 32) | info.nFileSizeLow
            self.size = size
            self.identity = (
                info.dwVolumeSerialNumber,
                (info.nFileIndexHigh << 32) | info.nFileIndexLow,
            )
            buffer = ctypes.create_unicode_buffer(32768)
            length = _KERNEL32.GetFinalPathNameByHandleW(handle, buffer, len(buffer), 0)
            if length == 0 or length >= len(buffer):
                self.close()
                raise OSError(ctypes.get_last_error(), "GetFinalPathNameByHandleW failed")
            final = buffer.value
            if final.startswith("\\\\?\\UNC\\"):
                final = "\\\\" + final[8:]
            elif final.startswith("\\\\?\\"):
                final = final[4:]
            if os.path.normcase(os.path.abspath(final)) != os.path.normcase(os.path.abspath(path)):
                self.close()
                _fail("REPARSE_POINT_FORBIDDEN")
        else:
            flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
            self.fd = os.open(path, flags)
            status = os.fstat(self.fd)
            self.size = status.st_size
            self.identity = (status.st_dev, status.st_ino)
        try:
            Path(path).absolute().relative_to(root)
        except ValueError:
            self.close()
            _fail("PATH_OUTSIDE_EVIDENCE_ROOT")

    def chunks(self, chunk_size: int = 1024 * 1024):
        if self.directory:
            _fail("REQUIRED_FILE_MISSING")
        if os.name == "nt":
            assert self.handle is not None
            assert _KERNEL32 is not None
            position = ctypes.c_longlong(0)
            if not _KERNEL32.SetFilePointerEx(self.handle, position, None, 0):
                raise OSError(ctypes.get_last_error(), "SetFilePointerEx failed")
            remaining = self.size
            while remaining:
                amount = min(chunk_size, remaining)
                buffer = ctypes.create_string_buffer(amount)
                read = wintypes.DWORD()
                if not _KERNEL32.ReadFile(self.handle, buffer, amount, ctypes.byref(read), None):
                    raise OSError(ctypes.get_last_error(), "ReadFile failed")
                if read.value == 0:
                    raise OSError("unexpected end of file")
                remaining -= read.value
                yield buffer.raw[: read.value]
        else:
            assert self.fd is not None
            os.lseek(self.fd, 0, os.SEEK_SET)
            while chunk := os.read(self.fd, chunk_size):
                yield chunk

    def read(self, maximum: int) -> bytes:
        if self.size > maximum:
            _fail("FILE_SIZE_LIMIT_EXCEEDED")
        return b"".join(self.chunks())

    def sha256(self) -> str:
        digest = hashlib.sha256()
        for chunk in self.chunks():
            digest.update(chunk)
        return digest.hexdigest()

    def close(self) -> None:
        if self.handle is not None:
            assert _KERNEL32 is not None
            _KERNEL32.CloseHandle(self.handle)
            self.handle = None
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None


class _EvidenceSession:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.handles: dict[str, _BoundHandle] = {}
        self.file_identities: dict[tuple[int, int], str] = {}

    def __enter__(self) -> "_EvidenceSession":
        pending = [self.root]
        try:
            self._bind(self.root, directory=True)
            while pending:
                directory = pending.pop()
                with os.scandir(directory) as iterator:
                    entries = list(iterator)
                for entry in entries:
                    path = Path(entry.path)
                    if entry.is_symlink():
                        _fail("REPARSE_POINT_FORBIDDEN")
                    is_directory = entry.is_dir(follow_symlinks=False)
                    if is_directory:
                        self._bind(path, directory=True)
                        pending.append(path)
                    else:
                        self._bind(path, directory=False)
            return self
        except Exception:
            self.close()
            raise

    def _key(self, path: Path) -> str:
        if path == self.root:
            return ""
        return path.relative_to(self.root).as_posix().casefold()

    def _bind(self, path: Path, directory: bool) -> None:
        key = self._key(path)
        if key in self.handles:
            _fail("CASE_COLLISION")
        handle = _BoundHandle(path, self.root, directory)
        if not directory and handle.identity in self.file_identities:
            handle.close()
            _fail("FILE_IDENTITY_COLLISION")
        if not directory:
            self.file_identities[handle.identity] = key
        self.handles[key] = handle

    def file(self, path: Path) -> _BoundHandle:
        try:
            handle = self.handles[self._key(path)]
        except (KeyError, ValueError):
            _fail("REQUIRED_FILE_MISSING")
        if handle.directory:
            _fail("REQUIRED_FILE_MISSING")
        return handle

    def close(self) -> None:
        for handle in reversed(tuple(self.handles.values())):
            handle.close()
        self.handles.clear()
        self.file_identities.clear()

    def __exit__(self, *_args: object) -> None:
        self.close()


_ACTIVE_SESSION: _EvidenceSession | None = None


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
    if _ACTIVE_SESSION is None:
        _fail("VALIDATION_SESSION_MISSING")
    return _ACTIVE_SESSION.file(path).sha256()


def _read_bytes(path: Path, maximum: int) -> bytes:
    if _ACTIVE_SESSION is None:
        _fail("VALIDATION_SESSION_MISSING")
    return _ACTIVE_SESSION.file(path).read(maximum)


def _file_size(path: Path) -> int:
    if _ACTIVE_SESSION is None:
        _fail("VALIDATION_SESSION_MISSING")
    return _ACTIVE_SESSION.file(path).size


def _check_report_secrets(value: Any) -> None:
    if isinstance(value, dict):
        for key, nested in value.items():
            normalized = re.sub(r"[^a-z0-9]", "", key.casefold()) if isinstance(key, str) else ""
            if not isinstance(key, str) or any(word in normalized for word in SECRET_KEY_WORDS):
                _fail("SECRET_LIKE_REPORT_CONTENT")
            _check_report_secrets(nested)
    elif isinstance(value, list):
        for nested in value:
            _check_report_secrets(nested)
    elif isinstance(value, str) and SECRET_VALUE_PATTERN.search(value):
        _fail("SECRET_LIKE_REPORT_CONTENT")


def _json_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            _fail("JSON_DUPLICATE_KEY")
        result[key] = value
    return result


def _json_nonfinite(_value: str) -> None:
    _fail("JSON_NONFINITE")


def _exact_keys(value: Any, expected: set[str]) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != expected:
        _fail("RUN_REPORT_SCHEMA_INVALID")
    return value


def _strict_string(value: Any) -> bool:
    return isinstance(value, str) and bool(value)


def _strict_int(value: Any) -> bool:
    return type(value) is int and value >= 0


def _strict_number(value: Any) -> bool:
    return type(value) in {int, float} and math.isfinite(value)


def _validate_report_schema(report: dict[str, Any]) -> None:
    _exact_keys(
        report,
        {"authorized_by", "output", "requested_duration_seconds", "parts", "errors", "runtime_identity", "final_mode", "ok"},
    )
    if (
        not _strict_string(report["authorized_by"])
        or not _strict_string(report["output"])
        or not _strict_number(report["requested_duration_seconds"])
        or not isinstance(report["parts"], list)
        or report["errors"] != []
        or not _strict_string(report["final_mode"])
        or type(report["ok"]) is not bool
    ):
        _fail("RUN_REPORT_SCHEMA_INVALID")
    identity = _exact_keys(
        report["runtime_identity"],
        {"is_simulator", "preset", "probe_part_number", "firmware", "base_station", "headstage"},
    )
    if type(identity["is_simulator"]) is not bool or not all(
        _strict_string(identity[field])
        for field in ("preset", "probe_part_number", "firmware", "base_station", "headstage")
    ):
        _fail("RUN_REPORT_SCHEMA_INVALID")
    for part in report["parts"]:
        part = _exact_keys(
            part,
            {"part", "name", "preset", "preset_readback", "preset_evidence", "recording_root", "validation"},
        )
        if (
            not _strict_int(part["part"])
            or not all(_strict_string(part[field]) for field in ("name", "preset", "preset_readback", "recording_root"))
            or part["preset_evidence"] is not None
        ):
            _fail("RUN_REPORT_SCHEMA_INVALID")
        validation = _exact_keys(
            part["validation"],
            {"ok", "recording_root", "settings_path", "presets", "recording_units", "streams", "errors"},
        )
        if (
            type(validation["ok"]) is not bool
            or not _strict_string(validation["recording_root"])
            or not _strict_string(validation["settings_path"])
            or not isinstance(validation["presets"], list)
            or not all(_strict_string(item) for item in validation["presets"])
            or not _strict_int(validation["recording_units"])
            or not isinstance(validation["streams"], list)
            or validation["errors"] != []
        ):
            _fail("RUN_REPORT_SCHEMA_INVALID")
        for stream in validation["streams"]:
            stream = _exact_keys(
                stream,
                {"name", "channels", "sample_rate", "duration_seconds", "nonunit_sample_steps", "invalid_timestamps", "continuous_bytes", "errors"},
            )
            if (
                not _strict_string(stream["name"])
                or not _strict_int(stream["channels"])
                or not _strict_number(stream["sample_rate"])
                or not _strict_number(stream["duration_seconds"])
                or not _strict_int(stream["nonunit_sample_steps"])
                or not _strict_int(stream["invalid_timestamps"])
                or not _strict_int(stream["continuous_bytes"])
                or stream["errors"] != []
            ):
                _fail("RUN_REPORT_SCHEMA_INVALID")


def _load_report(root: Path) -> dict[str, Any]:
    path = _safe_existing(root, root / "data" / "run_report.json", file=True)
    try:
        report = json.loads(
            _read_bytes(path, MAX_REPORT_BYTES).decode("utf-8"),
            object_pairs_hook=_json_pairs,
            parse_constant=_json_nonfinite,
        )
    except HistoricalEvidenceError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError):
        _fail("RUN_REPORT_INVALID")
    if not isinstance(report, dict):
        _fail("RUN_REPORT_INVALID")
    _check_report_secrets(report)
    _validate_report_schema(report)
    return report


def _parse_manifest(root: Path) -> list[dict[str, Any]]:
    manifest = _safe_existing(root, root / "SHA256SUMS.txt", file=True)
    try:
        lines = _read_bytes(manifest, MAX_MANIFEST_BYTES).decode("ascii").splitlines()
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
            not raw_path
            or raw_path.endswith("/")
            or any(ord(character) < 32 or ord(character) == 127 for character in raw_path)
            or any(part in {"", ".", ".."} for part in raw_path.split("/"))
            or pure.is_absolute()
            or windows.is_absolute()
            or bool(windows.drive)
            or "\\" in raw_path
            or pure.as_posix() != raw_path
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
            "bytes": _file_size(root / relative),
            "sha256": _sha256(root / relative),
        }
        for relative in sorted(observed)
    ]


def _npy_values(path: Path, descriptor: str) -> tuple[int, Iterable[int | float]]:
    try:
        payload = _read_bytes(path, MAX_NPY_BYTES)
        if payload[:6] != b"\x93NUMPY" or len(payload) < 10:
            _fail("NPY_INVALID")
        major, _minor = payload[6:8]
        if major not in {1, 2, 3}:
            _fail("NPY_INVALID")
        length_size = 2 if major == 1 else 4
        offset = 8
        header_length = struct.unpack(
            "<H" if length_size == 2 else "<I", payload[offset : offset + length_size]
        )[0]
        if header_length > MAX_NPY_HEADER_BYTES:
            _fail("NPY_INVALID")
        offset += length_size
        header = ast.literal_eval(payload[offset : offset + header_length].decode("latin1"))
        if (
            header.get("descr") != descriptor
            or header.get("fortran_order") is not False
            or not isinstance(header.get("shape"), tuple)
            or len(header["shape"]) != 1
            or type(header["shape"][0]) is not int
            or header["shape"][0] < 0
            or header["shape"][0] > MAX_NPY_COUNT
        ):
            _fail("NPY_INVALID")
        count = header["shape"][0]
        data = payload[offset + header_length :]
    except (OSError, SyntaxError, ValueError, struct.error, UnicodeError):
        _fail("NPY_INVALID")
    if len(data) != count * 8:
        _fail("NPY_COUNT_MISMATCH")
    code = "q" if descriptor == "<i8" else "d"
    return count, (item[0] for item in struct.iter_unpack(f"<{code}", data))


def _file_record(root: Path, path: Path) -> dict[str, Any]:
    return {
        "path": path.relative_to(root).as_posix(),
        "bytes": _file_size(path),
        "sha256": _sha256(path),
    }


def _validate_structure(
    path: Path, stream_directory: Path, record_node_id: int
) -> None:
    try:
        payload = json.loads(
            _read_bytes(path, MAX_STRUCTURE_BYTES).decode("utf-8"),
            object_pairs_hook=_json_pairs,
            parse_constant=_json_nonfinite,
        )
    except HistoricalEvidenceError:
        raise
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
        or stream.get("source_processor_name") not in {None, "OneBox"}
        or stream.get("source_processor_id") not in {None, 100}
        or stream.get("recorded_processor") != "Record Node"
        or stream.get("recorded_processor_id") != record_node_id
        or stream.get("num_channels") != 384
        or not isinstance(stream.get("channels"), list)
        or len(stream["channels"]) != 384
    ):
        _fail("STRUCTURE_OEBIN_MISMATCH")


def _validate_settings(path: Path, preset: str, record_node_id: int) -> None:
    try:
        payload = _read_bytes(path, MAX_SETTINGS_BYTES)
        if b"<!DOCTYPE" in payload.upper():
            _fail("SETTINGS_XML_MISMATCH")
        root = ET.fromstring(payload)
    except HistoricalEvidenceError:
        raise
    except (ET.ParseError, OSError, ValueError):
        _fail("SETTINGS_XML_MISMATCH")
    if root.tag != "SETTINGS":
        _fail("SETTINGS_XML_MISMATCH")
    processors = list(root.iter("PROCESSOR"))
    oneboxes = [item for item in processors if item.get("name") == "OneBox"]
    record_nodes = [item for item in processors if item.get("name") == "Record Node"]
    if len(oneboxes) != 1 or len(record_nodes) != 1:
        _fail("SETTINGS_XML_MISMATCH")
    onebox, record_node = oneboxes[0], record_nodes[0]
    if onebox.get("nodeId") != "100" or record_node.get("nodeId") != str(record_node_id):
        _fail("SETTINGS_XML_MISMATCH")
    for processor in (onebox, record_node):
        streams = processor.findall("./STREAM")
        if not any(
            stream.get("name") == "ProbeA"
            and stream.get("sample_rate") == "30000.0"
            and stream.get("channel_count") == "384"
            for stream in streams
        ):
            _fail("SETTINGS_XML_MISMATCH")
    probes = onebox.findall(".//NP_PROBE")
    if len(probes) != 2:
        _fail("SETTINGS_XML_MISMATCH")
    observed_presets = [probe.get("electrodeConfigurationPreset") for probe in probes]
    if observed_presets != [preset, "NONE"]:
        _fail("SETTINGS_XML_MISMATCH")
    if any(
        probe.get("bs_firmware_version") != "SIM 0.0"
        or probe.get("probe_part_number") != "NP2013"
        or probe.get("probe_name") != "Neuropixels 2.0 - Multishank"
        for probe in probes
    ):
        _fail("SETTINGS_XML_MISMATCH")


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
    if any(_file_size(path) <= 0 for path in required_files):
        _fail("REQUIRED_FILE_EMPTY")
    record_node_id = int(record_node_match.group(1))
    _validate_settings(settings, expected_preset, record_node_id)
    _validate_structure(structure, stream_directory, record_node_id)

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
    if _file_size(continuous) != stream.get("continuous_bytes"):
        _fail("CONTINUOUS_BYTES_MISMATCH")
    sample_count, sample_values = _npy_values(sample_numbers, "<i8")
    timestamp_count, timestamp_values = _npy_values(timestamps, "<f8")
    if sample_count != timestamp_count or _file_size(continuous) != sample_count * 384 * 2:
        _fail("SAMPLE_COUNT_MISMATCH")
    previous_sample: int | None = None
    for sample in sample_values:
        if type(sample) is not int or (
            previous_sample is not None and sample - previous_sample != 1
        ):
            _fail("STRUCTURAL_REPORT_INVALID")
        previous_sample = sample
    first_timestamp: float | None = None
    previous_timestamp: float | None = None
    expected_step = 1.0 / 30000.0
    for timestamp in timestamp_values:
        if not math.isfinite(timestamp) or timestamp == -1.0:
            _fail("TIMESTAMPS_INVALID")
        if previous_timestamp is not None:
            step = timestamp - previous_timestamp
            if step <= 0.0 or abs(step - expected_step) > 1e-9:
                _fail("TIMESTAMPS_INVALID")
        if first_timestamp is None:
            first_timestamp = timestamp
        previous_timestamp = timestamp
    if sample_count and (
        first_timestamp is None
        or previous_timestamp is None
        or abs((previous_timestamp - first_timestamp) - (sample_count - 1) * expected_step) > 1e-7
    ):
        _fail("TIMESTAMPS_INVALID")
    duration = stream.get("duration_seconds")
    if not isinstance(duration, (int, float)) or abs(duration - sample_count / 30000.0) > 1e-9:
        _fail("STRUCTURAL_REPORT_INVALID")
    return {
        "part": index,
        "preset": expected_preset,
        "native_directory": part_root.relative_to(root).as_posix(),
        "recording_unit": unit.relative_to(root).as_posix(),
        "continuous_bytes": _file_size(continuous),
        "sample_count": sample_count,
        "files": [_file_record(root, path) for path in required_files],
    }


def _validate_bound_evidence(root: Path) -> dict[str, Any]:
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
        "delivery_sha256_kind": "canonical_validated_evidence_digest",
        "simulator": {
            "firmware": "SIM 0.0",
            "probe_part_number": "NP2013",
            "channels": 384,
            "sample_rate": 30000,
        },
        "parts": part_results,
        "sha256_manifest": manifest,
    }
    return result


def validate_historical_source_sim(evidence_root: str | Path) -> dict[str, Any]:
    """Validate a historical delivery without mutating it or qualifying this fork."""
    global _ACTIVE_SESSION
    root = Path(evidence_root).absolute()
    if not root.is_dir() or _is_reparse(root):
        _fail("EVIDENCE_ROOT_INVALID")
    if _ACTIVE_SESSION is not None:
        _fail("VALIDATION_SESSION_REENTRY")
    try:
        with _EvidenceSession(root) as session:
            _ACTIVE_SESSION = session
            return _validate_bound_evidence(root)
    except HistoricalEvidenceError:
        raise
    except OSError:
        _fail("EVIDENCE_MUTATED_DURING_VALIDATION")
    finally:
        _ACTIVE_SESSION = None
