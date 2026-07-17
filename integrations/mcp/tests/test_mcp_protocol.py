import json

import pytest
from mcp.shared.memory import create_connected_server_and_client_session

from open_ephys_agent_mcp.models import NativeStatus
from open_ephys_agent_mcp.server import create_server
from open_ephys_agent_mcp.service import ObservationService, RESOURCES, TOOLS


class StaticClient:
    def get_status(self) -> NativeStatus:
        return NativeStatus.model_validate(
            {
                "schema_version": "oe-agent-control-preview/v0.0.1",
                "backend": "in-process-agent-endpoint",
                "session_id": "protocol-session",
                "online": True,
                "mutation_allowed": False,
                "mutation_disabled_reason": "NOT_ARMED",
                "phase": "READY",
                "gui_version": "1.0.2",
                "mode": "IDLE",
                "revision": 11,
            }
        )


@pytest.mark.asyncio
async def test_lists_exact_tools_and_resources_with_strict_schemas() -> None:
    server = create_server(ObservationService(StaticClient()))

    async with create_connected_server_and_client_session(server) as session:
        tools = (await session.list_tools()).tools
        resources = (await session.list_resources()).resources

    assert [tool.name for tool in tools] == TOOLS
    assert [str(resource.uri) for resource in resources] == RESOURCES
    for tool in tools:
        assert tool.inputSchema["type"] == "object"
        assert tool.inputSchema.get("properties", {}) == {}
        assert tool.inputSchema.get("additionalProperties") is False


@pytest.mark.asyncio
async def test_invokes_every_tool_and_reads_every_resource() -> None:
    server = create_server(ObservationService(StaticClient()))

    async with create_connected_server_and_client_session(server) as session:
        for name in TOOLS:
            result = await session.call_tool(name, arguments={})
            assert result.isError is not True
            assert result.structuredContent is not None
            assert result.structuredContent["result"]["ok"] is True

        for uri in RESOURCES:
            result = await session.read_resource(uri)
            payload = json.loads(result.contents[0].text)
            assert payload["ok"] is True


@pytest.mark.asyncio
async def test_rejects_unknown_tool_and_unknown_input_field() -> None:
    server = create_server(ObservationService(StaticClient()))

    async with create_connected_server_and_client_session(
        server, raise_exceptions=False
    ) as session:
        unknown_tool = await session.call_tool("oe_start_recording", arguments={})
        unknown_field = await session.call_tool(
            "oe_get_runtime_status", arguments={"unsafe": True}
        )

    assert unknown_tool.isError is True
    assert unknown_field.isError is True
