import argparse
import hashlib
import importlib.metadata
import json
import math
import time
from pathlib import Path

import numpy as np
from open_ephys.analysis import Session


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def one(root: Path, name: str) -> Path:
    matches = list(root.rglob(name))
    require(len(matches) == 1, f"expected one {name} under {root}; got {len(matches)}")
    return matches[0]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--process-count", type=int, required=True)
    parser.add_argument("--listener-count", type=int, required=True)
    args = parser.parse_args()

    run = args.run.resolve()
    package = args.package.resolve()
    require(args.process_count == 0, "Open Ephys process remains during offline audit")
    require(args.listener_count == 0, "protected listener remains during offline audit")
    loader_version = importlib.metadata.version("open-ephys-python-tools")
    require(loader_version == "1.0.1", f"unexpected official loader version: {loader_version}")

    manifest = json.loads((run / "RUN-MANIFEST.json").read_text(encoding="utf-8-sig"))
    qualification = json.loads(
        (run / "evidence/file-reader-gui-qualification-result.json").read_text(
            encoding="utf-8-sig"
        )
    )
    restart = json.loads(
        (run / "evidence/restart-recovery-result.json").read_text(encoding="utf-8-sig")
    )
    require(manifest["approved_block_count"] == 8, "manifest block count is not eight")
    require(len(manifest["blocks"]) == 8, "manifest block plan is incomplete")
    require(qualification["pass"] is True, "qualification reported failure")
    require(qualification["final_status"]["mode"] == "IDLE", "qualification did not end IDLE")
    require(restart["pass"] is True, "restart recovery reported failure")
    require(restart["status"]["mode"] == "IDLE", "restart did not read back IDLE")
    require(restart["status"]["mutation_allowed"] is False, "restart was not read-only")

    controls = qualification["negative_controls"]
    require(controls["unauthorized_requests"]["pass"], "authentication control failed")
    require(controls["stale_revision"]["pass"], "stale revision control failed")
    require(controls["request_id_semantics"]["pass"], "request-id control failed")
    require(controls["directory_collision"]["pass"], "collision control failed")

    package_manifest_path = package / "RUN-MANIFEST.json"
    package_manifest = json.loads(package_manifest_path.read_text(encoding="utf-8-sig"))
    package_failures = []
    for entry in package_manifest["files"]:
        candidate = package / entry["path"]
        if not candidate.is_file() or sha256(candidate) != entry["sha256"]:
            package_failures.append(entry["path"])
    require(not package_failures, f"package hash failures: {package_failures}")

    recordings = run / "recordings"
    expected_names = [f"BLOCK_{index:02d}" for index in range(1, 9)]
    observed_names = sorted(path.name for path in recordings.iterdir() if path.is_dir())
    require(observed_names == expected_names, f"unexpected block directories: {observed_names}")

    data_paths = [one(recordings / name, "continuous.dat") for name in expected_names]
    stability = []
    for observation in range(3):
        stability.append([path.stat().st_size for path in data_paths])
        if observation < 2:
            time.sleep(5)
    require(stability[0] == stability[1] == stability[2], "payload changed during offline audit")

    blocks = []
    hashes = set()
    for index, name in enumerate(expected_names, start=1):
        block = recordings / name
        data_path = one(block, "continuous.dat")
        structure_path = one(block, "structure.oebin")
        settings_path = one(block, "settings.xml")
        sample_paths = [
            path for path in block.rglob("sample_numbers.npy")
            if "continuous" in path.parts
        ]
        require(len(sample_paths) == 1, "continuous sample numbers are ambiguous")
        sample_path = sample_paths[0]
        timestamp_paths = list(block.rglob("timestamps.npy"))
        continuous_timestamp_paths = [
            path for path in timestamp_paths if "continuous" in path.parts
        ]
        require(len(continuous_timestamp_paths) == 1, "continuous timestamps are ambiguous")
        timestamp_path = continuous_timestamp_paths[0]

        structure = json.loads(structure_path.read_text(encoding="utf-8"))
        continuous = structure["continuous"]
        require(len(continuous) == 1, "structure has multiple continuous streams")
        require(continuous[0]["num_channels"] == 16, "channel count is not 16")
        require(float(continuous[0]["sample_rate"]) == 40000.0, "sample rate is not 40 kHz")

        size = data_path.stat().st_size
        require(size % 32 == 0, "continuous.dat is not frame aligned")
        frames = size // 32
        duration = frames / 40000.0
        require(10.0 <= duration <= 12.0, f"duration out of range for {name}: {duration}")

        sample_numbers = np.load(sample_path, mmap_mode="r")
        timestamps = np.load(timestamp_path, mmap_mode="r")
        require(sample_numbers.ndim == 1 and len(sample_numbers) == frames, "sample-number length mismatch")
        require(timestamps.ndim == 1 and len(timestamps) == frames, "timestamp length mismatch")
        require(np.isfinite(timestamps).all(), "timestamps contain non-finite values")
        sample_diffs = np.diff(sample_numbers)
        timestamp_diffs = np.diff(timestamps)
        require(np.all(sample_diffs == 1), "sample numbers are not contiguous and monotonic")
        require(
            np.allclose(timestamp_diffs, 1.0 / 40000.0, rtol=0, atol=1e-10),
            "timestamps are not contiguous and monotonic at 40 kHz",
        )

        session = Session(str(block))
        require(len(session.recordnodes) == 1, "official loader record-node count mismatch")
        require(len(session.recordnodes[0].recordings) == 1, "official loader recording count mismatch")
        recording = session.recordnodes[0].recordings[0]
        require(len(recording.continuous) == 1, "official loader stream count mismatch")
        loaded = recording.continuous[0]
        require(loaded.samples.shape == (frames, 16), "official loader sample shape mismatch")
        require(loaded.sample_numbers.shape == (frames,), "official loader sample-number shape mismatch")
        require(loaded.timestamps.shape == (frames,), "official loader timestamp shape mismatch")
        require(loaded.metadata.sample_rate == 40000.0, "official loader sample rate mismatch")
        require(loaded.metadata.num_channels == 16, "official loader channel count mismatch")

        digest = sha256(data_path)
        require(digest not in hashes, "duplicate continuous.dat hash across blocks")
        hashes.add(digest)
        blocks.append(
            {
                "index": index,
                "directory": name,
                "continuous_file": str(data_path),
                "bytes": size,
                "frames": frames,
                "duration_seconds": round(duration, 6),
                "sha256": digest,
                "sample_number_first": int(sample_numbers[0]),
                "sample_number_last": int(sample_numbers[-1]),
                "sample_number_continuity": "PASS",
                "timestamp_continuity": "PASS",
                "official_loader": "PASS",
                "settings_file": str(settings_path),
            }
        )

    output = {
        "schema_version": "oe-agent-independent-file-reader-audit/v1",
        "pass": True,
        "qualification_result_was_not_trusted": True,
        "process_count": args.process_count,
        "protected_listener_count": args.listener_count,
        "package_source_commit": package_manifest["source_commit"],
        "package_file_count": len(package_manifest["files"]),
        "package_manifest_sha256": sha256(package_manifest_path),
        "package_hash_failures": package_failures,
        "block_count": len(blocks),
        "stability_observations": stability,
        "blocks": blocks,
        "official_loader": {
            "package": "open-ephys-python-tools",
            "version": loader_version,
            "all_blocks": "PASS",
        },
        "file_reader_timeline_note": (
            "The qualification uses a manifest-bound 120-second derived source so "
            "no source loop boundary occurs during the eight 10-second blocks."
        ),
        "FILE_READER_EIGHT_BLOCK_QUALIFICATION": "PASS",
        "NEUROPIXELS_SIM_CAPABILITY": "UNVERIFIED",
        "EIGHT_PRESET_SIM_ACCEPTANCE": "BLOCKED",
        "EIGHT_SHANK_CLAIM": "PROHIBITED",
        "SCIENTIFIC_SIGNAL_QC": "NEEDS_REVIEW",
    }
    output_path = run / "evidence/independent-file-reader-audit.json"
    output_path.write_text(json.dumps(output, indent=2), encoding="utf-8")
    print(json.dumps(output, indent=2))


if __name__ == "__main__":
    main()
