#!/usr/bin/env python3
"""Validate an observation report or complete eight-part run manifest."""

from __future__ import annotations

import json
import re
import sys
from datetime import datetime
from pathlib import Path
from pathlib import PureWindowsPath
from typing import NoReturn


TOP_LEVEL_FIELDS = {
    "schema_version",
    "status",
    "native",
    "preflight",
    "shanks",
    "summary",
}
RUN_TOP_LEVEL_FIELDS = {
    "schema_version",
    "run_id",
    "subject_id",
    "operator",
    "protocol_id",
    "created_at_utc",
    "experiment_root",
    "archive_root",
    "hardware",
    "storage",
    "stream_expectations",
    "qc_policy",
    "authorizations",
    "parts",
}
DEFAULT_PARTS = tuple(
    (
        index,
        f"All Shanks {1 + (index - 1) * 96}-{index * 96}",
        1 + (index - 1) * 96,
        index * 96,
    )
    for index in range(1, 9)
)
SECRET_KEY_PARTS = ("token", "authorization", "password", "secret", "bearer")
NON_SECRET_SCHEMA_KEYS = {"authorizations"}
SAFE_ID = re.compile(r"^[A-Za-z0-9._:-]{1,128}$")
AUTO_SUFFIX = re.compile(r" \([1-9][0-9]*\)$")
RESERVED_NAMES = {
    "CON",
    "PRN",
    "AUX",
    "NUL",
    *(f"COM{index}" for index in range(1, 10)),
    *(f"LPT{index}" for index in range(1, 10)),
}


class ManifestError(ValueError):
    def __init__(self, code: str) -> None:
        super().__init__(code)
        self.code = code


def fail(code: str) -> NoReturn:
    raise ManifestError(code)


def reject_secret_fields(value: object) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            folded = str(key).casefold()
            if (
                folded not in NON_SECRET_SCHEMA_KEYS
                and any(part in folded for part in SECRET_KEY_PARTS)
            ):
                fail("SECRET_FIELD_FORBIDDEN")
            reject_secret_fields(child)
    elif isinstance(value, list):
        for child in value:
            reject_secret_fields(child)


def validate_native(value: object) -> None:
    if not isinstance(value, dict) or set(value) != {
        "session_id",
        "revision",
        "mode",
        "gui_version",
    }:
        fail("INVALID_NATIVE_IDENTITY")
    session_id = value["session_id"]
    revision = value["revision"]
    if not isinstance(session_id, str) or SAFE_ID.fullmatch(session_id) is None:
        fail("INVALID_NATIVE_IDENTITY")
    if not isinstance(revision, int) or isinstance(revision, bool) or revision < 0:
        fail("INVALID_NATIVE_IDENTITY")
    if value["mode"] not in {"IDLE", "ACQUIRE", "RECORD", "UNKNOWN"}:
        fail("INVALID_NATIVE_IDENTITY")
    if value["gui_version"] != "1.0.2":
        fail("INVALID_NATIVE_IDENTITY")


def validate_preflight(value: object) -> None:
    if not isinstance(value, dict) or set(value) != {"pass", "checks"}:
        fail("INVALID_PREFLIGHT")
    if not isinstance(value["pass"], bool) or not isinstance(value["checks"], list):
        fail("INVALID_PREFLIGHT")
    for check in value["checks"]:
        if (
            not isinstance(check, dict)
            or set(check) != {"name", "pass"}
            or not isinstance(check["name"], str)
            or not check["name"]
            or not isinstance(check["pass"], bool)
        ):
            fail("INVALID_PREFLIGHT")
    if value["pass"] != all(check["pass"] for check in value["checks"]):
        fail("INVALID_PREFLIGHT")


def validate_report(report: object) -> None:
    if not isinstance(report, dict):
        fail("INVALID_REPORT")
    reject_secret_fields(report)
    unknown = set(report) - TOP_LEVEL_FIELDS
    if unknown:
        fail("UNKNOWN_TOP_LEVEL_FIELD")
    if set(report) != TOP_LEVEL_FIELDS:
        fail("MISSING_TOP_LEVEL_FIELD")
    if report["schema_version"] != "oe-agent-observation-report/v0.0.1":
        fail("SCHEMA_VERSION_MISMATCH")
    if report["status"] == "SUCCESS":
        fail("SUCCESS_NOT_SUPPORTED")
    if report["status"] not in {"OBSERVATION_ONLY", "BLOCKED", "INCIDENT"}:
        fail("INVALID_STATUS")
    validate_native(report["native"])
    validate_preflight(report["preflight"])
    shanks = report["shanks"]
    if (
        not isinstance(shanks, list)
        or any(not isinstance(item, int) or isinstance(item, bool) for item in shanks)
        or sorted(shanks) != list(range(1, 9))
    ):
        fail("INVALID_SHANKS")
    summary = report["summary"]
    if not isinstance(summary, str) or not summary or len(summary) > 2000:
        fail("INVALID_SUMMARY")


def require_exact_dict(value: object, fields: set[str], code: str) -> dict:
    if not isinstance(value, dict) or set(value) != fields:
        fail(code)
    return value


def require_positive_int(value: object, code: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        fail(code)
    return value


def validate_native_directory_name(value: object) -> str:
    if not isinstance(value, str) or not value or len(value) > 120:
        fail("INVALID_NATIVE_DIRECTORY_NAME")
    if AUTO_SUFFIX.search(value):
        fail("AUTO_SUFFIX_FORBIDDEN")
    if (
        "/" in value
        or "\\" in value
        or "." in value
        or value[-1].isspace()
        or any(ord(character) < 32 for character in value)
        or value.upper() in RESERVED_NAMES
    ):
        fail("INVALID_NATIVE_DIRECTORY_NAME")
    return value


def validate_run_manifest(value: object) -> None:
    manifest = require_exact_dict(
        value, RUN_TOP_LEVEL_FIELDS, "INVALID_RUN_MANIFEST_FIELDS"
    )
    for key in ("run_id", "subject_id", "operator", "protocol_id"):
        if not isinstance(manifest[key], str) or SAFE_ID.fullmatch(manifest[key]) is None:
            fail("INVALID_RUN_IDENTITY")
    try:
        created = datetime.fromisoformat(
            str(manifest["created_at_utc"]).replace("Z", "+00:00")
        )
    except ValueError:
        fail("INVALID_RUN_TIMESTAMP")
    if created.tzinfo is None:
        fail("INVALID_RUN_TIMESTAMP")
    for key, code in (
        ("experiment_root", "EXPERIMENT_ROOT_NOT_ABSOLUTE"),
        ("archive_root", "ARCHIVE_ROOT_NOT_ABSOLUTE"),
    ):
        path = PureWindowsPath(manifest[key]) if isinstance(manifest[key], str) else None
        if path is None or not path.is_absolute() or not path.drive:
            fail(code)

    hardware = require_exact_dict(
        manifest["hardware"],
        {
            "gui_version",
            "pxi_plugin_version",
            "imec_api_version",
            "onebox_serial",
            "probe_serial",
            "slot",
            "port",
            "dock",
            "reference",
        },
        "INVALID_HARDWARE_IDENTITY",
    )
    for key in (
        "gui_version",
        "pxi_plugin_version",
        "imec_api_version",
        "onebox_serial",
        "probe_serial",
        "reference",
    ):
        if not isinstance(hardware[key], str) or not hardware[key]:
            fail("INVALID_HARDWARE_IDENTITY")
    for key in ("slot", "port", "dock"):
        if not isinstance(hardware[key], int) or isinstance(hardware[key], bool):
            fail("INVALID_HARDWARE_IDENTITY")

    storage = require_exact_dict(
        manifest["storage"],
        {"minimum_free_bytes", "reserve_bytes", "poll_interval_seconds"},
        "INVALID_STORAGE_POLICY",
    )
    for key in storage:
        require_positive_int(storage[key], "INVALID_STORAGE_POLICY")

    streams = manifest["stream_expectations"]
    if not isinstance(streams, list) or not streams:
        fail("STREAM_EXPECTATIONS_REQUIRED")
    stream_names = []
    for stream in streams:
        item = require_exact_dict(
            stream,
            {
                "name",
                "channel_count",
                "sample_rate_hz",
                "recording_enabled",
                "events_required",
                "timestamps_required",
            },
            "INVALID_STREAM_EXPECTATION",
        )
        if not isinstance(item["name"], str) or not item["name"]:
            fail("INVALID_STREAM_EXPECTATION")
        require_positive_int(item["channel_count"], "INVALID_STREAM_EXPECTATION")
        if (
            not isinstance(item["sample_rate_hz"], (int, float))
            or isinstance(item["sample_rate_hz"], bool)
            or item["sample_rate_hz"] <= 0
            or item["recording_enabled"] is not True
            or not isinstance(item["events_required"], bool)
            or not isinstance(item["timestamps_required"], bool)
        ):
            fail("INVALID_STREAM_EXPECTATION")
        stream_names.append(item["name"].casefold())
    if len(stream_names) != len(set(stream_names)):
        fail("DUPLICATE_STREAM_EXPECTATION")

    qc = require_exact_dict(
        manifest["qc_policy"],
        {"criteria_id", "scientist_review_required"},
        "INVALID_QC_POLICY",
    )
    if (
        not isinstance(qc["criteria_id"], str)
        or not qc["criteria_id"]
        or qc["scientist_review_required"] is not True
    ):
        fail("INVALID_QC_POLICY")

    approvals = manifest["authorizations"]
    if not isinstance(approvals, list) or not approvals:
        fail("AUTHORIZATION_REQUIRED")
    for approval in approvals:
        item = require_exact_dict(
            approval,
            {"approval_id", "scope", "approver", "expires_at_utc"},
            "INVALID_AUTHORIZATION",
        )
        if (
            not isinstance(item["approval_id"], str)
            or not item["approval_id"]
            or item["scope"] != "SUPERVISED_COMPLETE_RUN"
            or not isinstance(item["approver"], str)
            or not item["approver"]
        ):
            fail("INVALID_AUTHORIZATION")
        try:
            expiry = datetime.fromisoformat(
                str(item["expires_at_utc"]).replace("Z", "+00:00")
            )
        except ValueError:
            fail("INVALID_AUTHORIZATION")
        if expiry.tzinfo is None or expiry <= created:
            fail("INVALID_AUTHORIZATION")

    parts = manifest["parts"]
    if not isinstance(parts, list) or len(parts) != 8:
        fail("PART_COUNT_NOT_EIGHT")
    observed = []
    directory_names = []
    for part in parts:
        item = require_exact_dict(
            part,
            {
                "part_index",
                "preset",
                "electrode_start",
                "electrode_end",
                "target_duration_seconds",
                "duration_tolerance_seconds",
                "maximum_overrun_seconds",
                "settling_seconds",
                "directory_name",
            },
            "INVALID_PART_FIELDS",
        )
        observed.append(
            (
                item["part_index"],
                item["preset"],
                item["electrode_start"],
                item["electrode_end"],
            )
        )
        if (
            not isinstance(item["target_duration_seconds"], int)
            or isinstance(item["target_duration_seconds"], bool)
            or item["target_duration_seconds"] <= 0
        ):
            fail("DURATION_NOT_CONCRETE")
        for key in (
            "duration_tolerance_seconds",
            "maximum_overrun_seconds",
            "settling_seconds",
        ):
            if (
                not isinstance(item[key], int)
                or isinstance(item[key], bool)
                or item[key] < 0
            ):
                fail("INVALID_PART_DURATION_POLICY")
        directory_names.append(
            validate_native_directory_name(item["directory_name"]).casefold()
        )
    if tuple(observed) != DEFAULT_PARTS:
        fail("PART_PLAN_NOT_ACTUAL_EIGHT_BLOCK_SEQUENCE")
    if len(directory_names) != len(set(directory_names)):
        fail("DUPLICATE_PART_DIRECTORY")


def validate_document(value: object) -> str:
    if not isinstance(value, dict):
        fail("INVALID_REPORT")
    reject_secret_fields(value)
    schema = value.get("schema_version")
    if schema == "oe-agent-run-manifest/v0.0.2":
        validate_run_manifest(value)
        return schema
    validate_report(value)
    return "oe-agent-observation-report/v0.0.1"


def main() -> int:
    if len(sys.argv) != 2:
        print(json.dumps({"error_code": "USAGE"}), file=sys.stderr)
        return 2
    try:
        raw = Path(sys.argv[1]).read_bytes()
        if len(raw) > 65_536:
            fail("REPORT_TOO_LARGE")
        report = json.loads(raw)
        schema = validate_document(report)
    except ManifestError as error:
        print(json.dumps({"error_code": error.code}), file=sys.stderr)
        return 1
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        print(json.dumps({"error_code": "INVALID_JSON"}), file=sys.stderr)
        return 1
    result = {"valid": True}
    if schema == "oe-agent-run-manifest/v0.0.2":
        result["schema_version"] = schema
    print(json.dumps(result, separators=(",", ":"), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
