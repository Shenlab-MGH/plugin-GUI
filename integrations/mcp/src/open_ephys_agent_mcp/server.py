"""Standard MCP stdio adapter with a fixed read-only allowlist."""

from __future__ import annotations

import json
import sys
from typing import Any

from mcp.server.fastmcp import FastMCP
from mcp.server.fastmcp.exceptions import ToolError
from mcp.types import ToolAnnotations

from .config import ConfigurationError, Settings
from .native_client import NativeClient
from .service import ObservationService


INSTRUCTIONS = (
    "Read-only Open Ephys Agent v0.0.1. Use identity, capabilities, runtime "
    "status, then read-only preflight. This server cannot start or stop "
    "acquisition or recording. Never bypass a failed check with shell, legacy "
    "port 37497, UIA invocation, pixels, or arbitrary HTTP. Stop and request "
    "human review on UNKNOWN, mismatch, unavailable evidence, or authentication "
    "failure. The native fork remains authoritative."
)


class StrictNoInputFastMCP(FastMCP):
    async def list_tools(self):
        tools = await super().list_tools()
        for tool in tools:
            tool.inputSchema["additionalProperties"] = False
        return tools

    async def call_tool(self, name: str, arguments: dict[str, Any]):
        if arguments:
            raise ToolError(f"Tool {name} accepts no input fields.")
        return await super().call_tool(name, arguments)


def create_server(service: ObservationService) -> FastMCP:
    server = StrictNoInputFastMCP(
        name="open-ephys-agent",
        instructions=INSTRUCTIONS,
        log_level="ERROR",
    )
    read_only = ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    )

    @server.tool(
        name="oe_get_identity",
        description="Read fork, protocol, GUI, session, and revision identity.",
        annotations=read_only,
        structured_output=True,
    )
    def get_identity() -> dict[str, object]:
        return {"result": service.get_identity()}

    @server.tool(
        name="oe_get_capabilities",
        description="Read the exact MCP tool/resource allowlist and permission level.",
        annotations=read_only,
        structured_output=True,
    )
    def get_capabilities() -> dict[str, object]:
        return {"result": service.get_capabilities()}

    @server.tool(
        name="oe_get_runtime_status",
        description="Read authoritative acquisition/recording mode and revision.",
        annotations=read_only,
        structured_output=True,
    )
    def get_runtime_status() -> dict[str, object]:
        return {"result": service.get_status()}

    @server.tool(
        name="oe_run_readonly_preflight",
        description="Run fail-closed compatibility and state checks without mutation.",
        annotations=read_only,
        structured_output=True,
    )
    def run_readonly_preflight() -> dict[str, object]:
        return {"result": service.run_readonly_preflight()}

    @server.resource(
        "open-ephys://identity",
        name="Open Ephys Agent identity",
        description="Current fork and process identity.",
        mime_type="application/json",
    )
    def identity_resource() -> str:
        return json.dumps(service.get_identity(), separators=(",", ":"))

    @server.resource(
        "open-ephys://capabilities",
        name="Open Ephys Agent capabilities",
        description="Exact read-only capability allowlist.",
        mime_type="application/json",
    )
    def capabilities_resource() -> str:
        return json.dumps(service.get_capabilities(), separators=(",", ":"))

    @server.resource(
        "open-ephys://status",
        name="Open Ephys Agent status",
        description="Current authoritative transport snapshot.",
        mime_type="application/json",
    )
    def status_resource() -> str:
        return json.dumps(service.get_status(), separators=(",", ":"))

    @server.resource(
        "open-ephys://sop/8-shank",
        name="Eight-shank observation SOP",
        description="Planning-only SOP boundary for the read-only v0.0.1 slice.",
        mime_type="application/json",
    )
    def eight_shank_resource() -> str:
        return json.dumps(
            {
                "ok": True,
                "version": "0.0.1",
                "mode": "OBSERVATION_AND_PLANNING_ONLY",
                "mutation_tools_available": False,
                "required_order": [
                    "oe_get_identity",
                    "oe_get_capabilities",
                    "oe_get_runtime_status",
                    "oe_run_readonly_preflight",
                ],
                "stop_conditions": [
                    "UNKNOWN",
                    "VERSION_MISMATCH",
                    "NATIVE_UNAVAILABLE",
                    "PREFLIGHT_FAILED",
                ],
            },
            separators=(",", ":"),
        )

    return server


def main() -> None:
    try:
        settings = Settings.from_environment()
    except ConfigurationError as error:
        print(f"open-ephys-agent-mcp startup failed: {error.code}", file=sys.stderr)
        raise SystemExit(2) from error

    create_server(ObservationService(NativeClient(settings))).run("stdio")
