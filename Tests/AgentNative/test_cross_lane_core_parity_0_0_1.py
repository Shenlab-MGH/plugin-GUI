"""Cross-lane 0.0.1 core parity: v1.1 integration vs v1.0.2 core machine contract.

Asserts product version 0.0.1 and that capability IDs/meanings, MCP tool
names/schemas, API routes/fields, and UIA automation IDs match the mature
v1.0.2 core contract modulo GUI baseline metadata (upstream version/commit).

This test is the TDD gate for align-v11-core-0.0.1. It must stay RED until
recording-options / disk / time / full UIA are already implemented and only
then exposed — never by inventing endpoints or falsifying the contract.
"""

from __future__ import annotations

import json
import os
import re
import unittest
from pathlib import Path
from typing import Any


V11_ROOT = Path(__file__).resolve().parents[2]
V102_ROOT = Path(
    os.environ.get(
        "OE_V102_CORE_ROOT",
        r"C:\Users\wangc\Documents\Cong\01-open-ephys-v102-core-r0-mcp-r010",
    )
)

PRODUCT_VERSION = "0.0.1"
MCP_PROTOCOL_VERSION = "2024-11-05"

# Active filenames must use _v0_0_1 (product) after the GUI version segment.
V11_CONTRACT_CANONICAL = V11_ROOT / "agent_native" / "open_ephys_agent_contract_v1_1_0_v0_0_1.json"
V11_CONTRACT_LEGACY_MISNAMED = (
    V11_ROOT / "agent_native" / "open_ephys_agent_contract_v1_1_0_0_0_1.json"
)
V102_CONTRACT = V102_ROOT / "agent_native" / "open_ephys_agent_contract_v1_0_2_v0_0_1.json"

BASELINE_KEYS = frozenset(
    {
        "upstream",
        "version",
        "commit",
        "upstream_release_commit",
        "fork_source_commit",
    }
)
# GUI baseline metadata may differ; product machine surface must not.
STRIP_TOP_LEVEL = frozenset({"baseline", "bundle", "verification", "transport"})


def _load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _active_v11_contract_path() -> Path:
    if V11_CONTRACT_CANONICAL.is_file():
        return V11_CONTRACT_CANONICAL
    if V11_CONTRACT_LEGACY_MISNAMED.is_file():
        return V11_CONTRACT_LEGACY_MISNAMED
    raise FileNotFoundError(
        f"missing v1.1 contract; expected {V11_CONTRACT_CANONICAL.name} "
        f"(or transitional {V11_CONTRACT_LEGACY_MISNAMED.name})"
    )


def _capability_ids(contract: dict[str, Any]) -> list[str]:
    caps = contract["api"]["expected_capabilities_response"]["capabilities"]
    return [item["id"] for item in caps]


def _capability_by_id(contract: dict[str, Any]) -> dict[str, dict[str, Any]]:
    caps = contract["api"]["expected_capabilities_response"]["capabilities"]
    return {item["id"]: item for item in caps}


def _tool_by_name(contract: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {item["name"]: item for item in contract["tools"]}


def _uia_id(cap: dict[str, Any]) -> str | None:
    uia = cap.get("uia")
    if uia is None:
        return None
    if isinstance(uia, dict):
        return uia.get("automation_id")
    return None


def _api_ops(cap: dict[str, Any]) -> list[dict[str, Any]]:
    return list(cap.get("api") or [])


def _normalize_tool_schema(schema: dict[str, Any]) -> dict[str, Any]:
    """Order-insensitive compare of JSON Schema objects used by MCP tools."""
    return json.loads(json.dumps(schema, sort_keys=True))


def _implementation_markers(root: Path) -> dict[str, bool]:
    http = (root / "Source" / "Utils" / "OpenEphysHttpServer.h").read_text(encoding="utf-8", errors="replace")
    routes = root / "Source" / "Utils" / "OpenEphysHttpApiRoutes.h"
    routes_text = routes.read_text(encoding="utf-8", errors="replace") if routes.is_file() else ""
    control_panel = (root / "Source" / "UI" / "ControlPanel.cpp").read_text(
        encoding="utf-8", errors="replace"
    )
    core_h = (root / "Source" / "CoreServices.h").read_text(encoding="utf-8", errors="replace")
    utils = root / "Source" / "Utils"
    return {
        "route_recording_options": (
            "/api/recording/options" in http or "kRecordingOptions" in routes_text
        ),
        "route_disk": "/api/disk" in http or "kDiskGet" in routes_text,
        "route_time": "/api/time" in http or "kTimeGet" in routes_text,
        "file_control_status": (utils / "ControlStatus.h").is_file(),
        "file_recording_options_control": (utils / "RecordingOptionsControl.h").is_file(),
        "core_disk": "getRecordingDiskUsage" in core_h,
        "core_clock": "getClockStatus" in core_h,
        "core_options": "getRecordingOptionsStatus" in core_h,
        "uia_acquisition": "oe.control.acquisition" in control_panel,
        "uia_recording": "oe.control.recording" in control_panel,
        "uia_recording_options": "oe.control.recording.options" in control_panel,
        "uia_filename": "oe.control.recording.filename" in control_panel,
        "uia_new_directory": "oe.control.recording.new_directory" in control_panel,
        "uia_force_new_directory": "oe.control.recording.force_new_directory" in control_panel,
        "uia_cpu": "oe.status.cpu_usage" in control_panel,
        "uia_disk": "oe.status.disk_usage" in control_panel,
        "uia_elapsed": "oe.status.elapsed_time" in control_panel,
    }


class CrossLaneCoreParity001Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.assertTrue = unittest.TestCase.assertTrue  # placate type checkers
        if not V102_ROOT.is_dir():
            raise unittest.SkipTest(f"v1.0.2 core worktree missing: {V102_ROOT}")
        if not V102_CONTRACT.is_file():
            raise unittest.SkipTest(f"v1.0.2 contract missing: {V102_CONTRACT}")

        cls.v11_path = _active_v11_contract_path()
        cls.v11 = _load(cls.v11_path)
        cls.v102 = _load(V102_CONTRACT)
        cls.v11_impl = _implementation_markers(V11_ROOT)
        cls.v102_impl = _implementation_markers(V102_ROOT)

    def test_active_v11_contract_filename_uses_v0_0_1_suffix(self) -> None:
        """Normalize to open_ephys_agent_contract_v1_1_0_v0_0_1.json (not ..._0_0_1)."""
        self.assertTrue(
            V11_CONTRACT_CANONICAL.is_file(),
            f"active contract must be named {V11_CONTRACT_CANONICAL.name}; "
            f"found transitional {self.v11_path.name}",
        )
        self.assertFalse(
            V11_CONTRACT_LEGACY_MISNAMED.exists(),
            f"misnamed contract must not remain: {V11_CONTRACT_LEGACY_MISNAMED.name}",
        )
        self.assertRegex(V11_CONTRACT_CANONICAL.name, r"_v0_0_1\.json$")
        self.assertRegex(V102_CONTRACT.name, r"_v0_0_1\.json$")

    def test_product_version_is_literal_0_0_1_on_both_lanes(self) -> None:
        for label, contract in (("v11", self.v11), ("v102", self.v102)):
            with self.subTest(lane=label):
                self.assertEqual(contract["schema_version"], PRODUCT_VERSION)
                self.assertEqual(contract["contract"]["version"], PRODUCT_VERSION)
                self.assertEqual(contract["api"]["capabilities_contract_version"], PRODUCT_VERSION)
                self.assertEqual(
                    contract["api"]["expected_capabilities_response"]["contract_version"],
                    PRODUCT_VERSION,
                )
                self.assertEqual(contract["mcp"]["protocol_version"], MCP_PROTOCOL_VERSION)

    def test_capability_ids_and_meanings_match_modulo_baseline(self) -> None:
        ids_v11 = _capability_ids(self.v11)
        ids_v102 = _capability_ids(self.v102)
        self.assertEqual(
            ids_v11,
            ids_v102,
            f"capability ID order/set mismatch; missing={sorted(set(ids_v102) - set(ids_v11))} "
            f"extra={sorted(set(ids_v11) - set(ids_v102))}",
        )

        by_v11 = _capability_by_id(self.v11)
        by_v102 = _capability_by_id(self.v102)
        meaning_keys = ("id", "name", "description", "kind")
        for cap_id in ids_v102:
            left = {k: by_v11[cap_id].get(k) for k in meaning_keys}
            right = {k: by_v102[cap_id].get(k) for k in meaning_keys}
            self.assertEqual(left, right, f"meaning mismatch for {cap_id}")

    def test_api_routes_and_fields_match(self) -> None:
        by_v11 = _capability_by_id(self.v11)
        by_v102 = _capability_by_id(self.v102)
        for cap_id, right in by_v102.items():
            self.assertIn(cap_id, by_v11, f"missing capability {cap_id}")
            left_ops = _api_ops(by_v11[cap_id])
            right_ops = _api_ops(right)
            self.assertEqual(
                len(left_ops),
                len(right_ops),
                f"api op count mismatch for {cap_id}",
            )
            for i, (lop, rop) in enumerate(zip(left_ops, right_ops)):
                for key in ("operation", "method", "path", "request_fields", "response_fields"):
                    self.assertEqual(
                        lop.get(key),
                        rop.get(key),
                        f"{cap_id} op[{i}].{key} mismatch",
                    )

    def test_uia_automation_ids_match(self) -> None:
        by_v11 = _capability_by_id(self.v11)
        by_v102 = _capability_by_id(self.v102)
        for cap_id, right in by_v102.items():
            self.assertIn(cap_id, by_v11, f"missing capability {cap_id}")
            self.assertEqual(
                _uia_id(by_v11[cap_id]),
                _uia_id(right),
                f"UIA automation_id mismatch for {cap_id}",
            )

    def test_mcp_tool_names_and_schemas_match(self) -> None:
        tools_v11 = _tool_by_name(self.v11)
        tools_v102 = _tool_by_name(self.v102)
        self.assertEqual(
            sorted(tools_v11),
            sorted(tools_v102),
            f"tool name set mismatch; missing={sorted(set(tools_v102) - set(tools_v11))} "
            f"extra={sorted(set(tools_v11) - set(tools_v102))}",
        )
        for name, right in tools_v102.items():
            left = tools_v11[name]
            self.assertEqual(
                _normalize_tool_schema(left.get("inputSchema") or {}),
                _normalize_tool_schema(right.get("inputSchema") or {}),
                f"inputSchema mismatch for {name}",
            )

    def test_required_implementations_present_before_contract_claims(self) -> None:
        """Hard gate: do not greenwash. Missing impl => fail with evidence."""
        required_impl = {
            "route_recording_options": True,
            "route_disk": True,
            "route_time": True,
            "file_control_status": True,
            "file_recording_options_control": True,
            "core_disk": True,
            "core_clock": True,
            "core_options": True,
            "uia_acquisition": True,
            "uia_recording": True,
            "uia_recording_options": True,
            "uia_filename": True,
            "uia_new_directory": True,
            "uia_force_new_directory": True,
            "uia_cpu": True,
            "uia_disk": True,
            "uia_elapsed": True,
        }
        # v102 lane must still hold the reference implementations.
        for key, expected in required_impl.items():
            self.assertEqual(
                self.v102_impl[key],
                expected,
                f"v1.0.2 reference lost {key}; cannot assert cross-lane parity",
            )

        missing = [key for key, expected in required_impl.items() if self.v11_impl[key] is not True]
        self.assertEqual(
            missing,
            [],
            "v1.1 HEAD lacks required core implementations (STOP BLOCKED — do not "
            f"invent endpoints or falsify contract): {missing}; markers={self.v11_impl}",
        )


if __name__ == "__main__":
    unittest.main()
