import pytest

from open_ephys_agent_mcp.models import NativeStatus
from open_ephys_agent_mcp.native_client import NativeClientError
from open_ephys_agent_mcp.service import ObservationService


def status(**changes: object) -> NativeStatus:
    payload = {
        "schema_version": "oe-agent-control-preview/v0.0.1",
        "backend": "in-process-agent-endpoint",
        "session_id": "session-001",
        "online": True,
        "mutation_allowed": False,
        "mutation_disabled_reason": "NOT_ARMED",
        "phase": "READY",
        "gui_version": "1.0.2",
        "mode": "IDLE",
        "revision": 3,
    }
    payload.update(changes)
    return NativeStatus.model_validate(payload)


class StubClient:
    def __init__(
        self,
        response: NativeStatus | NativeClientError | None = None,
    ) -> None:
        self.response = response or status()

    def get_status(self) -> NativeStatus:
        if isinstance(self.response, NativeClientError):
            raise self.response
        return self.response


def test_identity_binds_component_versions_and_native_session() -> None:
    result = ObservationService(StubClient()).get_identity()

    assert result == {
        "ok": True,
        "product": "open-ephys-agent",
        "product_version": "0.0.1",
        "mcp_package_version": "0.0.1",
        "native_schema_version": "oe-agent-control-preview/v0.0.1",
        "gui_version": "1.0.2",
        "session_id": "session-001",
        "revision": 3,
        "observation_timestamp_available": False,
    }


def test_capabilities_are_an_exact_read_only_allowlist() -> None:
    result = ObservationService(StubClient()).get_capabilities()

    assert result["ok"] is True
    assert result["permission_level"] == "OBSERVE"
    assert result["mutation_available"] is False
    assert result["tools"] == [
        "oe_get_identity",
        "oe_get_capabilities",
        "oe_get_runtime_status",
        "oe_run_readonly_preflight",
    ]
    assert result["resources"] == [
        "open-ephys://identity",
        "open-ephys://capabilities",
        "open-ephys://status",
        "open-ephys://sop/8-shank",
    ]


def test_runtime_status_preserves_authoritative_fields() -> None:
    result = ObservationService(StubClient(status(mode="RECORD", revision=9))).get_status()

    assert result["ok"] is True
    assert result["mode"] == "RECORD"
    assert result["revision"] == 9
    assert result["session_id"] == "session-001"
    assert result["provenance"] == "NATIVE_FORK_ENDPOINT"


@pytest.mark.parametrize(
    ("changes", "failed_check"),
    [
        ({"online": False}, "online"),
        ({"phase": "DETACHED"}, "phase_ready"),
        ({"mode": "UNKNOWN"}, "mode_known"),
        ({"gui_version": "1.0.3"}, "gui_version_compatible"),
    ],
)
def test_preflight_fails_closed(
    changes: dict[str, object], failed_check: str
) -> None:
    result = ObservationService(StubClient(status(**changes))).run_readonly_preflight()

    assert result["ok"] is True
    assert result["pass"] is False
    checks = {check["name"]: check for check in result["checks"]}
    assert checks[failed_check]["pass"] is False
    assert result["session_id"] == "session-001"
    assert result["revision"] == 3


def test_native_client_error_becomes_machine_readable_failure() -> None:
    service = ObservationService(
        StubClient(NativeClientError("NATIVE_UNAVAILABLE", "offline"))
    )

    result = service.run_readonly_preflight()

    assert result == {
        "ok": False,
        "error_code": "NATIVE_UNAVAILABLE",
        "message": "Native endpoint is unavailable or invalid.",
    }
