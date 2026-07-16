import json
import tempfile
import unittest
from pathlib import Path

from open_ephys_agent_gateway.audit import JsonlAuditLog
from open_ephys_agent_gateway.models import Mode, RequestState
from open_ephys_agent_gateway.service import AgentGatewayService


class FakeOpenEphysClient:
    def __init__(self, modes=None):
        self.modes = list(modes or ["IDLE"])
        self.puts = []

    def get_mode(self):
        if len(self.modes) > 1:
            return self.modes.pop(0)
        return self.modes[0]

    def get_recording(self):
        return {
            "parent_directory": "D:/safe",
            "record_nodes": [
                {
                    "node_id": 101,
                    "is_synchronized": True,
                    "parent_directory": "D:/safe",
                }
            ],
        }

    def get_config_xml(self):
        return '<NP_PROBE bs_firmware_version="SIM 0.0"/>'

    def set_mode(self, mode):
        self.puts.append(mode)


class GatewayCoreTests(unittest.TestCase):
    def make_service(self, client=None, mutation_allowed=True):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        audit = JsonlAuditLog(Path(temporary.name) / "audit.jsonl")
        return AgentGatewayService(
            client or FakeOpenEphysClient(),
            audit,
            mutation_allowed=mutation_allowed,
            mutation_disabled_reason=(
                None if mutation_allowed else "NATIVE_API_EXPOSED"
            ),
        ), audit

    def test_revision_changes_only_when_observed_mode_changes(self):
        service, _ = self.make_service(
            FakeOpenEphysClient(["IDLE", "IDLE", "ACQUIRE"])
        )

        first = service.status()
        second = service.status()
        third = service.status()

        self.assertEqual(first["revision"], 1)
        self.assertEqual(second["revision"], 1)
        self.assertEqual(third["revision"], 2)
        self.assertEqual(third["mode"], "ACQUIRE")
        self.assertEqual(first["session_id"], third["session_id"])

    def test_request_id_has_a_strict_size_limit(self):
        service, _ = self.make_service()
        status = service.status()
        result = service.submit(
            {
                "request_id": "x" * 129,
                "target_mode": "IDLE",
                "expected_revision": status["revision"],
            }
        )
        self.assertEqual(result["state"], "REJECTED")
        self.assertEqual(result["reason"], "INVALID_REQUEST")

    def test_exposure_gate_rejects_mutation_without_backend_put(self):
        client = FakeOpenEphysClient()
        service, _ = self.make_service(
            client,
            mutation_allowed=False,
        )
        status = service.status()

        receipt = service.submit(
            {
                "request_id": "blocked-1",
                "target_mode": "ACQUIRE",
                "expected_revision": status["revision"],
            }
        )

        self.assertEqual(receipt["state"], "REJECTED")
        self.assertEqual(receipt["reason"], "NATIVE_API_EXPOSED")
        self.assertEqual(client.puts, [])

    def test_stale_revision_is_rejected(self):
        service, _ = self.make_service()
        service.status()

        receipt = service.submit(
            {
                "request_id": "stale-1",
                "target_mode": "ACQUIRE",
                "expected_revision": 0,
            }
        )

        self.assertEqual(receipt["state"], "REJECTED")
        self.assertEqual(receipt["reason"], "STALE_REVISION")

    def test_duplicate_id_is_idempotent_and_conflict_is_rejected(self):
        service, _ = self.make_service()
        status = service.status()
        request = {
            "request_id": "same-1",
            "target_mode": "IDLE",
            "expected_revision": status["revision"],
        }

        first = service.submit(request)
        duplicate = service.submit(request)
        conflict = service.submit(
            {**request, "target_mode": "ACQUIRE"}
        )

        self.assertEqual(first["state"], "COMPLETED")
        self.assertEqual(duplicate, first)
        self.assertEqual(conflict["state"], "REJECTED")
        self.assertEqual(conflict["reason"], "ID_CONFLICT")

    def test_record_to_idle_verifies_intermediate_acquire(self):
        client = FakeOpenEphysClient(
            [
                "RECORD",
                "RECORD",
                "RECORD",
                "ACQUIRE",
                "ACQUIRE",
                "IDLE",
            ]
        )
        service, _ = self.make_service(client)
        status = service.status()

        result = service.submit(
            {
                "request_id": "stop-all-1",
                "target_mode": "IDLE",
                "expected_revision": status["revision"],
            }
        )

        self.assertEqual(result["state"], "COMPLETED")
        self.assertEqual(client.puts, ["ACQUIRE", "IDLE"])
        self.assertEqual(result["final_mode"], "IDLE")

    def test_record_preflight_requires_nodes_sync_and_writable_directory(self):
        client = FakeOpenEphysClient(["IDLE"])
        client.get_recording = lambda: {
            "parent_directory": "Z:/missing",
            "record_nodes": [],
        }
        service, _ = self.make_service(client)
        status = service.status()

        result = service.submit(
            {
                "request_id": "record-1",
                "target_mode": "RECORD",
                "expected_revision": status["revision"],
            }
        )

        self.assertEqual(result["state"], "REJECTED")
        self.assertEqual(result["reason"], "RECORD_PREFLIGHT_FAILED")
        self.assertEqual(client.puts, [])

    def test_lost_put_response_uses_readback_without_retry(self):
        class LostResponseClient(FakeOpenEphysClient):
            def __init__(self):
                super().__init__(
                    ["IDLE", "IDLE", "IDLE", "ACQUIRE"]
                )

            def set_mode(self, mode):
                self.puts.append(mode)
                raise TimeoutError("response lost")

        client = LostResponseClient()
        service, _ = self.make_service(client)
        status = service.status()

        result = service.submit(
            {
                "request_id": "lost-response-1",
                "target_mode": "ACQUIRE",
                "expected_revision": status["revision"],
            }
        )

        self.assertEqual(result["state"], "COMPLETED")
        self.assertEqual(client.puts, ["ACQUIRE"])
        self.assertTrue(result["steps"][0]["transport_error"])

    def test_lost_put_response_with_wrong_readback_fails_without_retry(self):
        class FailedClient(FakeOpenEphysClient):
            def __init__(self):
                super().__init__(["IDLE"])

            def set_mode(self, mode):
                self.puts.append(mode)
                raise TimeoutError("response lost")

        client = FailedClient()
        service, _ = self.make_service(client)
        status = service.status()

        result = service.submit(
            {
                "request_id": "lost-response-2",
                "target_mode": "ACQUIRE",
                "expected_revision": status["revision"],
            }
        )

        self.assertEqual(result["state"], "REJECTED")
        self.assertEqual(result["reason"], "READBACK_MISMATCH")
        self.assertEqual(client.puts, ["ACQUIRE"])

    def test_human_state_change_before_commit_aborts_without_put(self):
        client = FakeOpenEphysClient(
            ["IDLE", "IDLE", "RECORD"]
        )
        service, _ = self.make_service(client)
        status = service.status()

        result = service.submit(
            {
                "request_id": "takeover-1",
                "target_mode": "ACQUIRE",
                "expected_revision": status["revision"],
            }
        )

        self.assertEqual(result["state"], "REJECTED")
        self.assertEqual(result["reason"], "PRECONDITION_CHANGED")
        self.assertEqual(client.puts, [])

    def test_dynamic_safety_is_rechecked_before_mutation(self):
        checks = iter(
            [
                (True, None, {"gate": "initial"}),
                (False, "SAFETY_CHANGED", {"gate": "commit"}),
            ]
        )
        client = FakeOpenEphysClient(["IDLE", "IDLE"])
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        service = AgentGatewayService(
            client,
            JsonlAuditLog(Path(temporary.name) / "audit.jsonl"),
            mutation_allowed=True,
            mutation_disabled_reason=None,
            safety_check=lambda: next(checks),
        )
        status = service.status()

        result = service.submit(
            {
                "request_id": "safety-change-1",
                "target_mode": "ACQUIRE",
                "expected_revision": status["revision"],
            }
        )

        self.assertEqual(result["state"], "REJECTED")
        self.assertEqual(result["reason"], "SAFETY_CHANGED")
        self.assertEqual(client.puts, [])

    def test_malformed_unhashable_request_returns_structured_rejection(self):
        service, _ = self.make_service()
        result = service.submit(
            {
                "request_id": [],
                "target_mode": {},
                "expected_revision": False,
            }
        )
        self.assertEqual(result["state"], "REJECTED")
        self.assertEqual(result["reason"], "INVALID_REQUEST")

    def test_post_mutation_audit_failure_reaches_terminal_fault(self):
        class FailingAudit:
            def __init__(self):
                self.failed = False

            def append(self, event, payload):
                if event == "transport_step_readback":
                    self.failed = True
                    raise OSError("disk full")

        client = FakeOpenEphysClient(
            ["IDLE", "IDLE", "IDLE", "ACQUIRE"]
        )
        service = AgentGatewayService(
            client,
            FailingAudit(),
            mutation_allowed=True,
            mutation_disabled_reason=None,
        )
        status = service.status()
        result = service.submit(
            {
                "request_id": "audit-failure-1",
                "target_mode": "ACQUIRE",
                "expected_revision": status["revision"],
            }
        )

        self.assertEqual(result["state"], "REJECTED")
        self.assertEqual(result["reason"], "AUDIT_WRITE_FAILED")
        self.assertEqual(client.puts, ["ACQUIRE"])
        follow_up = service.submit(
            {
                "request_id": "audit-failure-2",
                "target_mode": "IDLE",
                "expected_revision": result["final_revision"],
            }
        )
        self.assertEqual(follow_up["reason"], "AUDIT_UNAVAILABLE")
        self.assertEqual(client.puts, ["ACQUIRE"])

    def test_audit_log_does_not_store_bearer_token(self):
        service, audit = self.make_service()
        service.status()
        service.submit(
            {
                "request_id": "audit-1",
                "target_mode": "IDLE",
                "expected_revision": 1,
                "authorization": "Bearer secret-value",
            }
        )

        content = audit.path.read_text(encoding="utf-8")
        self.assertNotIn("secret-value", content)
        records = [
            json.loads(line)
            for line in content.splitlines()
            if line.strip()
        ]
        self.assertTrue(records)


if __name__ == "__main__":
    unittest.main()
