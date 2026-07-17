import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

import pytest

from open_ephys_agent_mcp.experiment.historical_source_sim import (
    HistoricalEvidenceError,
    validate_historical_source_sim,
)
import open_ephys_agent_mcp.experiment.historical_source_sim as historical_module


ROOT = Path(__file__).resolve().parents[4]
MODULE = (
    ROOT
    / "integrations"
    / "mcp"
    / "src"
    / "open_ephys_agent_mcp"
    / "experiment"
    / "historical_source_sim.py"
)
CLI = ROOT / "tools" / "windows" / "validate_historical_source_sim.py"


def test_historical_source_sim_validator_module_exists() -> None:
    assert MODULE.is_file()


PRESETS = [
    "All Shanks 1-96",
    "All Shanks 97-192",
    "All Shanks 193-288",
    "All Shanks 289-384",
    "All Shanks 385-480",
    "All Shanks 481-576",
    "All Shanks 577-672",
    "All Shanks 673-768",
]
MANIFEST_PATHS = (
    "video/full_workflow_realtime.mp4",
    "video/full_workflow.mp4",
    "video/trace.jsonl",
    "data/run_report.json",
)


def write_npy(path: Path, descriptor: str, values: list[int | float]) -> None:
    header = repr(
        {"descr": descriptor, "fortran_order": False, "shape": (len(values),)}
    ).encode("latin1")
    padding = b" " * ((16 - ((10 + len(header) + 1) % 16)) % 16)
    header = header + padding + b"\n"
    code = "q" if descriptor == "<i8" else "d"
    path.write_bytes(
        b"\x93NUMPY\x01\x00"
        + struct.pack("<H", len(header))
        + header
        + struct.pack(f"<{len(values)}{code}", *values)
    )


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def write_report(root: Path, report: dict[str, object]) -> None:
    path = root / "data" / "run_report.json"
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")


def write_manifest(root: Path, paths: tuple[str, ...] = MANIFEST_PATHS) -> None:
    lines = [f"{sha256(root / relative)}  {relative}" for relative in paths]
    (root / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="ascii")


@pytest.fixture()
def delivery(tmp_path: Path) -> tuple[Path, dict[str, object]]:
    root = tmp_path / "delivery"
    (root / "data").mkdir(parents=True)
    (root / "video").mkdir()
    for relative in MANIFEST_PATHS[:3]:
        path = root / relative
        path.write_bytes(f"fixture:{relative}".encode())

    parts = []
    for index, preset in enumerate(PRESETS, start=1):
        part_root = root / "data" / f"SIMTEST_part{index}"
        node = part_root / "Record Node 101"
        unit = node / "experiment1" / "recording1"
        stream = unit / "continuous" / "OneBox-100.ProbeA"
        stream.mkdir(parents=True)
        (node / "settings.xml").write_text(
            f"""<SETTINGS><SIGNALCHAIN>
<PROCESSOR name="OneBox" nodeId="100" pluginName="OneBox">
  <STREAM name="ProbeA" sample_rate="30000.0" channel_count="384" />
  <EDITOR><NEUROPIXELS_EDITOR>
    <NP_PROBE bs_firmware_version="SIM 0.0" probe_part_number="NP2013"
      probe_name="Neuropixels 2.0 - Multishank"
      electrodeConfigurationPreset="{preset}" />
    <NP_PROBE bs_firmware_version="SIM 0.0" probe_part_number="NP2013"
      probe_name="Neuropixels 2.0 - Multishank"
      electrodeConfigurationPreset="NONE" />
  </NEUROPIXELS_EDITOR></EDITOR>
</PROCESSOR>
<PROCESSOR name="Record Node" nodeId="101" pluginName="Record Node">
  <STREAM name="ProbeA" sample_rate="30000.0" channel_count="384" />
</PROCESSOR>
</SIGNALCHAIN></SETTINGS>""",
            encoding="utf-8",
        )
        (unit / "structure.oebin").write_text(
            json.dumps(
                {
                    "GUI version": "1.0.2",
                    "continuous": [
                        {
                            "folder_name": "OneBox-100.ProbeA/",
                            "sample_rate": 30000.0,
                            "stream_name": "ProbeA",
                            "recorded_processor": "Record Node",
                            "recorded_processor_id": 101,
                            "num_channels": 384,
                            "channels": [{} for _ in range(384)],
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        continuous = stream / "continuous.dat"
        continuous.write_bytes(bytes(384 * 2 * 4))
        write_npy(stream / "sample_numbers.npy", "<i8", [0, 1, 2, 3])
        write_npy(stream / "timestamps.npy", "<f8", [0.0, 1 / 30000, 2 / 30000, 3 / 30000])
        parts.append(
            {
                "part": index,
                "name": part_root.name,
                "preset": preset,
                "preset_readback": preset,
                "preset_evidence": None,
                "recording_root": str(part_root),
                "validation": {
                    "ok": True,
                    "recording_root": str(part_root),
                    "settings_path": str(node / "settings.xml"),
                    "presets": [preset, "NONE"],
                    "recording_units": 1,
                    "streams": [
                        {
                            "name": "ProbeA",
                            "channels": 384,
                            "sample_rate": 30000.0,
                            "duration_seconds": 4 / 30000,
                            "nonunit_sample_steps": 0,
                            "invalid_timestamps": 0,
                            "continuous_bytes": continuous.stat().st_size,
                            "errors": [],
                        }
                    ],
                    "errors": [],
                },
            }
        )
    report: dict[str, object] = {
        "authorized_by": "historical-user-request",
        "output": str(root / "data"),
        "requested_duration_seconds": 3.0,
        "parts": parts,
        "errors": [],
        "runtime_identity": {
            "is_simulator": True,
            "preset": PRESETS[-1],
            "probe_part_number": "NP2013",
            "firmware": "SIM 0.0",
            "base_station": "Simulated BS",
            "headstage": "Simulated headstage",
        },
        "final_mode": "IDLE",
        "ok": True,
    }
    write_report(root, report)
    write_manifest(root)
    return root, report


def expect_error(root: Path, code: str) -> None:
    with pytest.raises(HistoricalEvidenceError, match=code):
        validate_historical_source_sim(root)


def test_valid_fixture_is_deterministic_historical_evidence(
    delivery: tuple[Path, dict[str, object]],
) -> None:
    root, _ = delivery
    first = validate_historical_source_sim(root)
    second = validate_historical_source_sim(root)
    assert first == second
    assert first["provenance"] == "historical_official_gui_simulation"
    assert first["qualifies_current_fork"] is False
    assert len(first["delivery_sha256"]) == 64
    assert [part["preset"] for part in first["parts"]] == PRESETS
    assert all(len(part["files"]) == 5 for part in first["parts"])


@pytest.mark.parametrize("mutation", ["wrong", "order"])
def test_rejects_wrong_or_reordered_preset(
    delivery: tuple[Path, dict[str, object]], mutation: str
) -> None:
    root, report = delivery
    parts = report["parts"]
    if mutation == "wrong":
        parts[0]["preset"] = "All Shanks 97-192"
    else:
        parts[0]["preset"], parts[1]["preset"] = (
            parts[1]["preset"],
            parts[0]["preset"],
        )
        parts[0]["preset_readback"], parts[1]["preset_readback"] = (
            parts[1]["preset_readback"],
            parts[0]["preset_readback"],
        )
    write_report(root, report)
    write_manifest(root)
    expect_error(root, "PRESET_SEQUENCE_INVALID")


def test_rejects_duplicate_native_directory(
    delivery: tuple[Path, dict[str, object]],
) -> None:
    root, report = delivery
    parts = report["parts"]
    parts[1]["recording_root"] = parts[0]["recording_root"]
    parts[1]["validation"]["recording_root"] = parts[0]["recording_root"]
    parts[1]["validation"]["settings_path"] = parts[0]["validation"]["settings_path"]
    write_report(root, report)
    write_manifest(root)
    expect_error(root, "DUPLICATE_NATIVE_DIRECTORY")


def test_rejects_auto_suffixed_native_directory(
    delivery: tuple[Path, dict[str, object]],
) -> None:
    root, report = delivery
    parts = report["parts"]
    suffixed = root / "data" / "SIMTEST_part1 (1)"
    parts[0]["recording_root"] = str(suffixed)
    parts[0]["validation"]["recording_root"] = str(suffixed)
    parts[0]["validation"]["settings_path"] = str(suffixed / "Record Node 101" / "settings.xml")
    write_report(root, report)
    write_manifest(root)
    expect_error(root, "SUFFIXED_NATIVE_DIRECTORY")


def test_rejects_non_sim_identity(delivery: tuple[Path, dict[str, object]]) -> None:
    root, report = delivery
    report["runtime_identity"]["firmware"] = "real firmware"
    write_report(root, report)
    write_manifest(root)
    expect_error(root, "SIMULATOR_IDENTITY_INVALID")


def test_rejects_report_output_ambiguity(
    delivery: tuple[Path, dict[str, object]],
) -> None:
    root, report = delivery
    report["output"] = str(root / "video")
    write_report(root, report)
    write_manifest(root)
    expect_error(root, "REPORT_OUTPUT_MISMATCH")


def test_rejects_missing_required_file(delivery: tuple[Path, dict[str, object]]) -> None:
    root, _ = delivery
    next(root.rglob("timestamps.npy")).unlink()
    expect_error(root, "REQUIRED_FILE_MISSING")


def test_rejects_continuous_byte_mismatch(
    delivery: tuple[Path, dict[str, object]],
) -> None:
    root, _ = delivery
    next(root.rglob("continuous.dat")).write_bytes(b"wrong")
    expect_error(root, "CONTINUOUS_BYTES_MISMATCH")


def test_rejects_structure_oebin_mismatch(
    delivery: tuple[Path, dict[str, object]],
) -> None:
    root, _ = delivery
    structure = next(root.rglob("structure.oebin"))
    payload = json.loads(structure.read_text(encoding="utf-8"))
    payload["continuous"][0]["num_channels"] = 383
    structure.write_text(json.dumps(payload), encoding="utf-8")
    expect_error(root, "STRUCTURE_OEBIN_MISMATCH")


def test_rejects_record_node_identity_mismatch(
    delivery: tuple[Path, dict[str, object]],
) -> None:
    root, _ = delivery
    structure = next(root.rglob("structure.oebin"))
    payload = json.loads(structure.read_text(encoding="utf-8"))
    payload["continuous"][0]["recorded_processor_id"] = 102
    structure.write_text(json.dumps(payload), encoding="utf-8")
    expect_error(root, "STRUCTURE_OEBIN_MISMATCH")


@pytest.mark.parametrize(
    ("attribute", "value"),
    [
        ("electrodeConfigurationPreset", "All Shanks 97-192"),
        ("bs_firmware_version", "REAL 1.0"),
        ("probe_part_number", "NP9999"),
    ],
)
def test_rejects_settings_probe_mismatch(
    delivery: tuple[Path, dict[str, object]], attribute: str, value: str
) -> None:
    root, _ = delivery
    settings = next(root.rglob("settings.xml"))
    text = settings.read_text(encoding="utf-8")
    text = text.replace(f'{attribute}="{PRESETS[0] if attribute == "electrodeConfigurationPreset" else "SIM 0.0" if attribute == "bs_firmware_version" else "NP2013"}"', f'{attribute}="{value}"', 1)
    settings.write_text(text, encoding="utf-8")
    expect_error(root, "SETTINGS_XML_MISMATCH")


def test_rejects_settings_record_node_mismatch(
    delivery: tuple[Path, dict[str, object]],
) -> None:
    root, _ = delivery
    settings = next(root.rglob("settings.xml"))
    text = settings.read_text(encoding="utf-8").replace(
        'name="Record Node" nodeId="101"', 'name="Record Node" nodeId="102"'
    )
    settings.write_text(text, encoding="utf-8")
    expect_error(root, "SETTINGS_XML_MISMATCH")


@pytest.mark.parametrize(
    "values",
    [
        [0.0, 1 / 30000, float("nan"), 3 / 30000],
        [0.0, 1 / 30000, -1.0, 3 / 30000],
        [0.0, 1 / 30000, 1 / 30000, 3 / 30000],
        [0.0, 1 / 20000, 2 / 20000, 3 / 20000],
    ],
)
def test_rejects_invalid_recomputed_timestamps(
    delivery: tuple[Path, dict[str, object]], values: list[float]
) -> None:
    root, _ = delivery
    write_npy(next(root.rglob("timestamps.npy")), "<f8", values)
    expect_error(root, "TIMESTAMPS_INVALID")


@pytest.mark.parametrize(
    ("kind", "code"),
    [
        ("malformed", "HASH_MANIFEST_MALFORMED"),
        ("traversal", "HASH_MANIFEST_PATH_INVALID"),
        ("absolute", "HASH_MANIFEST_PATH_INVALID"),
        ("duplicate", "HASH_MANIFEST_DUPLICATE"),
        ("missing", "HASH_MANIFEST_INVENTORY_MISMATCH"),
        ("extra", "HASH_MANIFEST_INVENTORY_MISMATCH"),
        ("mismatch", "HASH_MISMATCH"),
    ],
)
def test_rejects_bad_hash_manifest(
    delivery: tuple[Path, dict[str, object]], kind: str, code: str
) -> None:
    root, _ = delivery
    manifest = root / "SHA256SUMS.txt"
    lines = manifest.read_text(encoding="ascii").splitlines()
    if kind == "malformed":
        lines[0] = "not-a-valid-entry"
    elif kind == "traversal":
        lines[0] = f"{'a' * 64}  ../outside"
    elif kind == "absolute":
        lines[0] = f"{'a' * 64}  C:/outside"
    elif kind == "duplicate":
        lines.append(lines[0])
    elif kind == "missing":
        lines.pop()
    elif kind == "extra":
        (root / "SUMMARY_CN.md").write_text("summary", encoding="utf-8")
        lines.append(f"{sha256(root / 'SUMMARY_CN.md')}  SUMMARY_CN.md")
    else:
        (root / MANIFEST_PATHS[0]).write_bytes(b"changed")
    manifest.write_text("\n".join(lines) + "\n", encoding="ascii")
    expect_error(root, code)


@pytest.mark.parametrize(
    "alias",
    [
        "video//full_workflow.mp4",
        "video/./full_workflow.mp4",
        "video/full_workflow.mp4/",
        "video/full_workflow.mp4\x01",
        "C:drive-relative",
    ],
)
def test_rejects_noncanonical_manifest_alias(
    delivery: tuple[Path, dict[str, object]], alias: str
) -> None:
    root, _ = delivery
    manifest = root / "SHA256SUMS.txt"
    lines = manifest.read_text(encoding="ascii").splitlines()
    lines[1] = f"{'a' * 64}  {alias}"
    manifest.write_text("\n".join(lines) + "\n", encoding="ascii")
    expect_error(root, "HASH_MANIFEST_PATH_INVALID")


def test_rejects_symlink_escape(
    delivery: tuple[Path, dict[str, object]],
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    root, _ = delivery
    continuous = next(root.rglob("continuous.dat"))
    outside = tmp_path / "outside.dat"
    outside.write_bytes(continuous.read_bytes())
    continuous.unlink()
    try:
        os.symlink(outside, continuous)
    except OSError:
        continuous.write_bytes(outside.read_bytes())
        real_check = historical_module._is_reparse

        def report_continuous_as_reparse(path: Path) -> bool:
            return path.name == "continuous.dat" or real_check(path)

        monkeypatch.setattr(
            historical_module, "_is_reparse", report_continuous_as_reparse
        )
    expect_error(root, "REPARSE_POINT_FORBIDDEN")


def test_rejects_secret_like_report_field(
    delivery: tuple[Path, dict[str, object]],
) -> None:
    root, report = delivery
    report["api_token"] = "sk-this-must-not-be-imported"
    write_report(root, report)
    write_manifest(root)
    expect_error(root, "SECRET_LIKE_REPORT_CONTENT")


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("accessToken", "ordinary-value"),
        ("passwd", "ordinary-value"),
        ("authorization", "ordinary-value"),
        ("cookie", "ordinary-value"),
        ("note", "ghp_abcdefghijklmnopqrstuvwxyz123456"),
        ("note", "AKIAABCDEFGHIJKLMNOP"),
        ("note", "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxIn0.signature"),
        ("note", "-----BEGIN PRIVATE KEY-----"),
    ],
)
def test_rejects_extended_secret_patterns(
    delivery: tuple[Path, dict[str, object]], field: str, value: str
) -> None:
    root, report = delivery
    report[field] = value
    write_report(root, report)
    write_manifest(root)
    expect_error(root, "SECRET_LIKE_REPORT_CONTENT")


def test_rejects_json_duplicate_key(delivery: tuple[Path, dict[str, object]]) -> None:
    root, _ = delivery
    report_path = root / "data" / "run_report.json"
    text = report_path.read_text(encoding="utf-8")
    report_path.write_text(text.replace('{\n  "authorized_by"', '{\n  "ok": true,\n  "authorized_by"'), encoding="utf-8")
    write_manifest(root)
    expect_error(root, "JSON_DUPLICATE_KEY")


def test_rejects_json_nan(delivery: tuple[Path, dict[str, object]]) -> None:
    root, _ = delivery
    report_path = root / "data" / "run_report.json"
    text = report_path.read_text(encoding="utf-8").replace(
        '"requested_duration_seconds": 3.0', '"requested_duration_seconds": NaN'
    )
    report_path.write_text(text, encoding="utf-8")
    write_manifest(root)
    expect_error(root, "JSON_NONFINITE")


@pytest.mark.parametrize("mutation", ["unknown-field", "bool-as-int"])
def test_rejects_non_strict_report_schema(
    delivery: tuple[Path, dict[str, object]], mutation: str
) -> None:
    root, report = delivery
    if mutation == "unknown-field":
        report["unexpected"] = "value"
    else:
        report["parts"][0]["part"] = True
    write_report(root, report)
    write_manifest(root)
    expect_error(root, "RUN_REPORT_SCHEMA_INVALID")


def test_rejects_evidence_mutation_during_validation(
    delivery: tuple[Path, dict[str, object]], monkeypatch: pytest.MonkeyPatch
) -> None:
    root, _ = delivery
    continuous = next(root.rglob("continuous.dat"))
    original_stat = continuous.stat()
    real_parse = historical_module._parse_manifest

    def parse_then_mutate(candidate: Path) -> list[dict[str, object]]:
        entries = real_parse(candidate)
        payload = bytearray(continuous.read_bytes())
        payload[0] ^= 1
        continuous.write_bytes(payload)
        os.utime(
            continuous,
            ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns),
        )
        return entries

    monkeypatch.setattr(historical_module, "_parse_manifest", parse_then_mutate)
    expect_error(root, "EVIDENCE_MUTATED_DURING_VALIDATION")


def test_rejects_swap_and_restore_during_validation(
    delivery: tuple[Path, dict[str, object]],
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    root, _ = delivery
    continuous = next(root.rglob("continuous.dat"))
    backup = continuous.with_suffix(".original")
    replacement = tmp_path / "replacement.dat"
    replacement.write_bytes(continuous.read_bytes())
    real_parse = historical_module._parse_manifest

    def parse_then_swap_restore(candidate: Path) -> list[dict[str, object]]:
        entries = real_parse(candidate)
        os.replace(continuous, backup)
        os.replace(replacement, continuous)
        os.replace(backup, continuous)
        return entries

    monkeypatch.setattr(historical_module, "_parse_manifest", parse_then_swap_restore)
    expect_error(root, "EVIDENCE_MUTATED_DURING_VALIDATION")


def test_rejects_duplicate_file_identity_alias(
    delivery: tuple[Path, dict[str, object]],
) -> None:
    root, _ = delivery
    settings = sorted(root.rglob("settings.xml"))
    settings[1].unlink()
    os.link(settings[0], settings[1])
    expect_error(root, "FILE_IDENTITY_COLLISION")


def test_readonly_cli_prints_machine_result(
    delivery: tuple[Path, dict[str, object]],
) -> None:
    root, _ = delivery
    completed = subprocess.run(
        [sys.executable, str(CLI), str(root)],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, completed.stderr
    result = json.loads(completed.stdout)
    assert result["qualifies_current_fork"] is False
