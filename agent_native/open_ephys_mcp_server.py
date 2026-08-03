#!/usr/bin/env python3
"""Narrow legacy MCP bridge for the Open Ephys v1.0.2 Core R0 contract."""

from __future__ import annotations

import argparse
import json
import math
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


PROTOCOL_VERSION = "2024-11-05"
CONTRACT_VERSION = "r0.1.0"
DEFAULT_CONTRACT = Path(__file__).with_name("open_ephys_agent_contract_v1_0_2_r0_1_0.json")
TOOL_NAMES = (
    "oe_get_capabilities", "oe_get_status", "oe_set_status",
    "oe_get_recording_options", "oe_set_recording_options",
    "oe_get_recording_filename", "oe_set_recording_filename",
    "oe_get_cpu", "oe_get_disk", "oe_get_time",
)
CAPABILITY_IDS = (
    "oe.control.acquisition", "oe.control.recording", "oe.control.recording.options",
    "oe.control.recording.filename", "oe.control.recording.new_directory",
    "oe.control.recording.force_new_directory", "oe.status.cpu_usage",
    "oe.status.disk_usage", "oe.status.elapsed_time",
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
    expected = {
        "schema_version": CONTRACT_VERSION,
        "contract": {"id": "open-ephys-agent", "version": CONTRACT_VERSION},
        "bundle": {"id": "open-ephys-agent-native", "version": CONTRACT_VERSION},
        "baseline": {"upstream": "open-ephys/plugin-GUI", "version": "1.0.2", "commit": "c91afebcfb0678a667fb93f6312ed33c56ec640f"},
        "mcp": {"protocol_version": PROTOCOL_VERSION, "modern_protocol_supported": False, "server_name": "open-ephys-agent-native"},
    }
    if any(contract.get(key) != value for key, value in expected.items()):
        raise ValueError("Contract pins do not match the v1.0.2 Core R0 server.")
    tools = contract.get("tools")
    if not isinstance(tools, list) or [tool.get("name") for tool in tools if isinstance(tool, dict)] != list(TOOL_NAMES):
        raise ValueError("Contract tool names do not match the narrow Core R0 surface.")
    for tool in tools:
        schema = tool.get("inputSchema") if isinstance(tool, dict) else None
        if not isinstance(schema, dict) or schema.get("additionalProperties") is not False:
            raise ValueError("Every Core R0 tool requires a closed input schema.")
    api = contract.get("api")
    capabilities = api.get("expected_capabilities_response") if isinstance(api, dict) else None
    if not isinstance(capabilities, dict) or capabilities.get("contract_version") != "0.1.1":
        raise ValueError("Capability fixture must pin API contract 0.1.1.")
    items = capabilities.get("capabilities")
    if not isinstance(items, list) or [item.get("id") for item in items if isinstance(item, dict)] != list(CAPABILITY_IDS):
        raise ValueError("Capability fixture does not match the exact nine Core R0 capabilities.")
    if contract.get("verification") != {"hardware_verified": False, "scientific_verified": False}:
        raise ValueError("Contract must explicitly retain unverified hardware and scientific claims.")
    return contract


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
    suffixes = {*(str(i) for i in range(1, 10)), "¹", "²", "³"}
    reserved = {"CON", "PRN", "AUX", "NUL", *{f"COM{x}" for x in suffixes}, *{f"LPT{x}" for x in suffixes}}
    if value.split(".", 1)[0].upper() in reserved:
        raise ToolError("invalid_arguments", "Filename component is a reserved Windows device name.")


class McpServer:
    def __init__(self, contract_path: Path = DEFAULT_CONTRACT, base_url: str | None = None):
        self.contract = load_contract(Path(contract_path))
        self.api = ApiClient(base_url or self.contract["transport"]["http_base_url"])
        self.connection_state = "new"

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
        return {"protocolVersion": PROTOCOL_VERSION, "capabilities": {"tools": {}}, "serverInfo": {"name": self.contract["mcp"]["server_name"], "version": CONTRACT_VERSION}}

    def _require_ready(self) -> None:
        if self.connection_state != "ready": raise JsonRpcError(SERVER_NOT_INITIALIZED, "Server not initialized")

    def _call_tool(self, params: dict[str, Any]) -> dict[str, Any]:
        if (not {"name"}.issubset(params) or not set(params).issubset({"name", "arguments", "_meta"})
                or params.get("name") not in TOOL_NAMES or not isinstance(params.get("arguments", {}), dict)
                or ("_meta" in params and not isinstance(params["_meta"], dict))):
            raise JsonRpcError(INVALID_PARAMS, "Unknown tool or invalid arguments object.")
        try:
            name, arguments = params["name"], params.get("arguments", {})
            self._validate_arguments(name, arguments)
            return text_result(self._execute_tool(name, arguments))
        except ToolError as exc:
            return text_result(exc.payload(), is_error=True)

    @staticmethod
    def _validate_arguments(name: str, arguments: dict[str, Any]) -> None:
        if name in {"oe_get_capabilities", "oe_get_status", "oe_get_recording_options", "oe_get_recording_filename", "oe_get_cpu", "oe_get_disk", "oe_get_time"}:
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

    def _verify_capabilities(self) -> dict[str, Any]:
        actual = self.api.request("GET", "/api/capabilities")
        expected = self.contract["api"]["expected_capabilities_response"]
        if actual != expected:
            raise ToolError("capability_contract_mismatch", "Open Ephys capabilities do not exactly match the pinned Core R0 contract.")
        return actual

    def _raise_mutation_outcome_unknown(self, requested: dict[str, Any], put_error: ToolError, path: str, validator) -> None:
        details: dict[str, Any] = {"requested": requested, "put_error": put_error.payload()["error"]}
        try:
            details["observed"] = validator(self.api.request("GET", path))
        except ToolError as readback_error:
            details["readback_error"] = readback_error.payload()["error"]
        raise ToolError(
            "mutation_outcome_unknown",
            "Open Ephys may have committed the mutation, but the write response was not authoritative.",
            **details,
        )

    def _execute_tool(self, name: str, arguments: dict[str, Any]) -> Any:
        capabilities = self._verify_capabilities()
        if name == "oe_get_capabilities": return capabilities
        if name == "oe_get_status": return validate_status(self.api.request("GET", "/api/status"))
        if name == "oe_get_recording_options": return validate_options(self.api.request("GET", "/api/recording/options"))
        if name == "oe_get_recording_filename": return filename_projection(self.api.request("GET", "/api/recording"))
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
                if exc.details["status"] is not None:
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
                if exc.details["status"] is not None:
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
                if exc.details["status"] is not None:
                    raise
                self._raise_mutation_outcome_unknown(arguments, exc, "/api/recording", filename_projection)
            except ToolError as exc:
                self._raise_mutation_outcome_unknown(arguments, exc, "/api/recording", filename_projection)
            after = filename_projection(self.api.request("GET", "/api/recording"))
            expected = {**before, **arguments}
            if response != expected or after != expected:
                raise ToolError("postcondition_failed", "Open Ephys did not confirm the requested recording filename after mutation.", requested=arguments, actual=after)
            return {"before": before, "requested": arguments, "after": after}
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
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT); parser.add_argument("--base-url")
    args = parser.parse_args(argv)
    try: server = McpServer(args.contract, args.base_url)
    except (OSError, ValueError) as exc:
        print(f"open-ephys-mcp startup error: {exc}", file=sys.stderr); return 2
    run_stdio(server); return 0


if __name__ == "__main__": raise SystemExit(main())
