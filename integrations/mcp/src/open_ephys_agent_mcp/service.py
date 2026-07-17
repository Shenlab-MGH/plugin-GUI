"""Vendor-neutral read-only observation service."""

from __future__ import annotations

from typing import Protocol

from . import __version__
from .models import NativeStatus
from .native_client import NativeClientError


PRODUCT_VERSION = "0.0.1"
NATIVE_SCHEMA_VERSION = "oe-agent-control-preview/v0.0.1"
COMPATIBLE_GUI_VERSION = "1.0.2"
TOOLS = [
    "oe_get_identity",
    "oe_get_capabilities",
    "oe_get_runtime_status",
    "oe_run_readonly_preflight",
]
RESOURCES = [
    "open-ephys://identity",
    "open-ephys://capabilities",
    "open-ephys://status",
    "open-ephys://sop/8-shank",
]


class StatusClient(Protocol):
    def get_status(self) -> NativeStatus: ...


class ObservationService:
    def __init__(self, client: StatusClient) -> None:
        self._client = client

    @staticmethod
    def _failure(error: NativeClientError) -> dict[str, object]:
        return {
            "ok": False,
            "error_code": error.code,
            "message": "Native endpoint is unavailable or invalid.",
        }

    def _status_or_failure(self) -> NativeStatus | dict[str, object]:
        try:
            return self._client.get_status()
        except NativeClientError as error:
            return self._failure(error)

    def get_identity(self) -> dict[str, object]:
        status = self._status_or_failure()
        if isinstance(status, dict):
            return status
        return {
            "ok": True,
            "product": "open-ephys-agent",
            "product_version": PRODUCT_VERSION,
            "mcp_package_version": __version__,
            "native_schema_version": status.schema_version,
            "gui_version": status.gui_version,
            "session_id": status.session_id,
            "revision": status.revision,
            "observation_timestamp_available": False,
        }

    def get_capabilities(self) -> dict[str, object]:
        status = self._status_or_failure()
        if isinstance(status, dict):
            return status
        return {
            "ok": True,
            "product_version": PRODUCT_VERSION,
            "mcp_package_version": __version__,
            "native_schema_version": status.schema_version,
            "session_id": status.session_id,
            "revision": status.revision,
            "permission_level": "OBSERVE",
            "mutation_available": False,
            "tools": list(TOOLS),
            "resources": list(RESOURCES),
            "observation_timestamp_available": False,
        }

    def get_status(self) -> dict[str, object]:
        status = self._status_or_failure()
        if isinstance(status, dict):
            return status
        return {
            "ok": True,
            **status.model_dump(mode="json"),
            "provenance": "NATIVE_FORK_ENDPOINT",
            "observation_timestamp_available": False,
        }

    def run_readonly_preflight(self) -> dict[str, object]:
        status = self._status_or_failure()
        if isinstance(status, dict):
            return status

        checks = [
            {
                "name": "schema_compatible",
                "pass": status.schema_version == NATIVE_SCHEMA_VERSION,
                "observed": status.schema_version,
                "expected": NATIVE_SCHEMA_VERSION,
            },
            {
                "name": "gui_version_compatible",
                "pass": status.gui_version == COMPATIBLE_GUI_VERSION,
                "observed": status.gui_version,
                "expected": COMPATIBLE_GUI_VERSION,
            },
            {
                "name": "online",
                "pass": status.online,
                "observed": status.online,
                "expected": True,
            },
            {
                "name": "phase_ready",
                "pass": status.phase == "READY",
                "observed": status.phase,
                "expected": "READY",
            },
            {
                "name": "mode_known",
                "pass": status.mode != "UNKNOWN",
                "observed": status.mode,
                "expected": "IDLE|ACQUIRE|RECORD",
            },
        ]
        return {
            "ok": True,
            "pass": all(bool(check["pass"]) for check in checks),
            "permission_level": "OBSERVE",
            "mutation_available": False,
            "session_id": status.session_id,
            "revision": status.revision,
            "mode": status.mode,
            "checks": checks,
            "observation_timestamp_available": False,
        }
