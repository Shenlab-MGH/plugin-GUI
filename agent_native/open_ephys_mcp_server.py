#!/usr/bin/env python3
"""MCP bridge for the Shenlab agent-native Open Ephys GUI fork.

The bridge speaks MCP over stdio and calls the GUI's local HTTP API on
127.0.0.1:37497. Agent-facing mutations are exposed as POST-style commands
through oe_post_command even when the compatibility GUI route is still PUT/GET.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent
DEFAULT_MANIFEST = ROOT / "open_ephys_agent_surface.json"
DEFAULT_BASE_URL = "http://127.0.0.1:37497"
LOOPBACK_HOSTS = {"127.0.0.1", "localhost", "::1"}
PARAMETER_AUTOMATION_ID_RULE = "oe.parameter.<sanitised parameter key>"
PARAMETER_RESPONSE_REQUIRED_FIELDS = [
    "name",
    "type",
    "value",
    "key",
    "display_name",
    "description",
    "enabled",
    "deactivate_during_acquisition",
    "uia.automation_id",
]


def load_manifest(path: Path = DEFAULT_MANIFEST) -> dict[str, Any]:
    manifest_path = Path(path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    validate_manifest_contract(manifest, manifest_path)
    return manifest


def validate_manifest_contract(manifest: dict[str, Any], manifest_path: Path) -> None:
    """Fail closed when the manifest and its versioned OE contract diverge."""

    contract = manifest.get("contract")
    if not isinstance(contract, dict):
        raise ValueError("Open Ephys agent contract metadata is missing.")

    for field in ("id", "version", "fixture"):
        if not isinstance(contract.get(field), str) or not contract[field]:
            raise ValueError(f"Open Ephys agent contract field is invalid: {field}.")

    fixture_ref = Path(contract["fixture"])
    if fixture_ref.is_absolute():
        raise ValueError("Open Ephys agent contract fixture must be relative to the manifest.")

    manifest_dir = Path(manifest_path).parent.resolve()
    fixture_path = (manifest_dir / fixture_ref).resolve()
    if not fixture_path.is_relative_to(manifest_dir):
        raise ValueError(
            "Open Ephys agent contract fixture must stay within the manifest directory."
        )
    if not fixture_path.is_file():
        raise ValueError(f"Open Ephys agent contract fixture is missing: {fixture_path}")

    try:
        fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(
            f"Open Ephys agent contract fixture is unreadable: {fixture_path}"
        ) from exc

    if not isinstance(fixture, dict):
        raise ValueError("Open Ephys agent contract fixture must be a JSON object.")
    if fixture.get("contract") != contract:
        raise ValueError("Open Ephys agent contract metadata does not match its fixture.")

    baseline = manifest.get("baseline")
    if not isinstance(baseline, dict) or any(
        not isinstance(baseline.get(field), str) or not baseline[field]
        for field in ("upstream", "version", "commit")
    ):
        raise ValueError("Open Ephys agent contract baseline metadata is invalid.")
    if fixture.get("baseline") != baseline:
        raise ValueError("Open Ephys agent contract baseline does not match the manifest.")

    manifest_transport = manifest.get("transport") or {}
    fixture_transport = fixture.get("transport") or {}
    if fixture_transport.get("http_base_url") != manifest_transport.get("http_base_url"):
        raise ValueError("Open Ephys agent contract HTTP transport does not match the manifest.")
    if (
        fixture_transport.get("uia") != "windows_uia"
        or manifest_transport.get("windows_uia") is not True
    ):
        raise ValueError("Open Ephys agent contract UIA transport is not Windows UIA.")

    manifest_uia = manifest.get("uia") or {}
    fixture_uia = fixture.get("uia") or {}
    if fixture_uia.get("root_id") != manifest_uia.get("root_id"):
        raise ValueError("Open Ephys agent contract UIA root does not match the manifest.")

    fixture_parameter_rule = fixture_uia.get("parameter_automation_id_rule")
    if fixture_parameter_rule != PARAMETER_AUTOMATION_ID_RULE:
        raise ValueError("Open Ephys agent contract parameter UIA rule is invalid.")
    if manifest_uia.get("parameter_automation_id_rule") != fixture_parameter_rule:
        raise ValueError("Open Ephys agent contract parameter UIA rule does not match the manifest.")

    parameter_response = (fixture.get("api") or {}).get("parameter_response")
    if not isinstance(parameter_response, dict) or (
        parameter_response.get("required_fields") != PARAMETER_RESPONSE_REQUIRED_FIELDS
    ):
        raise ValueError("Open Ephys agent contract parameter response is invalid.")
    if manifest.get("parameter_response") != parameter_response:
        raise ValueError("Open Ephys agent contract parameter response does not match the manifest.")

    required_ids = fixture_uia.get("required_ids")
    if not isinstance(required_ids, list) or any(
        not isinstance(entry, dict) or not isinstance(entry.get("id"), str)
        for entry in required_ids
    ):
        raise ValueError("Open Ephys agent contract UIA required_ids are invalid.")

    required_id_values = [entry["id"] for entry in required_ids]
    if len(required_id_values) != len(set(required_id_values)):
        raise ValueError("Open Ephys agent contract UIA required_ids are duplicated.")
    if fixture_uia.get("root_id") not in required_id_values:
        raise ValueError("Open Ephys agent contract UIA root is not declared in required_ids.")

    fixture_capability_ids = {
        entry_id
        for entry_id in required_id_values
        if entry_id != fixture_uia.get("root_id")
    }
    manifest_capability_ids = set(manifest_uia.get("capability_ids") or [])
    if fixture_capability_ids != manifest_capability_ids:
        raise ValueError("Open Ephys agent contract UIA capability IDs do not match the manifest.")


def command_index(manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {command["name"]: command for command in manifest["commands"]}


def command_capabilities(command: dict[str, Any]) -> list[str]:
    capabilities = command.get("capabilities")
    if capabilities is not None:
        return list(capabilities)

    capability = command.get("capability")
    return [capability] if capability else []


def validate_base_url(base_url: str) -> str:
    parsed = urllib.parse.urlparse(base_url)
    if parsed.scheme not in {"http", "https"} or parsed.hostname not in LOOPBACK_HOSTS:
        raise ValueError("Open Ephys MCP transport must use a loopback HTTP(S) URL.")
    return base_url


def validate_command_arguments(command: dict[str, Any], arguments: dict[str, Any]) -> None:
    required_fields = command["http"].get("required_body_fields", [])
    missing = [field for field in required_fields if field not in arguments]
    if missing:
        raise ValueError(f"Missing required field(s): {', '.join(missing)}")

    constraints = command.get("constraints", {})
    allowed_modes = constraints.get("mode")
    if allowed_modes and "mode" in arguments and arguments["mode"] not in allowed_modes:
        raise ValueError(f"Unsupported mode: {arguments['mode']}")

    if arguments.get("mode") == "RECORD":
        policy = constraints.get("record_policy", {})
        confirmation_argument = policy.get("confirmation_argument", "confirm_recording")
        if arguments.get(confirmation_argument) is not True:
            raise ValueError(
                "RECORD requires explicit confirmation in the current command arguments."
            )


def validate_raw_api_request(method: str, path: str, body: dict[str, Any] | None = None) -> None:
    if not path.startswith("/api/"):
        raise ValueError("Only /api/* paths are allowed.")

    normalized_method = method.upper()
    if normalized_method == "PUT" and path == "/api/quit":
        raise ValueError("Raw application quit is not available through the MCP escape hatch.")

    if path == "/api/status" and normalized_method == "PUT":
        if body and body.get("mode") == "RECORD" and body.get("confirm_recording") is not True:
            raise ValueError("Raw RECORD requests require confirm_recording=true.")


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
    validate_raw_api_request(method, path, body)
    validate_base_url(base_url)

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
    validate_command_arguments(command, arguments)
    path = render_path(command, arguments)
    result = call_http(
        command["http"]["method"],
        path,
        body_for(command, arguments),
        base_url=base_url,
        timeout=timeout,
    )
    result["command"] = name
    capabilities = command_capabilities(command)
    result["capabilities"] = capabilities
    result["capability"] = capabilities[0] if len(capabilities) == 1 else None
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
            "description": "Resolve an allowlisted capability id or API-returned parameter AutomationId.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "capability": {"type": "string"},
                    "automation_id": {"type": "string"},
                },
                "oneOf": [
                    {"required": ["capability"]},
                    {"required": ["automation_id"]},
                ],
            },
        },
    ]


def text_result(payload: Any) -> dict[str, Any]:
    return {"content": [{"type": "text", "text": json.dumps(payload, indent=2, sort_keys=True)}]}


class McpServer:
    def __init__(self, *, base_url: str = DEFAULT_BASE_URL, manifest_path: Path = DEFAULT_MANIFEST) -> None:
        self.base_url = validate_base_url(base_url)
        self.manifest = load_manifest(manifest_path)

    def handle(self, request: dict[str, Any]) -> dict[str, Any] | None:
        method = request.get("method")
        request_id = request.get("id")
        try:
            if method == "initialize":
                result = {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"tools": {}},
                    "serverInfo": {
                        "name": "open-ephys-agent-native",
                        "version": self.manifest["contract"]["version"],
                    },
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
            capability = arguments.get("capability")
            automation_id = arguments.get("automation_id")
            if capability is not None and automation_id is not None:
                raise ValueError("Provide either capability or automation_id, not both.")

            if capability is not None:
                allowed_capabilities = set(self.manifest["uia"]["capability_ids"])
                if capability not in allowed_capabilities:
                    raise ValueError("UIA capability is not allowlisted by the manifest.")
                automation_id = capability
            elif not self._is_parameter_automation_id(automation_id):
                raise ValueError("Parameter AutomationId must match the declared parameter UIA rule.")

            payload = {
                "automation_id": automation_id,
                "transport": "windows_uia",
                "rule": (
                    self.manifest["uia"]["automation_id_rule"]
                    if capability is not None
                    else self.manifest["uia"]["parameter_automation_id_rule"]
                ),
                "inspect_script": self.manifest["uia"]["script"],
            }
            if capability is not None:
                payload["capability"] = capability
            return text_result(
                payload
            )
        raise ValueError(f"Unknown tool: {name}")

    def _is_parameter_automation_id(self, automation_id: Any) -> bool:
        if not isinstance(automation_id, str):
            return False

        rule = self.manifest["uia"]["parameter_automation_id_rule"]
        prefix = rule.removesuffix("<sanitised parameter key>")
        return re.fullmatch(re.escape(prefix) + r"[a-z0-9]+(?:_[a-z0-9]+)*", automation_id) is not None


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
