#!/usr/bin/env python3
"""Validate a read-only Open Ephys Agent observation report."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import NoReturn


TOP_LEVEL_FIELDS = {
    "schema_version",
    "status",
    "native",
    "preflight",
    "shanks",
    "summary",
}
SECRET_KEY_PARTS = ("token", "authorization", "password", "secret", "bearer")
SAFE_ID = re.compile(r"^[A-Za-z0-9._:-]{1,128}$")


class ManifestError(ValueError):
    def __init__(self, code: str) -> None:
        super().__init__(code)
        self.code = code


def fail(code: str) -> NoReturn:
    raise ManifestError(code)


def reject_secret_fields(value: object) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            if any(part in str(key).casefold() for part in SECRET_KEY_PARTS):
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


def main() -> int:
    if len(sys.argv) != 2:
        print(json.dumps({"error_code": "USAGE"}), file=sys.stderr)
        return 2
    try:
        raw = Path(sys.argv[1]).read_bytes()
        if len(raw) > 65_536:
            fail("REPORT_TOO_LARGE")
        report = json.loads(raw)
        validate_report(report)
    except ManifestError as error:
        print(json.dumps({"error_code": error.code}), file=sys.stderr)
        return 1
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        print(json.dumps({"error_code": "INVALID_JSON"}), file=sys.stderr)
        return 1
    print(json.dumps({"valid": True}, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
