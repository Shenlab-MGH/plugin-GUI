"""Portability/privacy gate for cross-lane 0.0.1 parity.

Hosted CI must not depend on a private developer worktree path, must not skip
when OE_V102_CORE_ROOT is absent, and must pin a checked-in v1.0.2 reference
fixture with provenance + content hash while still detecting semantic drift.
"""

from __future__ import annotations

import ast
import hashlib
import json
import os
import re
import unittest
from pathlib import Path
from typing import Any
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
CROSS_LANE = ROOT / "Tests" / "AgentNative" / "test_cross_lane_core_parity_0_0_1.py"
FIXTURE = ROOT / "agent_native" / "fixtures" / "v102_core_0_0_1_cross_lane_reference.json"
V11_CONTRACT = ROOT / "agent_native" / "open_ephys_agent_contract_v1_1_0_v0_0_1.json"

EXPECTED_SOURCE_COMMIT = "54f982fd06c4d1fc1157ca74babb711f3c3dd239"
USER_PATH_RE = re.compile(
    r"(C:\\\\Users\\\\|C:/Users/|/home/|/Users/[A-Za-z]|wangc\\\\Documents|wangc/Documents)",
    re.IGNORECASE,
)


def _canonical_sha256(document: dict[str, Any]) -> str:
    payload = json.dumps(document, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode(
        "utf-8"
    )
    return hashlib.sha256(payload).hexdigest()


def _load_cross_lane_module():
    import importlib.util

    spec = importlib.util.spec_from_file_location("cross_lane_parity_under_test", CROSS_LANE)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    # Ensure hosted default does not inherit a developer OE_V102_CORE_ROOT.
    with mock.patch.dict(os.environ, {}, clear=False):
        os.environ.pop("OE_V102_CORE_ROOT", None)
        spec.loader.exec_module(module)
    return module


class CrossLanePortabilityTests(unittest.TestCase):
    def test_default_source_has_no_user_specific_absolute_path(self) -> None:
        source = CROSS_LANE.read_text(encoding="utf-8")
        self.assertIsNone(
            USER_PATH_RE.search(source),
            "cross-lane parity default must not embed a user-specific absolute path "
            f"(found match in {CROSS_LANE.name})",
        )
        self.assertNotIn("C:\\Users\\", source)
        self.assertNotIn("C:/Users/", source)

    def test_default_path_does_not_skip_when_oe_v102_core_root_absent(self) -> None:
        source = CROSS_LANE.read_text(encoding="utf-8")
        tree = ast.parse(source)
        skip_on_missing_worktree = False
        for node in ast.walk(tree):
            if isinstance(node, ast.Call):
                func = node.func
                name = ""
                if isinstance(func, ast.Attribute):
                    name = func.attr
                elif isinstance(func, ast.Name):
                    name = func.id
                if name == "SkipTest":
                    # Any SkipTest in this gate is a hosted-CI portability failure.
                    skip_on_missing_worktree = True
        self.assertFalse(
            skip_on_missing_worktree,
            "cross-lane parity must not SkipTest when the optional worktree is absent",
        )

        # Runtime: loading suite with env cleared must not skip setUpClass.
        env = {k: v for k, v in os.environ.items() if k != "OE_V102_CORE_ROOT"}
        with mock.patch.dict(os.environ, env, clear=True):
            module = _load_cross_lane_module()
            suite = unittest.defaultTestLoader.loadTestsFromTestCase(module.CrossLaneCoreParity001Tests)
            result = unittest.TestResult()
            suite.run(result)
            self.assertEqual(
                result.skipped,
                [],
                f"hosted default must not skip; skipped={result.skipped!r} errors={result.errors!r}",
            )
            self.assertGreater(result.testsRun, 0)

    def test_checked_in_reference_fixture_provenance_and_hash(self) -> None:
        self.assertTrue(FIXTURE.is_file(), f"missing portable fixture {FIXTURE.as_posix()}")
        fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))
        provenance = fixture["provenance"]
        self.assertEqual(provenance["source_commit"], EXPECTED_SOURCE_COMMIT)
        self.assertEqual(
            provenance["source_path"],
            "agent_native/open_ephys_agent_contract_v1_0_2_v0_0_1.json",
        )
        self.assertEqual(provenance["hash_algorithm"], "sha256")
        contract = fixture["contract"]
        actual_hash = _canonical_sha256(contract)
        self.assertEqual(
            provenance["content_sha256"],
            actual_hash,
            "fixture provenance content_sha256 must match the embedded contract",
        )
        # Non-empty machine surface so the fixture is not a vacuous placeholder.
        self.assertEqual(len(contract["api"]["expected_capabilities_response"]["capabilities"]), 9)
        self.assertEqual(len(contract["tools"]), 10)
        markers = fixture["implementation_markers"]
        self.assertTrue(all(markers.values()), markers)

    def test_cross_lane_still_detects_semantic_drift(self) -> None:
        """Parity remains non-tautological: a drifted v1.1 surface must fail."""
        module = _load_cross_lane_module()
        fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))
        v11 = json.loads(V11_CONTRACT.read_text(encoding="utf-8"))
        # Baseline: current v1.1 matches the reference set.
        self.assertEqual(
            module._capability_ids(v11),
            module._capability_ids(fixture["contract"]),
        )
        drifted = json.loads(json.dumps(v11))
        drifted["api"]["expected_capabilities_response"]["capabilities"] = drifted["api"][
            "expected_capabilities_response"
        ]["capabilities"][:-1]
        self.assertNotEqual(
            module._capability_ids(drifted),
            module._capability_ids(fixture["contract"]),
            "fixture comparison must remain sensitive to capability drift",
        )
        # Exercise the same assertion body the parity test uses.
        with self.assertRaises(AssertionError):
            self.assertEqual(
                module._capability_ids(drifted),
                module._capability_ids(fixture["contract"]),
            )


if __name__ == "__main__":
    unittest.main()
