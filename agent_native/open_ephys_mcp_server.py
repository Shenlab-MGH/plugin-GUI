#!/usr/bin/env python3
"""MCP bridge for the Shenlab agent-native Open Ephys GUI fork.

The bridge speaks MCP over stdio and calls the GUI's local HTTP API on
127.0.0.1:37497. Agent-facing mutations are exposed as POST-style commands
through oe_post_command even when the compatibility GUI route is still PUT/GET.
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent
DEFAULT_MANIFEST = ROOT / "open_ephys_agent_surface.json"
DEFAULT_BASE_URL = "http://127.0.0.1:37497"


def load_manifest(path: Path = DEFAULT_MANIFEST) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def command_index(manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {command["name"]: command for command in manifest["commands"]}


def render_path(command: dict[str, Any], arguments: dict[str, Any]) -> str:
    http = command["http"]
    template = http.get("path_template", http.get("path"))
    if not template:
        raise ValueError(f"Command {command['name']} has no HTTP path.")

    result = template
    for field in http.get("path_fields", []):
        if field not in arguments:
            raise ValueError(f"Missing path field: {field}")
        result = result.replace("{" + field + "}", urllib.parse.quote(str(arguments[field]), safe=""))
    return result


def body_for(command: dict[str, Any], arguments: dict[str, Any]) -> dict[str, Any] | None:
    fields = command["http"].get("body_fields", [])
    body = {field: arguments[field] for field in fields if field in arguments}
    return body if fields else None


def call_http(
    method: str,
    path: str,
    body: dict[str, Any] | None = None,
    *,
    base_url: str = DEFAULT_BASE_URL,
    timeout: float = 5.0,
) -> dict[str, Any]:
    if not path.startswith("/api/"):
        raise ValueError("Only /api/* paths are allowed.")

    url = base_url.rstrip("/") + path
    data = None
    headers = {"Accept": "application/json"}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"

    request = urllib.request.Request(url, data=data, method=method.upper(), headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            payload = response.read().decode("utf-8")
            parsed = json.loads(payload) if payload else None
            return {
                "ok": 200 <= response.status < 300,
                "status": response.status,
                "method": method.upper(),
                "path": path,
                "response": parsed,
            }
    except urllib.error.HTTPError as exc:
        payload = exc.read().decode("utf-8", errors="replace")
        try:
            parsed: Any = json.loads(payload) if payload else None
        except json.JSONDecodeError:
            parsed = payload
        return {
            "ok": False,
            "status": exc.code,
            "method": method.upper(),
            "path": path,
            "response": parsed,
        }
    except urllib.error.URLError as exc:
        return {
            "ok": False,
            "status": None,
            "method": method.upper(),
            "path": path,
            "error": {
                "code": "connection_failed",
                "message": str(exc.reason),
            },
        }


def execute_command(
    name: str,
    arguments: dict[str, Any] | None = None,
    *,
    manifest: dict[str, Any] | None = None,
    base_url: str = DEFAULT_BASE_URL,
    timeout: float = 5.0,
) -> dict[str, Any]:
    manifest = manifest or load_manifest()
    commands = command_index(manifest)
    if name not in commands:
        raise ValueError(f"Unknown command: {name}")

    arguments = arguments or {}
    command = commands[name]
    path = render_path(command, arguments)
    result = call_http(
        command["http"]["method"],
        path,
        body_for(command, arguments),
        base_url=base_url,
        timeout=timeout,
    )
    result["command"] = name
    result["capability"] = command["capability"]
    result["operation"] = command["operation"]
    result["agent_command_style"] = "POST" if command.get("post_command") else command["http"]["method"]
    result["readback"] = command.get("readback")
    return result


def mcp_tool_list(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    enum = sorted(command_index(manifest))
    return [
        {
            "name": "oe_list_commands",
            "description": "List agent-native Open Ephys commands with API/UIA metadata.",
            "inputSchema": {"type": "object", "properties": {}},
        },
        {
            "name": "oe_post_command",
            "description": "Run one named Open Ephys command through the local API using the manifest.",
            "inputSchema": {
                "type": "object",
                "required": ["command"],
                "properties": {
                    "command": {"type": "string", "enum": enum},
                    "arguments": {"type": "object", "additionalProperties": True},
                },
            },
        },
        {
            "name": "oe_api_request",
            "description": "Call a raw /api/* route on the local Open Ephys GUI.",
            "inputSchema": {
                "type": "object",
                "required": ["method", "path"],
                "properties": {
                    "method": {"type": "string", "enum": ["GET", "POST", "PUT", "DELETE"]},
                    "path": {"type": "string"},
                    "body": {"type": "object", "additionalProperties": True},
                },
            },
        },
        {
            "name": "oe_uia_locator",
            "description": "Resolve a capability id to its Windows UIA AutomationId contract.",
            "inputSchema": {
                "type": "object",
                "required": ["capability"],
                "properties": {"capability": {"type": "string"}},
            },
        },
    ]


def text_result(payload: Any) -> dict[str, Any]:
    return {"content": [{"type": "text", "text": json.dumps(payload, indent=2, sort_keys=True)}]}


class McpServer:
    def __init__(self, *, base_url: str = DEFAULT_BASE_URL, manifest_path: Path = DEFAULT_MANIFEST) -> None:
        self.base_url = base_url
        self.manifest = load_manifest(manifest_path)

    def handle(self, request: dict[str, Any]) -> dict[str, Any] | None:
        method = request.get("method")
        request_id = request.get("id")
        try:
            if method == "initialize":
                result = {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "open-ephys-agent-native", "version": "0.1.0"},
                }
            elif method == "tools/list":
                result = {"tools": mcp_tool_list(self.manifest)}
            elif method == "tools/call":
                result = self._call_tool(request.get("params", {}))
            elif method == "notifications/initialized":
                return None
            else:
                raise ValueError(f"Unsupported MCP method: {method}")
            return {"jsonrpc": "2.0", "id": request_id, "result": result}
        except Exception as exc:  # MCP servers should return structured failures.
            return {
                "jsonrpc": "2.0",
                "id": request_id,
                "error": {"code": -32000, "message": str(exc)},
            }

    def _call_tool(self, params: dict[str, Any]) -> dict[str, Any]:
        name = params.get("name")
        arguments = params.get("arguments") or {}
        if name == "oe_list_commands":
            return text_result(self.manifest)
        if name == "oe_post_command":
            return text_result(
                execute_command(
                    arguments["command"],
                    arguments.get("arguments") or {},
                    manifest=self.manifest,
                    base_url=self.base_url,
                )
            )
        if name == "oe_api_request":
            return text_result(
                call_http(
                    arguments["method"],
                    arguments["path"],
                    arguments.get("body"),
                    base_url=self.base_url,
                )
            )
        if name == "oe_uia_locator":
            capability = arguments["capability"]
            return text_result(
                {
                    "capability": capability,
                    "automation_id": capability,
                    "transport": "windows_uia",
                    "rule": self.manifest["uia"]["automation_id_rule"],
                    "inspect_script": self.manifest["uia"]["script"],
                }
            )
        raise ValueError(f"Unknown tool: {name}")


def run_stdio(server: McpServer) -> None:
    for line in sys.stdin:
        if not line.strip():
            continue
        response = server.handle(json.loads(line))
        if response is not None:
            sys.stdout.write(json.dumps(response, separators=(",", ":")) + "\n")
            sys.stdout.flush()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--list-commands", action="store_true")
    parser.add_argument("--command")
    parser.add_argument("--arguments", default="{}")
    args = parser.parse_args(argv)

    manifest = load_manifest(args.manifest)
    if args.list_commands:
        print(json.dumps(manifest, indent=2, sort_keys=True))
        return 0
    if args.command:
        payload = json.loads(args.arguments)
        print(json.dumps(execute_command(args.command, payload, manifest=manifest, base_url=args.base_url), indent=2))
        return 0

    run_stdio(McpServer(base_url=args.base_url, manifest_path=args.manifest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
