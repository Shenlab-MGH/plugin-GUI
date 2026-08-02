#!/usr/bin/env python3
"""MCP bridge for the Shenlab agent-native Open Ephys GUI fork.

The bridge speaks MCP over stdio and calls the GUI's local HTTP API on
127.0.0.1:37497. Agent-facing mutations are exposed as POST-style commands
through oe_post_command even when the compatibility GUI route is still PUT/GET.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent
DEFAULT_MANIFEST = ROOT / "open_ephys_agent_surface.json"
DEFAULT_BASE_URL = "http://127.0.0.1:37497"
LOOPBACK_HOSTS = {"127.0.0.1", "localhost", "::1"}
SUPPORTED_SCHEMA_VERSION = "0.1.3"
SUPPORTED_CONTRACT_ID = "open-ephys-agent"
SUPPORTED_CONTRACT_VERSION = "0.1.3"
MCP_PROTOCOL_VERSION = "2024-11-05"
SERVER_NOT_INITIALIZED = -32002
PARAMETER_AUTOMATION_ID_RULE = "oe.parameter.<sanitised parameter key>"
PROCESSOR_CATALOG_AUTOMATION_ID_RULE = (
    "oe.processor_catalog.<sanitised processor slug>"
)
STREAM_ROW_AUTOMATION_ID_RULE = (
    "oe.processor.<processor_id>.streams.table.source_<source_id>."
    "stream_<sanitised identifier-or-display-name>"
    "[.index_<nonnegative stream_index on collision>]"
)
STREAM_ROW_AUTOMATION_ID_PATTERN = re.compile(
    r"oe\.processor\.(?P<processor_id>[0-9]+)\.streams\.table\."
    r"source_(?P<source_id>[0-9]+)\."
    r"stream_(?P<semantic_segment>[a-z0-9]+(?:_[a-z0-9]+)*)"
    r"(?:\.index_(?P<collision_index>[0-9]+))?"
)
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
PARAMETER_RESPONSE_CONTRACT = {
    "required_fields": PARAMETER_RESPONSE_REQUIRED_FIELDS,
    "route_lookup_field": "name",
    "stable_identity_field": "key",
}
STREAM_RESPONSE_CONTRACT = {
    "required_fields": [
        "name",
        "source_id",
        "sample_rate",
        "channel_count",
        "parameters",
        "stream_index",
        "runtime_id",
        "source_name",
        "description",
        "identifier",
        "generates_timestamps",
        "identity.available",
        "identity.scope",
        "identity.source_id",
        "identity.identifier",
        "uia.automation_id",
        "uia.scope",
        "uia.uses_display_name_fallback",
        "uia.uses_collision_suffix",
    ],
    "route_lookup_field": "stream_index",
    "identity": {
        "fields": ["source_id", "identifier"],
        "available_when": "identifier_nonempty",
        "scope": "configuration",
    },
    "non_durable_fields": {
        "stream_index": "current_configuration_order",
        "runtime_id": "process_lifetime",
    },
}
TYPED_STREAM_TOOLS = {
    "oe_list_streams": {
        "backend_command": "get_processor",
        "readback": "response.streams",
    },
    "oe_get_stream": {
        "backend_command": "get_stream",
        "readback": "response",
    },
}
PARAMETER_NAME_SEGMENT_POLICY = {
    "field": "parameter_name",
    "source": "parameter_response.name",
    "input": "raw",
    "render": "percent_encode_utf8_once",
    "reject": ["empty", "slash", "backslash", "dot_segment", "control_character"],
}


def load_manifest(path: Path = DEFAULT_MANIFEST) -> dict[str, Any]:
    manifest_path = Path(path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    validate_manifest_contract(manifest, manifest_path)
    return manifest


def validate_manifest_contract(manifest: dict[str, Any], manifest_path: Path) -> None:
    """Fail closed when the manifest and its versioned OE contract diverge."""

    if manifest.get("schema_version") != SUPPORTED_SCHEMA_VERSION:
        raise ValueError(
            f"Open Ephys agent schema is not supported: {manifest.get('schema_version')!r}."
        )

    contract = manifest.get("contract")
    if not isinstance(contract, dict):
        raise ValueError("Open Ephys agent contract metadata is missing.")

    for field in ("id", "version", "fixture"):
        if not isinstance(contract.get(field), str) or not contract[field]:
            raise ValueError(f"Open Ephys agent contract field is invalid: {field}.")

    if (
        contract["id"] != SUPPORTED_CONTRACT_ID
        or contract["version"] != SUPPORTED_CONTRACT_VERSION
    ):
        raise ValueError(
            "Open Ephys agent manifest does not declare the supported contract "
            f"{SUPPORTED_CONTRACT_ID} {SUPPORTED_CONTRACT_VERSION}."
        )

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
    if fixture.get("schema_version") != SUPPORTED_SCHEMA_VERSION:
        raise ValueError("Open Ephys agent contract fixture schema is not supported.")
    if fixture.get("contract") != contract:
        raise ValueError("Open Ephys agent contract metadata does not match its fixture.")
    if (
        fixture["contract"].get("id") != SUPPORTED_CONTRACT_ID
        or fixture["contract"].get("version") != SUPPORTED_CONTRACT_VERSION
    ):
        raise ValueError("Open Ephys agent fixture does not declare the supported contract.")

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

    fixture_processor_catalog_rule = fixture_uia.get("processor_catalog_automation_id_rule")
    if fixture_processor_catalog_rule != PROCESSOR_CATALOG_AUTOMATION_ID_RULE:
        raise ValueError("Open Ephys agent contract processor catalog UIA rule is invalid.")
    if manifest_uia.get("processor_catalog_automation_id_rule") != fixture_processor_catalog_rule:
        raise ValueError(
            "Open Ephys agent contract processor catalog UIA rule does not match the manifest."
        )

    parameter_response = (fixture.get("api") or {}).get("parameter_response")
    if parameter_response != PARAMETER_RESPONSE_CONTRACT:
        raise ValueError("Open Ephys agent contract parameter response is invalid.")
    if manifest.get("parameter_response") != parameter_response:
        raise ValueError("Open Ephys agent contract parameter response does not match the manifest.")

    fixture_parameter_name_segment_policy = (fixture.get("api") or {}).get(
        "parameter_name_segment_policy"
    )
    if fixture_parameter_name_segment_policy != PARAMETER_NAME_SEGMENT_POLICY:
        raise ValueError("Open Ephys agent contract parameter name segment policy is not canonical.")
    if manifest.get("parameter_name_segment_policy") != fixture_parameter_name_segment_policy:
        raise ValueError(
            "Open Ephys agent contract parameter name segment policy does not match the manifest."
        )

    stream_response = (fixture.get("api") or {}).get("stream_response")
    if stream_response != STREAM_RESPONSE_CONTRACT:
        raise ValueError("Open Ephys agent contract stream response is invalid.")
    if manifest.get("stream_response") != stream_response:
        raise ValueError("Open Ephys agent contract stream response does not match the manifest.")

    typed_tools = (fixture.get("api") or {}).get("typed_tools")
    if typed_tools != TYPED_STREAM_TOOLS:
        raise ValueError("Open Ephys agent contract typed stream tools are invalid.")
    if manifest.get("typed_tools") != typed_tools:
        raise ValueError("Open Ephys agent typed stream tools do not match the manifest.")

    fixture_stream_rule = fixture_uia.get("stream_row_automation_id_rule")
    if fixture_stream_rule != STREAM_ROW_AUTOMATION_ID_RULE:
        raise ValueError("Open Ephys agent contract stream UIA rule is invalid.")
    if manifest_uia.get("stream_row_automation_id_rule") != fixture_stream_rule:
        raise ValueError("Open Ephys agent contract stream UIA rule does not match the manifest.")

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


def render_parameter_name_segment(value: Any) -> str:
    """Validate a raw parameter name before encoding it as one URL path segment."""

    if not isinstance(value, str) or not value:
        raise ValueError("Parameter name must be a non-empty raw path segment.")
    if "/" in value:
        raise ValueError("Parameter name must not contain a slash.")
    if "\\" in value:
        raise ValueError("Parameter name must not contain a backslash.")
    if value in {".", ".."}:
        raise ValueError("Parameter name must not be a dot segment.")
    if any(unicodedata.category(character) == "Cc" for character in value):
        raise ValueError("Parameter name must not contain control characters.")
    return urllib.parse.quote(value, safe="")


def render_path(command: dict[str, Any], arguments: dict[str, Any]) -> str:
    http = command["http"]
    template = http.get("path_template", http.get("path"))
    if not template:
        raise ValueError(f"Command {command['name']} has no HTTP path.")

    result = template
    for field in http.get("path_fields", []):
        if field not in arguments:
            raise ValueError(f"Missing path field: {field}")
        if field == "parameter_name":
            value = render_parameter_name_segment(arguments[field])
        else:
            value = urllib.parse.quote(str(arguments[field]), safe="")
        result = result.replace("{" + field + "}", value)
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


def typed_stream_tool_schema(name: str) -> dict[str, Any]:
    properties = {"processor_id": {"type": "integer", "minimum": 0}}
    required = ["processor_id"]
    if name == "oe_get_stream":
        properties["stream_index"] = {"type": "integer", "minimum": 0}
        required.append("stream_index")
    return {
        "type": "object",
        "required": required,
        "properties": properties,
        "additionalProperties": False,
    }


def validate_typed_stream_arguments(name: str, arguments: dict[str, Any]) -> None:
    if not isinstance(arguments, dict):
        raise ValueError("Typed stream tool arguments must be an object.")
    schema = typed_stream_tool_schema(name)
    allowed = set(schema["properties"])
    unexpected = set(arguments) - allowed
    if unexpected:
        raise ValueError(f"Unexpected typed stream argument(s): {', '.join(sorted(unexpected))}")
    for field in schema["required"]:
        value = arguments.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError(f"{field} must be a non-negative integer.")


def _has_dotted_field(payload: dict[str, Any], dotted_field: str) -> bool:
    current: Any = payload
    for field in dotted_field.split("."):
        if not isinstance(current, dict) or field not in current:
            return False
        current = current[field]
    return True


def _is_nonnegative_integer(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def sanitise_stream_semantic_segment(value: str) -> str:
    """Mirror C++ sanitiseSemanticSegment's ASCII-only behavior exactly."""

    result: list[str] = []
    for character in value:
        if "A" <= character <= "Z":
            character = character.lower()
        if "a" <= character <= "z" or "0" <= character <= "9":
            result.append(character)
        elif result and result[-1] != "_":
            result.append("_")
    while result and result[-1] == "_":
        result.pop()
    return "".join(result) or "unnamed"


def parse_stream_row_automation_id(automation_id: Any) -> dict[str, Any] | None:
    if not isinstance(automation_id, str):
        return None
    match = STREAM_ROW_AUTOMATION_ID_PATTERN.fullmatch(automation_id)
    if match is None:
        return None
    collision_index = match.group("collision_index")
    return {
        "processor_id": int(match.group("processor_id")),
        "source_id": int(match.group("source_id")),
        "semantic_segment": match.group("semantic_segment"),
        "collision_index": int(collision_index) if collision_index is not None else None,
    }


def validate_stream_response(
    stream: Any,
    *,
    processor_id: int,
    expected_stream_index: int,
) -> dict[str, Any]:
    if not isinstance(stream, dict):
        raise ValueError("Open Ephys stream response must be an object.")
    missing = [
        field
        for field in STREAM_RESPONSE_CONTRACT["required_fields"]
        if not _has_dotted_field(stream, field)
    ]
    if missing:
        raise ValueError(f"Open Ephys stream response is missing field(s): {', '.join(missing)}")

    for field in ("name", "source_name", "description", "identifier"):
        if not isinstance(stream[field], str):
            raise ValueError(f"Open Ephys stream field {field} must be a string.")
    for field in ("source_id", "channel_count", "stream_index", "runtime_id"):
        if not _is_nonnegative_integer(stream[field]):
            raise ValueError(f"Open Ephys stream field {field} must be a non-negative integer.")
    sample_rate = stream["sample_rate"]
    if (
        isinstance(sample_rate, bool)
        or not isinstance(sample_rate, (int, float))
        or not math.isfinite(sample_rate)
        or sample_rate <= 0
    ):
        raise ValueError("Open Ephys stream field sample_rate must be a positive number.")
    if not isinstance(stream["parameters"], list):
        raise ValueError("Open Ephys stream field parameters must be an array.")
    if not isinstance(stream["generates_timestamps"], bool):
        raise ValueError("Open Ephys stream field generates_timestamps must be a boolean.")
    if stream["stream_index"] != expected_stream_index:
        raise ValueError("Open Ephys stream_index does not match the requested current order.")

    identity = stream["identity"]
    if not isinstance(identity["available"], bool):
        raise ValueError("Open Ephys stream identity.available must be a boolean.")
    if identity["scope"] != "configuration":
        raise ValueError("Open Ephys stream identity scope must be configuration.")
    if not _is_nonnegative_integer(identity["source_id"]):
        raise ValueError("Open Ephys stream identity.source_id must be a non-negative integer.")
    if not isinstance(identity["identifier"], str):
        raise ValueError("Open Ephys stream identity.identifier must be a string.")
    if identity["source_id"] != stream["source_id"]:
        raise ValueError("Open Ephys stream identity source_id does not match the stream.")
    if identity["identifier"] != stream["identifier"]:
        raise ValueError("Open Ephys stream identity identifier does not match the stream.")
    if identity["available"] != bool(stream["identifier"]):
        raise ValueError("Open Ephys stream identity availability does not match its identifier.")

    uia = stream["uia"]
    if uia["scope"] != "configuration":
        raise ValueError("Open Ephys stream UIA scope must be configuration.")
    if not isinstance(uia["uses_display_name_fallback"], bool):
        raise ValueError("Open Ephys stream UIA fallback flag must be a boolean.")
    if uia["uses_display_name_fallback"] != (stream["identifier"] == ""):
        raise ValueError("Open Ephys stream UIA fallback does not match its identifier.")
    if not isinstance(uia["uses_collision_suffix"], bool):
        raise ValueError("Open Ephys stream UIA collision suffix flag must be a boolean.")
    parsed_locator = parse_stream_row_automation_id(uia["automation_id"])
    if parsed_locator is None:
        raise ValueError("Open Ephys stream UIA AutomationId is invalid.")
    if parsed_locator["processor_id"] != processor_id:
        raise ValueError("Open Ephys stream UIA processor id does not match the request.")
    if parsed_locator["source_id"] != stream["source_id"]:
        raise ValueError("Open Ephys stream UIA source id does not match the stream.")
    identity_segment = stream["identifier"] if stream["identifier"] else stream["name"]
    expected_semantic_segment = sanitise_stream_semantic_segment(identity_segment)
    if parsed_locator["semantic_segment"] != expected_semantic_segment:
        raise ValueError("Open Ephys stream UIA semantic segment does not match stream metadata.")
    has_collision_suffix = parsed_locator["collision_index"] is not None
    if uia["uses_collision_suffix"] != has_collision_suffix:
        raise ValueError("Open Ephys stream UIA collision suffix flag does not match its locator.")
    if (
        parsed_locator["collision_index"] is not None
        and parsed_locator["collision_index"] != stream["stream_index"]
    ):
        raise ValueError("Open Ephys stream UIA collision index does not match stream_index.")
    parsed_locator["uses_collision_suffix"] = uia["uses_collision_suffix"]
    parsed_locator["metadata_collision_key"] = (
        processor_id,
        stream["source_id"],
        expected_semantic_segment,
    )
    return parsed_locator


def validate_stream_list_locators(parsed_locators: list[dict[str, Any]]) -> None:
    base_counts: dict[tuple[int, int, str], int] = {}
    for locator in parsed_locators:
        key = locator["metadata_collision_key"]
        base_counts[key] = base_counts.get(key, 0) + 1
    for locator in parsed_locators:
        key = locator["metadata_collision_key"]
        has_collision = base_counts[key] > 1
        if has_collision != locator["uses_collision_suffix"]:
            raise ValueError("Open Ephys stream UIA collision suffix does not match sibling locators.")


def execute_typed_stream_tool(
    name: str,
    arguments: dict[str, Any],
    *,
    manifest: dict[str, Any],
    base_url: str,
) -> dict[str, Any]:
    validate_typed_stream_arguments(name, arguments)
    declaration = manifest["typed_tools"][name]
    result = execute_command(
        declaration["backend_command"],
        arguments,
        manifest=manifest,
        base_url=base_url,
    )
    result["tool"] = name
    result["backend_command"] = declaration["backend_command"]
    result["readback"] = declaration["readback"]
    if result.get("ok") is not True:
        return result

    if name == "oe_list_streams":
        response = result.get("response")
        if not isinstance(response, dict) or not isinstance(response.get("streams"), list):
            raise ValueError("Open Ephys processor response must contain a streams array.")
        if not _is_nonnegative_integer(response.get("id")):
            raise ValueError("Open Ephys processor response id must be a non-negative integer.")
        if response["id"] != arguments["processor_id"]:
            raise ValueError("Open Ephys processor response id does not match the request.")
        parsed_locators = [
            validate_stream_response(
                stream,
                processor_id=arguments["processor_id"],
                expected_stream_index=stream_index,
            )
            for stream_index, stream in enumerate(response["streams"])
        ]
        validate_stream_list_locators(parsed_locators)
    else:
        validate_stream_response(
            result.get("response"),
            processor_id=arguments["processor_id"],
            expected_stream_index=arguments["stream_index"],
        )
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
        {
            "name": "oe_list_streams",
            "description": "List streams for one processor with configuration-scoped identity and UIA metadata.",
            "inputSchema": typed_stream_tool_schema("oe_list_streams"),
        },
        {
            "name": "oe_get_stream",
            "description": "Read one stream by its current configuration order index.",
            "inputSchema": typed_stream_tool_schema("oe_get_stream"),
        },
    ]


def text_result(payload: Any, *, is_error: bool = False) -> dict[str, Any]:
    result = {
        "content": [
            {
                "type": "text",
                "text": json.dumps(payload, indent=2, sort_keys=True, allow_nan=False),
            }
        ]
    }
    if is_error:
        result["isError"] = True
    return result


class JsonRpcError(Exception):
    def __init__(self, code: int, message: str, data: Any = None) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.data = data


class InvalidParamsError(JsonRpcError):
    def __init__(self, message: str = "Invalid params", data: Any = None) -> None:
        super().__init__(-32602, message, data)


class McpServer:
    def __init__(self, *, base_url: str = DEFAULT_BASE_URL, manifest_path: Path = DEFAULT_MANIFEST) -> None:
        self.base_url = validate_base_url(base_url)
        self.manifest = load_manifest(manifest_path)
        self.connection_state = "new"

    def handle(self, request: Any) -> dict[str, Any] | None:
        request_id = self._request_id_for_error(request)
        try:
            self._validate_envelope(request)
            method = request["method"]
            is_notification = "id" not in request

            if method not in {"initialize", "tools/list", "tools/call", "notifications/initialized"}:
                if is_notification:
                    return None
                raise JsonRpcError(-32601, "Method not found")

            if method == "notifications/initialized":
                if not is_notification:
                    raise JsonRpcError(-32600, "Invalid Request")
                if not self._params_are_an_object(request):
                    return None
                if self.connection_state == "initialize_responded":
                    self.connection_state = "ready"
                return None

            if is_notification:
                return None

            self._validate_known_method_params(method, request)
            if method == "initialize":
                result = self._initialize(request["params"])
            elif method == "tools/list":
                self._require_ready()
                result = {"tools": mcp_tool_list(self.manifest)}
            elif method == "tools/call":
                self._require_ready()
                self._validate_tool_call_params(request["params"])
                result = self._call_tool(request["params"])
            return {"jsonrpc": "2.0", "id": request_id, "result": result}
        except JsonRpcError as exc:
            return self._error_response(request_id, exc)
        except Exception:
            return {
                "jsonrpc": "2.0",
                "id": request_id,
                "error": {"code": -32603, "message": "Internal error"},
            }

    @staticmethod
    def _request_id_for_error(request: Any) -> str | int | float | None:
        if not isinstance(request, dict) or "id" not in request:
            return None
        request_id = request["id"]
        if (
            isinstance(request_id, bool)
            or not isinstance(request_id, (str, int, float, type(None)))
            or (isinstance(request_id, float) and not math.isfinite(request_id))
        ):
            return None
        return request_id

    @staticmethod
    def _error_response(request_id: str | int | float | None, error: JsonRpcError) -> dict[str, Any]:
        payload: dict[str, Any] = {"code": error.code, "message": error.message}
        if error.data is not None:
            payload["data"] = error.data
        return {"jsonrpc": "2.0", "id": request_id, "error": payload}

    @staticmethod
    def _params_are_an_object(request: dict[str, Any]) -> bool:
        return "params" not in request or isinstance(request["params"], dict)

    def _validate_envelope(self, request: Any) -> None:
        if not isinstance(request, dict):
            raise JsonRpcError(-32600, "Invalid Request")
        if request.get("jsonrpc") != "2.0" or not isinstance(request.get("method"), str):
            raise JsonRpcError(-32600, "Invalid Request")
        if "id" in request and (
            isinstance(request["id"], bool)
            or not isinstance(request["id"], (str, int, float, type(None)))
            or (isinstance(request["id"], float) and not math.isfinite(request["id"]))
        ):
            raise JsonRpcError(-32600, "Invalid Request")

    def _validate_known_method_params(self, method: str, request: dict[str, Any]) -> None:
        if not self._params_are_an_object(request):
            raise InvalidParamsError()
        if method == "tools/call" and not isinstance(request.get("params"), dict):
            raise InvalidParamsError()
        if method == "initialize":
            params = request.get("params")
            if not isinstance(params, dict):
                raise InvalidParamsError()
            protocol_version = params.get("protocolVersion")
            capabilities = params.get("capabilities")
            client_info = params.get("clientInfo")
            if (
                not isinstance(protocol_version, str)
                or not isinstance(capabilities, dict)
                or not isinstance(client_info, dict)
                or not isinstance(client_info.get("name"), str)
                or not isinstance(client_info.get("version"), str)
            ):
                raise InvalidParamsError()

    def _initialize(self, params: dict[str, Any]) -> dict[str, Any]:
        if self.connection_state != "new":
            raise JsonRpcError(SERVER_NOT_INITIALIZED, "Invalid lifecycle order")
        requested_version = params["protocolVersion"]
        if requested_version != MCP_PROTOCOL_VERSION:
            raise InvalidParamsError(
                "Invalid params",
                {"supported": MCP_PROTOCOL_VERSION, "requested": requested_version},
            )
        self.connection_state = "initialize_responded"
        return {
            "protocolVersion": MCP_PROTOCOL_VERSION,
            "capabilities": {"tools": {}},
            "serverInfo": {
                "name": "open-ephys-agent-native",
                "version": self.manifest["contract"]["version"],
            },
        }

    def _require_ready(self) -> None:
        if self.connection_state != "ready":
            raise JsonRpcError(SERVER_NOT_INITIALIZED, "Server not initialized")

    def _validate_tool_call_params(self, params: dict[str, Any]) -> None:
        name = params.get("name")
        arguments = params.get("arguments", {})
        known_tools = {
            "oe_list_commands",
            "oe_post_command",
            "oe_api_request",
            "oe_uia_locator",
            *self.manifest["typed_tools"],
        }
        if not isinstance(name, str) or name not in known_tools or not isinstance(arguments, dict):
            raise InvalidParamsError()
        if name == "oe_post_command":
            self._validate_post_command_arguments(arguments)
        elif name == "oe_api_request":
            self._validate_raw_api_arguments(arguments)
        elif name in self.manifest["typed_tools"]:
            try:
                validate_typed_stream_arguments(name, arguments)
            except ValueError as exc:
                raise InvalidParamsError(str(exc)) from exc
        elif name == "oe_uia_locator":
            self._validate_uia_locator_arguments(arguments)

    def _validate_post_command_arguments(self, arguments: dict[str, Any]) -> None:
        name = arguments.get("command")
        command_arguments = arguments.get("arguments", {})
        if not isinstance(name, str) or not isinstance(command_arguments, dict):
            raise InvalidParamsError()
        command = command_index(self.manifest).get(name)
        if command is None:
            raise InvalidParamsError(f"Unknown command: {name}")
        try:
            validate_command_arguments(command, command_arguments)
            render_path(command, command_arguments)
        except ValueError as exc:
            raise InvalidParamsError(str(exc)) from exc

    @staticmethod
    def _validate_raw_api_arguments(arguments: dict[str, Any]) -> None:
        method = arguments.get("method")
        path = arguments.get("path")
        body = arguments.get("body")
        if (
            not isinstance(method, str)
            or method not in {"GET", "POST", "PUT", "DELETE"}
            or not isinstance(path, str)
            or ("body" in arguments and not isinstance(body, dict))
        ):
            raise InvalidParamsError()
        try:
            validate_raw_api_request(method, path, body)
        except ValueError as exc:
            raise InvalidParamsError(str(exc)) from exc

    def _validate_uia_locator_arguments(self, arguments: dict[str, Any]) -> None:
        capability = arguments.get("capability")
        automation_id = arguments.get("automation_id")
        if capability is not None and automation_id is not None:
            raise InvalidParamsError("Provide either capability or automation_id, not both.")
        if capability is not None:
            allowed_capabilities = {
                *self.manifest["uia"]["capability_ids"],
                self.manifest["uia"]["root_id"],
            }
            if capability not in allowed_capabilities:
                raise InvalidParamsError("UIA capability is not allowlisted by the manifest.")
        elif not (
            self._is_parameter_automation_id(automation_id)
            or self._is_processor_catalog_automation_id(automation_id)
            or self._is_stream_row_automation_id(automation_id)
        ):
            raise InvalidParamsError("AutomationId must match a declared dynamic UIA rule.")

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
        if name in self.manifest["typed_tools"]:
            payload = execute_typed_stream_tool(
                name,
                arguments,
                manifest=self.manifest,
                base_url=self.base_url,
            )
            return text_result(payload, is_error=payload.get("ok") is not True)
        if name == "oe_uia_locator":
            capability = arguments.get("capability")
            automation_id = arguments.get("automation_id")
            if capability is not None and automation_id is not None:
                raise ValueError("Provide either capability or automation_id, not both.")

            if capability is not None:
                allowed_capabilities = {
                    *self.manifest["uia"]["capability_ids"],
                    self.manifest["uia"]["root_id"],
                }
                if capability not in allowed_capabilities:
                    raise ValueError("UIA capability is not allowlisted by the manifest.")
                automation_id = capability
                rule = self.manifest["uia"]["automation_id_rule"]
            elif self._is_parameter_automation_id(automation_id):
                rule = self.manifest["uia"]["parameter_automation_id_rule"]
            elif self._is_processor_catalog_automation_id(automation_id):
                rule = self.manifest["uia"]["processor_catalog_automation_id_rule"]
            elif self._is_stream_row_automation_id(automation_id):
                rule = self.manifest["uia"]["stream_row_automation_id_rule"]
            else:
                raise ValueError(
                    "AutomationId must match a declared dynamic UIA rule."
                )

            payload = {
                "automation_id": automation_id,
                "transport": "windows_uia",
                "rule": rule,
                "inspect_script": self.manifest["uia"]["script"],
            }
            if capability is not None:
                payload["capability"] = capability
            return text_result(
                payload
            )
        raise ValueError(f"Unknown tool: {name}")

    def _is_parameter_automation_id(self, automation_id: Any) -> bool:
        return self._matches_dynamic_automation_id(
            automation_id,
            self.manifest["uia"]["parameter_automation_id_rule"],
            "<sanitised parameter key>",
        )

    def _is_processor_catalog_automation_id(self, automation_id: Any) -> bool:
        return self._matches_dynamic_automation_id(
            automation_id,
            self.manifest["uia"]["processor_catalog_automation_id_rule"],
            "<sanitised processor slug>",
        )

    @staticmethod
    def _is_stream_row_automation_id(automation_id: Any) -> bool:
        return parse_stream_row_automation_id(automation_id) is not None

    @staticmethod
    def _matches_dynamic_automation_id(
        automation_id: Any, rule: str, placeholder: str
    ) -> bool:
        if not isinstance(automation_id, str):
            return False

        prefix = rule.removesuffix(placeholder)
        return (
            re.fullmatch(
                re.escape(prefix) + r"[a-z0-9]+(?:_[a-z0-9]+)*",
                automation_id,
            )
            is not None
        )


def decode_stdio_line(line: str) -> Any:
    def reject_nonstandard_constant(constant: str) -> None:
        raise json.JSONDecodeError(
            f"Invalid constant: {constant}",
            line,
            line.find(constant),
        )

    return json.loads(line, parse_constant=reject_nonstandard_constant)


def run_stdio(server: McpServer) -> None:
    for line in sys.stdin:
        if not line.strip():
            continue
        try:
            request = decode_stdio_line(line)
        except json.JSONDecodeError:
            response = {
                "jsonrpc": "2.0",
                "id": None,
                "error": {"code": -32700, "message": "Parse error"},
            }
        else:
            response = server.handle(request)
        if response is not None:
            sys.stdout.write(
                json.dumps(response, separators=(",", ":"), allow_nan=False) + "\n"
            )
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
