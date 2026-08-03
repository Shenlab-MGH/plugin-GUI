import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / "agent_native" / "open_ephys_core_integration_parity_0_0_1.json"
CONTRACT = ROOT / "agent_native" / "open_ephys_agent_contract_v1_1_0_v0_0_1.json"


class CoreIntegrationParityTests(unittest.TestCase):
    def test_report_matches_existing_core_contract_without_new_oe_features(self):
        self.assertTrue(REPORT.exists(), "integration parity report must be generated")
        report = json.loads(REPORT.read_text(encoding="utf-8"))
        contract = json.loads(CONTRACT.read_text(encoding="utf-8"))

        self.assertEqual(report["schema_version"], "0.0.1")
        self.assertEqual(report["baseline"], {
            "version": "1.1.0",
            "upstream_release_commit": "c2ce076f5b2d4182222f9d0fd9bb9b97a60582e7",
        })
        self.assertFalse(report["new_open_ephys_functionality"])

        capabilities = {item["id"]: item for item in report["capabilities"]}
        contract_ids = {
            item["id"]
            for item in contract["api"]["expected_capabilities_response"]["capabilities"]
        }
        self.assertEqual(set(capabilities), contract_ids)
        self.assertEqual(capabilities["oe.control.acquisition"]["parity"], "BOTH")
        self.assertEqual(capabilities["oe.control.acquisition"]["uia"], {
            "platform": "windows",
            "automation_id": "oe.control.acquisition",
        })
        self.assertEqual(capabilities["oe.control.recording"]["parity"], "BOTH")
        self.assertEqual(capabilities["oe.control.recording"]["uia"], {
            "platform": "windows",
            "automation_id": "oe.control.recording",
        })
        self.assertEqual(capabilities["oe.control.recording.options"]["parity"], "BOTH")
        self.assertEqual(capabilities["oe.control.recording.options"]["uia"], {
            "platform": "windows",
            "automation_id": "oe.control.recording.options",
        })
        self.assertEqual(capabilities["oe.control.recording.filename"]["parity"], "BOTH")
        self.assertEqual(capabilities["oe.control.recording.filename"]["uia"], {
            "platform": "windows",
            "automation_id": "oe.control.recording.filename",
        })
        self.assertEqual(capabilities["oe.control.recording.new_directory"]["parity"], "BOTH")
        self.assertEqual(capabilities["oe.control.recording.force_new_directory"]["parity"], "BOTH")
        self.assertEqual(capabilities["oe.status.cpu_usage"]["parity"], "BOTH")
        self.assertEqual(capabilities["oe.status.cpu_usage"]["uia"], {
            "platform": "windows",
            "automation_id": "oe.status.cpu_usage",
        })
        self.assertEqual(capabilities["oe.status.disk_usage"]["parity"], "BOTH")
        self.assertEqual(capabilities["oe.status.elapsed_time"]["parity"], "BOTH")

    def test_report_keeps_remote_and_hardware_gates_pending(self):
        self.assertTrue(REPORT.exists(), "integration parity report must be generated")
        report = json.loads(REPORT.read_text(encoding="utf-8"))
        self.assertEqual(report["verification"], {
            "windows_uia_runtime": {
                "status": "passed",
                "focused_rounds": 10,
                "tests_per_round": 4,
            },
            "official_mcp_v2_local_interop": "passed",
            "official_mcp_v2_exact_commit": "pending",
            "hardware": "not_verified",
        })


if __name__ == "__main__":
    unittest.main()
