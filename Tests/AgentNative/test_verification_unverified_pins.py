"""RED gate: product contract and release bundle must pin unverified claims.

hardware_verified and scientific_verified must both be present and exactly false.
MCP load_contract must require the same.
"""

from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AGENT_DIR = ROOT / "agent_native"
CONTRACT_PATH = AGENT_DIR / "open_ephys_agent_contract_v1_1_0_v0_0_3.json"
BUNDLE_PATH = AGENT_DIR / "open_ephys_agent_release_bundle.json"
SERVER_PATH = AGENT_DIR / "open_ephys_mcp_server.py"


def load_server_module():
    spec = importlib.util.spec_from_file_location("open_ephys_mcp_server_verify_pins", SERVER_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class VerificationUnverifiedPinTests(unittest.TestCase):
    def test_product_contract_pins_hardware_and_scientific_false(self) -> None:
        contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        self.assertIn("verification", contract)
        self.assertEqual(
            contract["verification"],
            {"hardware_verified": False, "scientific_verified": False},
        )
        self.assertIs(contract["verification"]["hardware_verified"], False)
        self.assertIs(contract["verification"]["scientific_verified"], False)

    def test_release_bundle_pins_hardware_and_scientific_false(self) -> None:
        bundle = json.loads(BUNDLE_PATH.read_text(encoding="utf-8"))
        self.assertIn("verification", bundle)
        self.assertIn("hardware_verified", bundle["verification"])
        self.assertIn("scientific_verified", bundle["verification"])
        self.assertIs(bundle["verification"]["hardware_verified"], False)
        self.assertIs(bundle["verification"]["scientific_verified"], False)

    def test_mcp_loader_requires_both_verification_flags_exactly_false(self) -> None:
        module = load_server_module()
        base = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))

        # Ensure a contract missing verification is rejected once pins are required.
        missing = dict(base)
        missing.pop("verification", None)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "contract.json"
            path.write_text(json.dumps(missing), encoding="utf-8")
            with self.assertRaises(ValueError) as ctx:
                module.load_contract(path)
            self.assertIn("verification", str(ctx.exception).lower())

        for broken in (
            {"hardware_verified": True, "scientific_verified": False},
            {"hardware_verified": False, "scientific_verified": True},
            {"hardware_verified": False},
            {"scientific_verified": False},
            {},
        ):
            with self.subTest(verification=broken):
                candidate = dict(base)
                candidate["verification"] = broken
                with tempfile.TemporaryDirectory() as tmp:
                    path = Path(tmp) / "contract.json"
                    path.write_text(json.dumps(candidate), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        module.load_contract(path)


if __name__ == "__main__":
    unittest.main()
