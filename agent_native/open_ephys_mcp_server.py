#!/usr/bin/env python3
"""Narrow legacy MCP bridge for Open Ephys v1.1.0 agent contract 0.0.4."""

from __future__ import annotations

import argparse
import json
import math
import ntpath
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ElementTree
from pathlib import Path
from typing import Any


PROTOCOL_VERSION = "2024-11-05"
CONTRACT_VERSION = "0.0.4"
DEFAULT_CONTRACT = Path(__file__).with_name("open_ephys_agent_contract_v1_1_0_v0_0_4.json")
DEFAULT_ACTIVE_CONTRACT = Path(__file__).with_name("open_ephys_agent_contract_v1_1_0_v0_0_5.json")
DEFAULT_PRESET_CONTRACT = Path(__file__).with_name("neuropixels_preset_0.0.5.json")
V003_TOOL_NAMES = (
    "oe_get_capabilities", "oe_get_status", "oe_set_status",
    "oe_get_recording_options", "oe_set_recording_options",
    "oe_get_recording_filename", "oe_set_recording_filename",
    "oe_get_recording_directory", "oe_set_recording_directory",
    "oe_get_config",
    "oe_get_cpu", "oe_get_disk", "oe_get_time",
)
V004_TOOL_NAMES = (*V003_TOOL_NAMES[:10], "oe_get_processors", *V003_TOOL_NAMES[10:])
PRESET_TOOL_NAMES = ("oe_get_electrode_presets", "oe_set_electrode_preset")
TOOL_NAMES = (*V004_TOOL_NAMES, *PRESET_TOOL_NAMES)
V003_CAPABILITY_IDS = (
    "oe.control.acquisition", "oe.control.recording", "oe.control.recording.options",
    "oe.control.recording.filename", "oe.control.recording.directory",
    "oe.control.recording.new_directory",
    "oe.control.recording.force_new_directory", "oe.control.signal_chain.configuration",
    "oe.status.cpu_usage",
    "oe.status.disk_usage", "oe.status.elapsed_time",
)
V004_CAPABILITY_IDS = (*V003_CAPABILITY_IDS[:8], "oe.control.signal_chain.processors", *V003_CAPABILITY_IDS[8:])
PRESET_CAPABILITY_ID = "oe.control.neuropixels.preset"
CAPABILITY_IDS = (*V004_CAPABILITY_IDS, PRESET_CAPABILITY_ID)
PRESET_CONTRACT_VERSION = "0.0.5"
PRESET_INVENTORY_PATH = "/api/plugins/neuropixels/presets"
PRESET_SELECTED_PATH = "/api/plugins/neuropixels/presets/selected"
PRESET_CONVERGENCE_SECONDS = 3.0
PRESET_POLL_SECONDS = 0.1
DEFINITIVE_PRESET_HTTP_ERROR_CODES = frozenset({
    "invalid_arguments", "capability_unavailable", "processor_not_found",
    "probe_not_found", "ambiguous_target", "probe_identity_mismatch",
    "inventory_generation_mismatch", "electrode_map_expectation_mismatch",
    "preset_not_found", "preset_not_supported", "preset_change_requires_idle",
    "preset_apply_in_progress", "preset_apply_failed", "postcondition_failed",
})
DEFINITIVE_PRESET_5XX_ERROR_CODES = frozenset({"capability_unavailable", "preset_apply_failed"})
_BASE_CONTRACT_PATH = Path(__file__).with_name("open_ephys_agent_contract_v1_1_0_v0_0_4.json")
REMOTE_BASE_CAPABILITIES = tuple(
    json.loads(_BASE_CONTRACT_PATH.read_text(encoding="utf-8"))["api"]
    ["expected_capabilities_response"]["capabilities"]
)
ALLOWED_MODES = {"IDLE", "ACQUIRE", "RECORD"}
OPTION_FIELDS = ("expanded", "force_new_directory", "new_directory_requested")
FILENAME_FIELDS = ("prepend_text", "base_text", "append_text")

PARSE_ERROR, INVALID_REQUEST, METHOD_NOT_FOUND, INVALID_PARAMS, INTERNAL_ERROR = -32700, -32600, -32601, -32602, -32603
SERVER_NOT_INITIALIZED = -32002


class JsonRpcError(Exception):
    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code, self.message = code, message


class ToolError(Exception):
    def __init__(self, code: str, message: str, **details: Any):
        super().__init__(message)
        self.code, self.message, self.details = code, message, details

    def payload(self) -> dict[str, Any]:
        return {"ok": False, "error": {"code": self.code, "message": self.message, **self.details}}


class ApiHttpError(ToolError):
    def __init__(self, status: int | None, body: Any):
        super().__init__("api_http_error", "Open Ephys API request failed.", status=status, body=body)


class NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def _reject_nonfinite(value: str) -> None:
    raise ValueError(f"Non-finite JSON constant is not permitted: {value}")


def loads_json(text: str) -> Any:
    return json.loads(text, parse_constant=_reject_nonfinite)


def dumps_json(value: Any) -> str:
    return json.dumps(value, separators=(",", ":"), sort_keys=True, allow_nan=False)


def _is_number(value: Any) -> bool:
    return not isinstance(value, bool) and isinstance(value, (int, float)) and math.isfinite(float(value))


def load_contract(path: Path) -> dict[str, Any]:
    try:
        contract = loads_json(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        raise ValueError(f"Cannot load contract {path}: {exc}") from exc
    version = contract.get("schema_version")
    if version not in {"0.0.3", CONTRACT_VERSION} and version != PRESET_CONTRACT_VERSION:
        raise ValueError(f"Unsupported contract version: {version!r}")
    expected_tool_names = (V003_TOOL_NAMES if version == "0.0.3"
                           else V004_TOOL_NAMES if version == CONTRACT_VERSION
                           else TOOL_NAMES)
    expected_capability_ids = (V003_CAPABILITY_IDS if version == "0.0.3"
                               else V004_CAPABILITY_IDS if version == CONTRACT_VERSION
                               else CAPABILITY_IDS)
    expected = {
        "schema_version": version,
        "contract": {"id": "open-ephys-agent", "version": version},
        "bundle_id": "open-ephys-agent-native",
        "bundle_version": version,
        "baseline_version": "1.1.0",
        "mcp_protocol_version": PROTOCOL_VERSION,
        "mcp_modern_protocol_supported": False,
        "mcp_server_name": "open-ephys-agent-native",
    }
    actual = {
        "schema_version": contract.get("schema_version"),
        "contract": contract.get("contract"),
        "bundle_id": (contract.get("bundle") or {}).get("id"),
        "bundle_version": (contract.get("bundle") or {}).get("version"),
        "baseline_version": (contract.get("baseline") or {}).get("version"),
        "mcp_protocol_version": (contract.get("mcp") or {}).get("protocol_version"),
        "mcp_modern_protocol_supported": (contract.get("mcp") or {}).get("modern_protocol_supported"),
        "mcp_server_name": (contract.get("mcp") or {}).get("server_name"),
    }
    if actual != expected:
        raise ValueError(f"Contract pins do not match the v1.1.0 server: {actual!r}")
    tools = contract.get("tools")
    if not isinstance(tools, list) or [tool.get("name") for tool in tools if isinstance(tool, dict)] != list(expected_tool_names):
        raise ValueError("Contract tool names do not match the narrow Core R0 surface.")
    for tool in tools:
        schema = tool.get("inputSchema") if isinstance(tool, dict) else None
        if not isinstance(schema, dict) or schema.get("additionalProperties") is not False:
            raise ValueError("Every Core R0 tool requires a closed input schema.")
    api = contract.get("api")
    capabilities = api.get("expected_capabilities_response") if isinstance(api, dict) else None
    if not isinstance(capabilities, dict) or capabilities.get("contract_version") != version:
        raise ValueError(f"Capability fixture must pin API contract {version}.")
    items = capabilities.get("capabilities")
    if not isinstance(items, list) or [item.get("id") for item in items if isinstance(item, dict)] != list(expected_capability_ids):
        raise ValueError(f"Capability fixture does not match the exact {version} capabilities.")
    if contract.get("verification") != {
        "hardware_verified": False,
        "scientific_verified": False,
    }:
        raise ValueError(
            "Contract must explicitly pin verification.hardware_verified=false "
            "and verification.scientific_verified=false."
        )
    return contract


def load_preset_contract(path: Path) -> dict[str, Any]:
    try:
        contract = loads_json(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        raise ValueError(f"Cannot load preset contract {path}: {exc}") from exc
    expected_descriptor = {
        "id": PRESET_CAPABILITY_ID,
        "version": PRESET_CONTRACT_VERSION,
        "operations": ["inventory", "set"],
    }
    remote_api = contract.get("remote_api", {})
    capability_route = remote_api.get("capability_document", {})
    operations = remote_api.get("operations", {})
    routes_match = (
        capability_route.get("method") == "GET"
        and capability_route.get("path") == "/api/capabilities"
        and operations.get("inventory", {}).get("method") == "GET"
        and operations.get("inventory", {}).get("path") == PRESET_INVENTORY_PATH
        and operations.get("set", {}).get("method") == "PUT"
        and operations.get("set", {}).get("path") == PRESET_SELECTED_PATH
    )
    tools = contract.get("tools")
    if (contract.get("contract_version") != PRESET_CONTRACT_VERSION
            or contract.get("status") != "implemented_unreleased"
            or capability_route.get("required_descriptor") != expected_descriptor
            or not routes_match
            or not isinstance(tools, list)
            or [tool.get("name") for tool in tools if isinstance(tool, dict)] != list(PRESET_TOOL_NAMES)):
        raise ValueError("Neuropixels preset requirement does not match the 0.0.5 adapter surface.")
    if contract.get("compatible_processor_cardinality") != {
            "supported": "exactly_one", "zero": "capability_unavailable", "multiple": "ambiguous_target"}:
        raise ValueError("Preset requirement must support exactly one compatible processor.")
    if contract.get("verification") != {
            "bridge_contract_tests_verified": True, "software_verified": False,
            "hardware_verified": False, "scientific_verified": False}:
        raise ValueError("Preset verification matrix does not preserve product release gates.")
    for tool in tools:
        if not isinstance(tool.get("inputSchema"), dict) or not isinstance(tool.get("outputSchema"), dict):
            raise ValueError("Every preset tool requires input and output schemas.")
    return contract


def _schema_type_matches(value: Any, type_name: str) -> bool:
    if type_name == "object": return isinstance(value, dict)
    if type_name == "array": return isinstance(value, list)
    if type_name == "string": return isinstance(value, str)
    if type_name == "null": return value is None
    if type_name == "boolean": return isinstance(value, bool)
    if type_name == "integer": return isinstance(value, int) and not isinstance(value, bool)
    if type_name == "number": return _is_number(value)
    return False


def validate_json_schema(value: Any, schema: dict[str, Any], *, root: dict[str, Any] | None = None,
                         path: str = "$") -> None:
    root = schema if root is None else root
    if "$ref" in schema:
        reference = schema["$ref"]
        if not isinstance(reference, str) or not reference.startswith("#/$defs/"):
            raise ValueError(f"{path}: unsupported schema reference")
        definition = root.get("$defs", {}).get(reference.removeprefix("#/$defs/"))
        if not isinstance(definition, dict):
            raise ValueError(f"{path}: unresolved schema reference")
        validate_json_schema(value, definition, root=root, path=path)
        return
    if "oneOf" in schema:
        matches = 0
        for option in schema["oneOf"]:
            try:
                validate_json_schema(value, option, root=root, path=path)
                matches += 1
            except ValueError:
                pass
        if matches != 1:
            raise ValueError(f"{path}: value must match exactly one schema")
        return
    if "const" in schema and value != schema["const"]:
        raise ValueError(f"{path}: value does not match const")
    declared = schema.get("type")
    if declared is not None:
        allowed = declared if isinstance(declared, list) else [declared]
        if not any(_schema_type_matches(value, name) for name in allowed):
            raise ValueError(f"{path}: invalid type")
    if isinstance(value, dict) and declared == "object":
        properties, required = schema.get("properties", {}), schema.get("required", [])
        if any(name not in value for name in required):
            raise ValueError(f"{path}: missing required properties")
        if schema.get("additionalProperties") is False and not set(value).issubset(properties):
            raise ValueError(f"{path}: additional properties are forbidden")
        for name, item in value.items():
            if name in properties:
                validate_json_schema(item, properties[name], root=root, path=f"{path}.{name}")
    if isinstance(value, list) and declared == "array" and isinstance(schema.get("items"), dict):
        for index, item in enumerate(value):
            validate_json_schema(item, schema["items"], root=root, path=f"{path}[{index}]")
    if isinstance(value, str):
        if len(value) < schema.get("minLength", 0):
            raise ValueError(f"{path}: string is too short")
        if "pattern" in schema and re.fullmatch(schema["pattern"], value) is None:
            raise ValueError(f"{path}: string does not match pattern")
    if _is_number(value) and "minimum" in schema and value < schema["minimum"]:
        raise ValueError(f"{path}: number is below minimum")


class ApiClient:
    def __init__(self, base_url: str, timeout: float = 3.0):
        parsed = urllib.parse.urlsplit(base_url)
        if (parsed.scheme != "http" or parsed.hostname not in {"127.0.0.1", "localhost"}
                or parsed.username is not None or parsed.password is not None
                or parsed.path not in {"", "/"} or parsed.query or parsed.fragment):
            raise ValueError("Open Ephys API base URL must be loopback HTTP without a path, credentials, query, or fragment.")
        self.base_url, self.timeout = base_url.rstrip("/"), timeout
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirectHandler())

    def request(self, method: str, path: str, body: dict[str, Any] | None = None) -> Any:
        data, headers = None, {"Accept": "application/json"}
        if body is not None:
            data, headers = dumps_json(body).encode("utf-8"), {**headers, "Content-Type": "application/json"}
        request = urllib.request.Request(self.base_url + path, data=data, headers=headers, method=method)
        try:
            with self.opener.open(request, timeout=self.timeout) as response:
                raw = response.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            raw = exc.read().decode("utf-8", errors="replace")
            try: body_payload = loads_json(raw)
            except (ValueError, json.JSONDecodeError): body_payload = raw
            raise ApiHttpError(exc.code, body_payload) from exc
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            raise ApiHttpError(None, str(exc)) from exc
        try:
            return loads_json(raw)
        except (ValueError, json.JSONDecodeError) as exc:
            raise ToolError("response_schema_mismatch", "Open Ephys API returned non-JSON content.", path=path) from exc


def text_result(payload: Any, *, is_error: bool = False) -> dict[str, Any]:
    result: dict[str, Any] = {"content": [{"type": "text", "text": dumps_json(payload)}]}
    if is_error: result["isError"] = True
    return result


def expect_object(payload: Any, label: str) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise ToolError("response_schema_mismatch", f"{label} response must be a JSON object.")
    return payload


def validate_status(payload: Any) -> dict[str, str]:
    value = expect_object(payload, "Status")
    if set(value) != {"mode"} or value.get("mode") not in ALLOWED_MODES:
        raise ToolError("response_schema_mismatch", "Status response must contain only mode with IDLE, ACQUIRE, or RECORD.")
    return {"mode": value["mode"]}


def validate_options(payload: Any, *, mutation_response: bool = False) -> dict[str, Any]:
    value = expect_object(payload, "Recording options")
    allowed = {"capability", *OPTION_FIELDS, "recording"} | ({"ok"} if mutation_response else set())
    if set(value) != allowed or value.get("capability") != "oe.control.recording.options":
        raise ToolError("response_schema_mismatch", "Recording-options response has an unexpected shape.")
    if any(not isinstance(value[field], bool) for field in (*OPTION_FIELDS, "recording")):
        raise ToolError("response_schema_mismatch", "Recording-options values must be booleans.")
    if mutation_response and value.get("ok") is not True:
        raise ToolError("response_schema_mismatch", "Recording-options mutation response must confirm success.")
    return {field: value[field] for field in ("capability", *OPTION_FIELDS, "recording")}


def validate_recording(payload: Any) -> dict[str, Any]:
    value = expect_object(payload, "Recording")
    required_strings = {"parent_directory", "prepend_text", "base_text", "append_text", "default_record_engine"}
    if not required_strings.issubset(value) or any(not isinstance(value[field], str) for field in required_strings) or not isinstance(value.get("record_nodes"), list):
        raise ToolError("response_schema_mismatch", "Recording response is missing required typed fields.")
    return value


def filename_projection(payload: Any) -> dict[str, str]:
    recording = validate_recording(payload)
    return {field: recording[field] for field in FILENAME_FIELDS}


def directory_projection(payload: Any) -> dict[str, str]:
    recording = validate_recording(payload)
    return {"parent_directory": recording["parent_directory"]}


def is_reserved_windows_device_name(value: str) -> bool:
    suffixes = {*(str(i) for i in range(1, 10)), "¹", "²", "³"}
    reserved = {"CON", "PRN", "AUX", "NUL", *{f"COM{x}" for x in suffixes}, *{f"LPT{x}" for x in suffixes}}
    return value.rstrip(" .").split(".", 1)[0].upper() in reserved


def validate_config(payload: Any) -> dict[str, str]:
    value = expect_object(payload, "Config")
    info = value.get("info")
    if not isinstance(info, str):
        raise ToolError("response_schema_mismatch", "Config response must contain string info.")
    try:
        root = ElementTree.fromstring(info)
    except ElementTree.ParseError as exc:
        raise ToolError("response_schema_mismatch", "Config info must contain well-formed XML.") from exc
    if root.tag != "SETTINGS":
        raise ToolError("response_schema_mismatch", "Config XML root must be SETTINGS.")
    return {"info": info}


def _validate_processor_parameter(parameter: Any) -> None:
    value = expect_object(parameter, "Processor parameter")
    if set(value) != {"name", "type", "value"} or any(
        not isinstance(value.get(field), str) for field in value
    ):
        raise ToolError(
            "response_schema_mismatch",
            "Processor parameters must contain exact string name/type/value fields.",
        )


def _validate_processor_stream(stream: Any) -> None:
    value = expect_object(stream, "Processor stream")
    if set(value) != {"name", "source_id", "sample_rate", "channel_count", "parameters"}:
        raise ToolError("response_schema_mismatch", "Processor streams have an unexpected shape.")
    if (
        not isinstance(value["name"], str)
        or not isinstance(value["source_id"], int)
        or isinstance(value["source_id"], bool)
        or value["source_id"] < 0
        or not _is_number(value["sample_rate"])
        or not isinstance(value["channel_count"], int)
        or isinstance(value["channel_count"], bool)
        or value["channel_count"] < 0
        or not isinstance(value["parameters"], list)
    ):
        raise ToolError("response_schema_mismatch", "Processor stream fields have invalid types or values.")
    for parameter in value["parameters"]:
        _validate_processor_parameter(parameter)


def processors_projection(payload: Any) -> dict[str, list[dict[str, Any]]]:
    value = expect_object(payload, "Processors")
    if set(value) != {"processors"} or not isinstance(value["processors"], list):
        raise ToolError(
            "response_schema_mismatch",
            "Processors response must contain only a processors array.",
        )

    result = []
    seen_ids = set()
    for processor in value["processors"]:
        item = expect_object(processor, "Processor")
        if set(item) != {"id", "name", "parameters", "streams", "predecessor"}:
            raise ToolError("response_schema_mismatch", "Each processor has an unexpected shape.")
        processor_id = item["id"]
        predecessor = item["predecessor"]
        if (
            not isinstance(processor_id, int)
            or isinstance(processor_id, bool)
            or processor_id < 0
            or processor_id in seen_ids
            or not isinstance(item["name"], str)
            or not isinstance(item["parameters"], list)
            or not isinstance(item["streams"], list)
            or (
                predecessor is not None
                and (
                    not isinstance(predecessor, int)
                    or isinstance(predecessor, bool)
                    or predecessor < 0
                )
            )
        ):
            raise ToolError("response_schema_mismatch", "Processor fields have invalid types or values.")
        for parameter in item["parameters"]:
            _validate_processor_parameter(parameter)
        for stream in item["streams"]:
            _validate_processor_stream(stream)
        seen_ids.add(processor_id)
        result.append({"id": processor_id, "name": item["name"], "predecessor": predecessor})
    return {"processors": result}


def normalize_windows_directory(value: str) -> str:
    if not value:
        raise ToolError("invalid_arguments", "parent_directory must be a non-empty drive-letter-rooted Windows path.")
    normalized = ntpath.normpath(value)
    drive, tail = ntpath.splitdrive(normalized)
    is_drive_letter = (
        len(drive) == 2
        and drive[0].isascii()
        and drive[0].isalpha()
        and drive[1] == ":"
    )
    has_reserved_device_component = any(
        is_reserved_windows_device_name(component)
        for component in tail.split("\\")
        if component
    )
    if (not is_drive_letter or not tail.startswith("\\") or not ntpath.isabs(normalized)
            or ":" in tail or has_reserved_device_component):
        raise ToolError("invalid_arguments", "parent_directory must be a non-empty drive-letter-rooted Windows path.")
    return normalized


def windows_paths_equivalent(left: str, right: str) -> bool:
    return ntpath.normcase(ntpath.normpath(left)) == ntpath.normcase(ntpath.normpath(right))


def validate_cpu(payload: Any) -> dict[str, float | int]:
    value = expect_object(payload, "CPU")
    if set(value) != {"usage"} or not _is_number(value.get("usage")) or not 0.0 <= float(value["usage"]) <= 1.0:
        raise ToolError("response_schema_mismatch", "CPU response must contain only finite usage from 0.0 through 1.0.")
    return {"usage": value["usage"]}


def validate_disk(payload: Any) -> dict[str, Any]:
    value = expect_object(payload, "Disk")
    expected = {"capability", "usage", "minimum", "maximum", "read_only"}
    if set(value) != expected or value.get("capability") != "oe.status.disk_usage" or value.get("read_only") is not True:
        raise ToolError("response_schema_mismatch", "Disk response has an unexpected shape.")
    if any(not _is_number(value.get(field)) for field in ("usage", "minimum", "maximum")) or (float(value["minimum"]), float(value["maximum"])) != (0.0, 1.0) or not 0.0 <= float(value["usage"]) <= 1.0:
        raise ToolError("response_schema_mismatch", "Disk usage must be a finite fraction with the pinned range.")
    return value


def validate_time(payload: Any) -> dict[str, Any]:
    value = expect_object(payload, "Time")
    expected = {"capability", "display", "elapsed_milliseconds", "mode", "reference", "running", "recording", "read_only"}
    if set(value) != expected or value.get("capability") != "oe.status.elapsed_time" or value.get("read_only") is not True:
        raise ToolError("response_schema_mismatch", "Time response has an unexpected shape.")
    if (not all(isinstance(value.get(field), str) for field in ("display", "mode", "reference"))
            or value.get("mode") not in ALLOWED_MODES or not isinstance(value.get("elapsed_milliseconds"), int)
            or isinstance(value.get("elapsed_milliseconds"), bool) or value["elapsed_milliseconds"] < 0
            or not all(isinstance(value.get(field), bool) for field in ("running", "recording"))):
        raise ToolError("response_schema_mismatch", "Time response fields have invalid types or values.")
    return value


def validate_filename_component(value: str) -> None:
    if any(ord(character) < 32 or ord(character) == 127 for character in value):
        raise ToolError("invalid_arguments", "Filename components cannot contain control characters.")
    if any(character in '<>:"/\\|?*' for character in value) or ".." in value:
        raise ToolError("invalid_arguments", "Filename components must not contain paths or Windows-invalid characters.")
    if value.endswith((" ", ".")):
        raise ToolError("invalid_arguments", "Filename components cannot end with a space or dot.")
    if is_reserved_windows_device_name(value):
        raise ToolError("invalid_arguments", "Filename component is a reserved Windows device name.")


class McpServer:
    def __init__(self, contract_path: Path = DEFAULT_ACTIVE_CONTRACT, base_url: str | None = None,
                 timeout: float = 3.0, *, preset_contract_path: Path = DEFAULT_PRESET_CONTRACT,
                 api_client: Any | None = None):
        self.contract = load_contract(Path(contract_path))
        self.contract_version = self.contract["contract"]["version"]
        self.tool_names = tuple(tool["name"] for tool in self.contract["tools"])
        self.preset_contract = None
        if self.contract_version == PRESET_CONTRACT_VERSION:
            self.preset_contract = load_preset_contract(Path(preset_contract_path))
            contract_tools = {tool["name"]: tool for tool in self.contract["tools"]}
            for preset_tool in self.preset_contract["tools"]:
                if contract_tools[preset_tool["name"]].get("inputSchema") != preset_tool["inputSchema"]:
                    raise ValueError("Main contract and preset requirement input schemas do not match.")
        if api_client is not None and base_url is not None:
            raise ValueError("Provide either api_client or base_url, not both.")
        self.api = api_client or ApiClient(
            base_url or self.contract["transport"]["http_base_url"], timeout=timeout)
        self.connection_state = "new"

    def _preset_tools(self) -> dict[str, dict[str, Any]]:
        if self.preset_contract is None:
            return {}
        return {tool["name"]: tool for tool in self.preset_contract["tools"]}

    def _remote_capabilities(self) -> dict[str, Any] | None:
        if self.preset_contract is None:
            raise ToolError("capability_unavailable", "Preset contract is not active for this server profile.")
        expected = self.preset_contract["remote_api"]["capability_document"]["required_descriptor"]
        document = expect_object(self.api.request("GET", "/api/capabilities"), "Capabilities")
        self._last_capabilities = document
        capabilities = document.get("capabilities")
        base = list(REMOTE_BASE_CAPABILITIES)
        if set(document) != {"contract_version", "capabilities"} or not isinstance(capabilities, list):
            raise ToolError("capability_contract_mismatch", "Open Ephys capability meanings do not match the additive 0.0.5 contract.")
        remote_base = capabilities[:len(base)]
        if remote_base != base:
            raise ToolError("capability_contract_mismatch", "Open Ephys capability meanings do not match the additive 0.0.5 contract.")
        if len(capabilities) == len(base):
            return None
        if len(capabilities) != len(base) + 1:
            raise ToolError("capability_contract_mismatch", "Open Ephys capability count or version does not match the additive 0.0.5 contract.")
        if document.get("contract_version") != PRESET_CONTRACT_VERSION:
            return None
        if capabilities[-1] != expected:
            if (isinstance(capabilities[-1], dict)
                    and capabilities[-1].get("id") == PRESET_CAPABILITY_ID
                    and capabilities[-1].get("version") != PRESET_CONTRACT_VERSION):
                return None
            raise ToolError("capability_contract_mismatch", "Open Ephys preset capability is not the exact 0.0.5 descriptor.")
        return expected.copy()

    @staticmethod
    def _valid_id(value: Any) -> bool:
        return isinstance(value, str) or (isinstance(value, int) and not isinstance(value, bool))

    @staticmethod
    def _validate_request(request: Any) -> None:
        if not isinstance(request, dict) or request.get("jsonrpc") != "2.0" or not isinstance(request.get("method"), str):
            raise JsonRpcError(INVALID_REQUEST, "Invalid Request")
        if "id" in request and not McpServer._valid_id(request["id"]):
            raise JsonRpcError(INVALID_REQUEST, "Invalid Request")
        if "params" in request and not isinstance(request["params"], dict):
            raise JsonRpcError(INVALID_PARAMS, "Invalid params")
        method = request["method"]
        if method in {"initialize", "tools/list", "tools/call"} and "id" not in request:
            raise JsonRpcError(INVALID_REQUEST, f"{method} requires a request id")
        if method == "notifications/initialized" and "id" in request:
            raise JsonRpcError(INVALID_REQUEST, "notifications/initialized must not have an id")

    def handle(self, request: Any) -> dict[str, Any] | None:
        request_id = request.get("id") if isinstance(request, dict) and self._valid_id(request.get("id")) else None
        notification = isinstance(request, dict) and request.get("jsonrpc") == "2.0" and isinstance(request.get("method"), str) and "id" not in request
        try:
            self._validate_request(request)
            method, params = request["method"], request.get("params", {})
            if method == "initialize": result = self._initialize(params)
            elif method == "notifications/initialized":
                if self.connection_state != "initialize_responded": raise JsonRpcError(INVALID_REQUEST, "Unexpected initialized notification.")
                self.connection_state = "ready"; return None
            elif method == "tools/list": self._require_ready(); self._require_empty_params(params); result = {"tools": self.contract["tools"]}
            elif method == "tools/call": self._require_ready(); result = self._call_tool(params)
            else: raise JsonRpcError(METHOD_NOT_FOUND, "Method not found")
        except JsonRpcError as exc:
            if notification: return None
            return {"jsonrpc": "2.0", "id": request_id, "error": {"code": exc.code, "message": exc.message}}
        except Exception as exc:
            print(f"open-ephys-mcp internal error: {exc}", file=sys.stderr)
            if notification: return None
            return {"jsonrpc": "2.0", "id": request_id, "error": {"code": INTERNAL_ERROR, "message": "Internal error"}}
        return {"jsonrpc": "2.0", "id": request_id, "result": result}

    @staticmethod
    def _require_empty_params(params: dict[str, Any]) -> None:
        if params and (set(params) != {"_meta"} or not isinstance(params["_meta"], dict)):
            raise JsonRpcError(INVALID_PARAMS, "Invalid params")

    def _initialize(self, params: dict[str, Any]) -> dict[str, Any]:
        if self.connection_state != "new": raise JsonRpcError(INVALID_REQUEST, "Server is already initialized.")
        required = {"protocolVersion", "capabilities", "clientInfo"}
        if not required.issubset(params) or not set(params).issubset(required | {"_meta"}) or not isinstance(params["protocolVersion"], str) or not params["protocolVersion"] or not isinstance(params["capabilities"], dict) or not isinstance(params["clientInfo"], dict) or ("_meta" in params and not isinstance(params["_meta"], dict)):
            raise JsonRpcError(INVALID_PARAMS, "Invalid legacy initialize parameters.")
        client_info = params["clientInfo"]
        if (not isinstance(client_info.get("name"), str) or not client_info["name"]
                or not isinstance(client_info.get("version"), str) or not client_info["version"]):
            raise JsonRpcError(INVALID_PARAMS, "Invalid legacy initialize parameters.")
        self.connection_state = "initialize_responded"
        return {"protocolVersion": PROTOCOL_VERSION, "capabilities": {"tools": {}}, "serverInfo": {"name": self.contract["mcp"]["server_name"], "version": self.contract_version}}

    def _require_ready(self) -> None:
        if self.connection_state != "ready": raise JsonRpcError(SERVER_NOT_INITIALIZED, "Server not initialized")

    def _call_tool(self, params: dict[str, Any]) -> dict[str, Any]:
        if (not {"name"}.issubset(params) or not set(params).issubset({"name", "arguments", "_meta"})
                or params.get("name") not in self.tool_names or not isinstance(params.get("arguments", {}), dict)
                or ("_meta" in params and not isinstance(params["_meta"], dict))):
            raise JsonRpcError(INVALID_PARAMS, "Unknown tool or invalid arguments object.")
        try:
            name, arguments = params["name"], params.get("arguments", {})
            self._validate_arguments(name, arguments)
            return text_result(self._execute_tool(name, arguments))
        except ToolError as exc:
            return text_result(exc.payload(), is_error=True)

    def _validate_arguments(self, name: str, arguments: dict[str, Any]) -> None:
        if name in {"oe_get_capabilities", "oe_get_status", "oe_get_recording_options", "oe_get_recording_filename", "oe_get_recording_directory", "oe_get_config", "oe_get_processors", "oe_get_cpu", "oe_get_disk", "oe_get_time"}:
            if arguments: raise ToolError("invalid_arguments", f"{name} does not accept arguments.")
        elif name == "oe_set_status":
            if not set(arguments).issubset({"mode", "approve_recording"}) or not isinstance(arguments.get("mode"), str) or arguments["mode"] not in ALLOWED_MODES or ("approve_recording" in arguments and not isinstance(arguments["approve_recording"], bool)):
                raise ToolError("invalid_arguments", "Status arguments are invalid.")
        elif name == "oe_set_recording_options":
            if len(arguments) != 1 or not set(arguments).issubset(OPTION_FIELDS) or not isinstance(next(iter(arguments.values())), bool):
                raise ToolError("invalid_arguments", "Provide exactly one boolean recording option.")
        elif name == "oe_set_recording_filename":
            if len(arguments) != 1 or not set(arguments).issubset(FILENAME_FIELDS) or not isinstance(next(iter(arguments.values())), str):
                raise ToolError("invalid_arguments", "Provide exactly one string filename field.")
            validate_filename_component(next(iter(arguments.values())))
        elif name == "oe_set_recording_directory":
            if set(arguments) != {"parent_directory"} or not isinstance(arguments["parent_directory"], str):
                raise ToolError("invalid_arguments", "parent_directory must be the only string argument.")
            normalize_windows_directory(arguments["parent_directory"])
        elif name in PRESET_TOOL_NAMES:
            try:
                validate_json_schema(arguments, self._preset_tools()[name]["inputSchema"])
            except ValueError as exc:
                raise ToolError("invalid_arguments", f"{name} arguments do not match the 0.0.5 contract.") from exc

    def _verify_capabilities(self) -> dict[str, Any]:
        actual = self.api.request("GET", "/api/capabilities")
        expected = self.contract["api"]["expected_capabilities_response"]
        if actual != expected:
            raise ToolError("capability_contract_mismatch", f"Open Ephys capabilities do not exactly match the pinned {self.contract_version} contract.")
        return actual

    def _raise_mutation_outcome_unknown(self, requested: dict[str, Any], put_error: ToolError, path: str, validator, *, submitted: dict[str, Any] | None = None) -> None:
        details: dict[str, Any] = {"requested": requested, "put_error": put_error.payload()["error"]}
        if submitted is not None:
            details["submitted"] = submitted
        try:
            details["observed"] = validator(self.api.request("GET", path))
        except ToolError as readback_error:
            details["readback_error"] = readback_error.payload()["error"]
        raise ToolError(
            "mutation_outcome_unknown",
            "Open Ephys may have committed the mutation, but the write response was not authoritative.",
            **details,
        )

    @staticmethod
    def _is_authoritative_put_http_error(error: ApiHttpError) -> bool:
        status = error.details["status"]
        return isinstance(status, int) and 400 <= status < 500 and status != 408

    @staticmethod
    def _typed_preset_http_error(error: ApiHttpError) -> ToolError | None:
        body = error.details.get("body")
        payload = body.get("error") if isinstance(body, dict) else None
        if not isinstance(payload, dict):
            return None
        code, message = payload.get("code"), payload.get("message")
        if (not isinstance(code, str) or not code or not isinstance(message, str) or not message
                or code not in DEFINITIVE_PRESET_HTTP_ERROR_CODES):
            return None
        details = {key: value for key, value in payload.items() if key not in {"code", "message"}}
        return ToolError(code, message, **details)

    def _validate_preset_payload(self, tool_name: str, payload: Any) -> dict[str, Any]:
        schema = self._preset_tools()[tool_name]["outputSchema"]
        try:
            validate_json_schema(payload, schema)
        except ValueError as exc:
            raise ToolError("response_schema_mismatch", f"{tool_name} response does not match the 0.0.5 contract.") from exc
        return payload

    def _read_preset_inventory(self) -> dict[str, Any]:
        try:
            response = self.api.request("GET", PRESET_INVENTORY_PATH)
        except ApiHttpError as exc:
            typed_error = self._typed_preset_http_error(exc)
            if typed_error is not None:
                raise typed_error
            raise
        value = self._validate_preset_payload("oe_get_electrode_presets", response)
        probe_ids = [target["probe_id"] for target in value["targets"]]
        if len(probe_ids) != len(set(probe_ids)):
            raise ToolError("response_schema_mismatch", "Preset inventory contains duplicate probe_id values.")
        for target in value["targets"]:
            preset_ids = [preset["preset_id"] for preset in target["available_presets"]]
            if len(preset_ids) != len(set(preset_ids)):
                raise ToolError("response_schema_mismatch", "Preset inventory contains duplicate preset IDs.")
        return value

    @staticmethod
    def _resolve_preset_target(inventory_value: dict[str, Any], arguments: dict[str, Any]) -> dict[str, Any]:
        processor_targets = [target for target in inventory_value["targets"]
                             if target["processor_id"] == arguments["processor_id"]]
        if not processor_targets:
            raise ToolError("processor_not_found", "No preset target has the requested processor_id.")
        matches = [target for target in processor_targets if target["probe_id"] == arguments["probe_id"]]
        if not matches:
            raise ToolError("probe_not_found", "No probe matches processor_id plus probe_id.")
        if len(matches) != 1:
            raise ToolError("ambiguous_target", "processor_id plus probe_id does not resolve uniquely.")
        return matches[0]

    @staticmethod
    def _preset_readback(target: dict[str, Any]) -> dict[str, Any]:
        selected = target["selected"]
        return {
            "processor_id": target["processor_id"], "probe_id": target["probe_id"],
            "probe_serial": target["probe_serial"], "preset_id": selected["preset_id"],
            "preset_label": selected["preset_label"],
            "electrode_map_hash": selected["electrode_map_hash"],
        }

    def _preset_preconditions(self, inventory_value: dict[str, Any],
                              arguments: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
        target = self._resolve_preset_target(inventory_value, arguments)
        if target["probe_serial"] != arguments["expected_probe_serial"]:
            raise ToolError("probe_identity_mismatch", "The fresh probe serial does not match the expected serial.")
        if inventory_value["inventory_generation"] != arguments["expected_inventory_generation"]:
            raise ToolError("inventory_generation_mismatch", "The preset inventory generation changed.")
        before = self._preset_readback(target)
        presets = [preset for preset in target["available_presets"]
                   if preset["preset_id"] == arguments["preset_id"]]
        if not presets:
            raise ToolError("preset_not_found", "The requested preset_id is absent from the fresh target inventory.")
        if len(presets) != 1:
            raise ToolError("response_schema_mismatch", "The target inventory has duplicate preset_id values.")
        desired = presets[0]
        if desired["electrode_map_hash"] != arguments["expected_electrode_map_hash"]:
            raise ToolError("electrode_map_expectation_mismatch", "The requested preset row hash changed.")
        return before, desired

    def _fresh_preset_target(self, arguments: dict[str, Any]) -> dict[str, Any]:
        return self._resolve_preset_target(self._read_preset_inventory(), arguments)

    def _raise_preset_outcome_unknown(self, arguments: dict[str, Any], put_error: ToolError) -> None:
        details: dict[str, Any] = {"requested": arguments, "put_error": put_error.payload()["error"]}
        try:
            details["observed"] = self._fresh_preset_target(arguments)
        except ToolError as readback_error:
            details["readback_error"] = readback_error.payload()["error"]
        raise ToolError(
            "mutation_outcome_unknown",
            "Preset write outcome is unknown; use the fresh getter state and do not retry the mutation.",
            **details,
        )

    def _set_electrode_preset(self, current: dict[str, Any], arguments: dict[str, Any]) -> dict[str, Any]:
        if current["mode"] != "IDLE":
            raise ToolError("preset_change_requires_idle", "Electrode preset changes require Open Ephys IDLE mode.")
        before, desired = self._preset_preconditions(self._read_preset_inventory(), arguments)
        if all(before[field] == desired[field] for field in ("preset_id", "preset_label", "electrode_map_hash")):
            result = {"changed": False, "before": before, "requested": arguments, "after": before}
            return self._validate_preset_payload("oe_set_electrode_preset", result)
        try:
            put_response = self.api.request("PUT", PRESET_SELECTED_PATH, arguments)
        except ApiHttpError as exc:
            typed_error = self._typed_preset_http_error(exc)
            status = exc.details.get("status")
            if self._is_authoritative_put_http_error(exc):
                if typed_error is not None:
                    raise typed_error
                body = exc.details.get("body")
                body_error = body.get("error") if isinstance(body, dict) else None
                if isinstance(body_error, dict) and body_error.get("code") == "mutation_outcome_unknown":
                    self._raise_preset_outcome_unknown(arguments, exc)
                raise exc
            if (typed_error is not None and isinstance(status, int) and 500 <= status < 600
                    and typed_error.code in DEFINITIVE_PRESET_5XX_ERROR_CODES):
                raise typed_error
            self._raise_preset_outcome_unknown(arguments, exc)
        except ToolError as exc:
            self._raise_preset_outcome_unknown(arguments, exc)
        try:
            put_result = self._validate_preset_payload("oe_set_electrode_preset", put_response)
        except ToolError as exc:
            self._raise_preset_outcome_unknown(arguments, exc)
        expected_after = {
            "processor_id": before["processor_id"], "probe_id": before["probe_id"],
            "probe_serial": before["probe_serial"], "preset_id": desired["preset_id"],
            "preset_label": desired["preset_label"],
            "electrode_map_hash": desired["electrode_map_hash"],
        }
        valid_noop = put_result["changed"] is True or put_result["before"] == expected_after
        if (put_result["requested"] != arguments or put_result["after"] != expected_after or not valid_noop):
            self._raise_preset_outcome_unknown(
                arguments, ToolError("response_schema_mismatch", "Open Ephys preset PUT response did not match the requested transaction."))

        deadline, after = time.monotonic() + PRESET_CONVERGENCE_SECONDS, None
        obtained_readback = last_readback_was_authoritative = False
        while True:
            try:
                fresh_inventory = self._read_preset_inventory()
            except ToolError:
                last_readback_was_authoritative = False
                if time.monotonic() >= deadline:
                    break
                time.sleep(PRESET_POLL_SECONDS)
                continue
            obtained_readback = last_readback_was_authoritative = True
            try:
                after = self._preset_readback(self._resolve_preset_target(fresh_inventory, arguments))
            except ToolError as exc:
                raise ToolError("postcondition_failed", "Probe identity was not uniquely present during authoritative preset readback.", requested=arguments, readback_error=exc.payload()["error"]) from exc
            if fresh_inventory["inventory_generation"] != arguments["expected_inventory_generation"]:
                raise ToolError("postcondition_failed", "Preset inventory generation changed while the mutation was converging.", requested=arguments, actual=after, actual_inventory_generation=fresh_inventory["inventory_generation"])
            if (all(after[field] == desired[field] for field in ("preset_id", "preset_label", "electrode_map_hash"))
                    and all(after[field] == before[field] for field in ("processor_id", "probe_id", "probe_serial"))):
                result = {"changed": True, "before": before, "requested": arguments, "after": after}
                return self._validate_preset_payload("oe_set_electrode_preset", result)
            if time.monotonic() >= deadline:
                break
            time.sleep(PRESET_POLL_SECONDS)
        if not obtained_readback or not last_readback_was_authoritative:
            self._raise_preset_outcome_unknown(arguments, ToolError("response_schema_mismatch", "Authoritative preset readback was unavailable at the convergence deadline."))
        raise ToolError("postcondition_failed", "Preset mutation did not converge to authoritative readback.", requested=arguments, actual=after)

    def _execute_tool(self, name: str, arguments: dict[str, Any]) -> Any:
        if self.contract_version == PRESET_CONTRACT_VERSION:
            descriptor = self._remote_capabilities()
            capabilities = self._last_capabilities
        else:
            descriptor = None
            capabilities = self._verify_capabilities()
        if name == "oe_get_capabilities": return capabilities
        if name in PRESET_TOOL_NAMES and descriptor is None:
            raise ToolError("capability_unavailable", "The exact remote Neuropixels preset capability is unavailable.")
        if name == "oe_get_electrode_presets": return self._read_preset_inventory()
        if name == "oe_set_electrode_preset":
            return self._set_electrode_preset(validate_status(self.api.request("GET", "/api/status")), arguments)
        if name == "oe_get_status": return validate_status(self.api.request("GET", "/api/status"))
        if name == "oe_get_recording_options": return validate_options(self.api.request("GET", "/api/recording/options"))
        if name == "oe_get_recording_filename": return filename_projection(self.api.request("GET", "/api/recording"))
        if name == "oe_get_recording_directory": return directory_projection(self.api.request("GET", "/api/recording"))
        if name == "oe_get_config": return validate_config(self.api.request("GET", "/api/config"))
        if name == "oe_get_processors": return processors_projection(self.api.request("GET", "/api/processors"))
        if name == "oe_get_cpu": return validate_cpu(self.api.request("GET", "/api/cpu"))
        if name == "oe_get_disk": return validate_disk(self.api.request("GET", "/api/disk"))
        if name == "oe_get_time": return validate_time(self.api.request("GET", "/api/time"))
        if name == "oe_set_status":
            before, desired = validate_status(self.api.request("GET", "/api/status")), arguments["mode"]
            if desired == "RECORD" and arguments.get("approve_recording") is not True:
                raise ToolError("recording_approval_required", "Entering RECORD requires approve_recording=true in this call.")
            requested = {"mode": desired}
            try:
                response = validate_status(self.api.request("PUT", "/api/status", requested))
            except ApiHttpError as exc:
                if self._is_authoritative_put_http_error(exc):
                    raise
                self._raise_mutation_outcome_unknown(requested, exc, "/api/status", validate_status)
            except ToolError as exc:
                self._raise_mutation_outcome_unknown(requested, exc, "/api/status", validate_status)
            after = validate_status(self.api.request("GET", "/api/status"))
            if response["mode"] != desired or after["mode"] != desired:
                raise ToolError("postcondition_failed", "Open Ephys did not confirm the requested status after mutation.", requested_mode=desired, actual_mode=after["mode"])
            return {"before": before, "requested": requested, "after": after}
        if name == "oe_set_recording_options":
            before = validate_options(self.api.request("GET", "/api/recording/options"))
            try:
                response = validate_options(self.api.request("PUT", "/api/recording/options", arguments), mutation_response=True)
            except ApiHttpError as exc:
                if self._is_authoritative_put_http_error(exc):
                    raise
                self._raise_mutation_outcome_unknown(arguments, exc, "/api/recording/options", validate_options)
            except ToolError as exc:
                self._raise_mutation_outcome_unknown(arguments, exc, "/api/recording/options", validate_options)
            after = validate_options(self.api.request("GET", "/api/recording/options"))
            expected = {**before, **arguments}
            if response != expected or after != expected:
                raise ToolError("postcondition_failed", "Open Ephys did not confirm the requested recording option after mutation.", requested=arguments, actual=after)
            return {"before": before, "requested": arguments, "after": after}
        if name == "oe_set_recording_filename":
            before = filename_projection(self.api.request("GET", "/api/recording"))
            try:
                response = filename_projection(self.api.request("PUT", "/api/recording", arguments))
            except ApiHttpError as exc:
                if self._is_authoritative_put_http_error(exc):
                    raise
                self._raise_mutation_outcome_unknown(arguments, exc, "/api/recording", filename_projection)
            except ToolError as exc:
                self._raise_mutation_outcome_unknown(arguments, exc, "/api/recording", filename_projection)
            after = filename_projection(self.api.request("GET", "/api/recording"))
            expected = {**before, **arguments}
            if response != expected or after != expected:
                raise ToolError("postcondition_failed", "Open Ephys did not confirm the requested recording filename after mutation.", requested=arguments, actual=after)
            return {"before": before, "requested": arguments, "after": after}
        if name == "oe_set_recording_directory":
            submitted = {"parent_directory": normalize_windows_directory(arguments["parent_directory"])}
            if validate_status(self.api.request("GET", "/api/status"))["mode"] == "RECORD":
                raise ToolError(
                    "recording_active",
                    "Recording directory cannot be changed while Open Ephys is recording.",
                    requested=arguments,
                    submitted=submitted,
                )
            before = directory_projection(self.api.request("GET", "/api/recording"))
            try:
                response = directory_projection(self.api.request("PUT", "/api/recording", submitted))
            except ApiHttpError as exc:
                if self._is_authoritative_put_http_error(exc):
                    raise ToolError(exc.code, exc.message, **exc.details, requested=arguments, submitted=submitted)
                self._raise_mutation_outcome_unknown(
                    arguments, exc, "/api/recording", directory_projection, submitted=submitted
                )
            except ToolError as exc:
                self._raise_mutation_outcome_unknown(
                    arguments, exc, "/api/recording", directory_projection, submitted=submitted
                )
            after = directory_projection(self.api.request("GET", "/api/recording"))
            expected_path = submitted["parent_directory"]
            if (not windows_paths_equivalent(response["parent_directory"], expected_path)
                    or not windows_paths_equivalent(after["parent_directory"], expected_path)):
                raise ToolError(
                    "postcondition_failed",
                    "Open Ephys did not confirm the requested recording directory after mutation.",
                    requested=arguments,
                    submitted=submitted,
                    actual=after,
                )
            return {"before": before, "requested": arguments, "submitted": submitted, "after": after}
        raise JsonRpcError(INVALID_PARAMS, "Unknown tool.")


def decode_stdio_line(line: str) -> Any:
    try: return loads_json(line)
    except (ValueError, json.JSONDecodeError) as exc: raise JsonRpcError(PARSE_ERROR, "Parse error") from exc


def run_stdio(server: McpServer) -> None:
    for line in sys.stdin:
        if not line.strip(): continue
        try: response = server.handle(decode_stdio_line(line))
        except JsonRpcError as exc: response = {"jsonrpc": "2.0", "id": None, "error": {"code": exc.code, "message": exc.message}}
        if response is not None:
            sys.stdout.write(dumps_json(response) + "\n"); sys.stdout.flush()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path, default=DEFAULT_ACTIVE_CONTRACT); parser.add_argument("--base-url")
    args = parser.parse_args(argv)
    try: server = McpServer(args.contract, args.base_url)
    except (OSError, ValueError) as exc:
        print(f"open-ephys-mcp startup error: {exc}", file=sys.stderr); return 2
    run_stdio(server); return 0


if __name__ == "__main__": raise SystemExit(main())
