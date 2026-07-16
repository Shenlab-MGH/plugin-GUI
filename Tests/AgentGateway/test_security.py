import unittest
from unittest.mock import patch
from pathlib import Path

from open_ephys_agent_gateway.security import (
    assess_native_api_exposure,
    is_simulated_config,
    validate_loopback_backend_url,
)


class SecurityTests(unittest.TestCase):
    def test_firewall_script_is_inspect_only_without_apply(self):
        root = Path(__file__).resolve().parents[2]
        script = (
            root
            / "tools"
            / "windows"
            / "Protect-OeNativeApi.ps1"
        ).read_text(encoding="utf-8")
        self.assertIn("[switch] $Apply", script)
        self.assertIn("if (-not $Apply)", script)
        self.assertIn("New-NetFirewallRule", script)
        self.assertIn("-Action Block", script)
        self.assertIn("-Profile Any", script)

    def test_simulated_config_detection_is_explicit(self):
        self.assertTrue(
            is_simulated_config(
                "<SETTINGS><SIGNALCHAIN>"
                '<PROCESSOR name="OneBox" processorType="2">'
                '<NP_PROBE bs_firmware_version="SIM 0.0" '
                'bs_part_number="Simulated BS"/>'
                "</PROCESSOR></SIGNALCHAIN></SETTINGS>"
            )
        )
        self.assertFalse(
            is_simulated_config(
                '<NP_PROBE bs_firmware_version="2.0"/>'
            )
        )
        self.assertFalse(
            is_simulated_config(
                "<SETTINGS><SIGNALCHAIN>"
                '<PROCESSOR name="Source Sim" processorType="2"/>'
                '<PROCESSOR name="OneBox" processorType="2">'
                '<NP_PROBE bs_firmware_version="2.0"/>'
                "</PROCESSOR></SIGNALCHAIN></SETTINGS>"
            )
        )

    def test_backend_url_must_be_loopback_http(self):
        self.assertEqual(
            validate_loopback_backend_url(
                "http://127.0.0.1:37497"
            ),
            37497,
        )
        with self.assertRaises(ValueError):
            validate_loopback_backend_url(
                "http://192.168.1.20:37497"
            )
        with self.assertRaises(ValueError):
            validate_loopback_backend_url(
                "https://127.0.0.1:37497"
            )

    @patch(
        "open_ephys_agent_gateway.security._powershell_json"
    )
    def test_wildcard_listener_and_public_allow_fail_closed(
        self,
        powershell_json,
    ):
        powershell_json.side_effect = [
            {
                "LocalAddress": "0.0.0.0",
                "LocalPort": 37497,
                "OwningProcess": 10,
                "OwnerPath": r"C:\Open Ephys\open-ephys.exe",
            },
            {
                "Enabled": True,
                "Direction": "Inbound",
                "Action": "Allow",
                "Profile": "Public",
            },
        ]

        result = assess_native_api_exposure(
            executable_path=r"C:\Open Ephys\open-ephys.exe"
        )

        self.assertFalse(result["safe_for_mutation"])
        self.assertIn(
            "NATIVE_API_WILDCARD_LISTENER",
            result["reasons"],
        )
        self.assertIn(
            "NATIVE_API_PUBLIC_FIREWALL_ALLOW",
            result["reasons"],
        )

    @patch(
        "open_ephys_agent_gateway.security._powershell_json"
    )
    def test_numeric_windows_firewall_enums_are_recognized(
        self,
        powershell_json,
    ):
        powershell_json.side_effect = [
            {
                "LocalAddress": "127.0.0.1",
                "LocalPort": 37497,
                "OwningProcess": 10,
                "OwnerPath": r"C:\Open Ephys\open-ephys.exe",
            },
            {
                "Enabled": 1,
                "Direction": 1,
                "Action": 2,
                "Profile": 4,
            },
        ]
        result = assess_native_api_exposure(
            executable_path=r"C:\Open Ephys\open-ephys.exe"
        )
        self.assertFalse(result["safe_for_mutation"])
        self.assertTrue(result["public_firewall_allow"])

    @patch(
        "open_ephys_agent_gateway.security._powershell_json",
        return_value=None,
    )
    def test_probe_failure_is_never_treated_as_safe(self, _):
        result = assess_native_api_exposure(
            executable_path=r"C:\Open Ephys\open-ephys.exe"
        )
        self.assertFalse(result["safe_for_mutation"])
        self.assertIn(
            "NATIVE_API_LISTENER_PROBE_FAILED",
            result["reasons"],
        )
        self.assertIn(
            "NATIVE_API_FIREWALL_PROBE_FAILED",
            result["reasons"],
        )


if __name__ == "__main__":
    unittest.main()
