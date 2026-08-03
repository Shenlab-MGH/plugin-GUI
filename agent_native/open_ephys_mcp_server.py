#!/usr/bin/env python3
"""Minimal legacy MCP bridge for the Open Ephys v1.1.0 r0.1.0 core."""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


PROTOCOL_VERSION = "2024-11-05"
CONTRACT_VERSION = "r0.1.0"
DEFAULT_CONTRACT = Path(__file__).with_name("open_ephys_agent_contract_v1_1_0_r0_1_0.json")
EXPECTED_TOOL_NAMES = [
    "oe_get_capabilities",
    "oe_get_status",
    "oe_set_status",
    "oe_get_recording_filename",
    "oe_set_recording_filename",
    "oe_get_cpu",
]
ALLOWED_MODES = {"IDLE", "ACQUIRE", "RECORD"}
FILENAME_FIELDS = ("prepend_text", "base_text", "append_text")

PARSE_ERROR = -32700
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602
INTERNAL_ERROR = -32603
SERVER_NOT_INITIALIZED = -32002


class JsonRpcError(Exception):
    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


class ToolError(Exception):
    def __init__(self, code: str, message: str, **details: Any):
        super().__init__(message)
        self.code = code
        self.message = message
        self.details = details

    def payload(self) -> dict[str, Any]:
        error = {"code": self.code, "message": self.message, **self.details}
        return {"ok": False, "error": error}


class ApiHttpError(ToolError):
    def __init__(self, status: int | None, body: Any):
        super().__init__(
            "api_http_error",
            "Open Ephys API request failed.",
            status=status,
            body=body,
        )


def load_contract(path: Path) -> dict[str, Any]:
    try:
        contract = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"Cannot load contract {path}: {exc}") from exc

    expected = {
        "schema_version": CONTRACT_VERSION,
        "contract_version": CONTRACT_VERSION,
        "bundle_version": CONTRACT_VERSION,
        "baseline_version": "1.1.0",
        "protocol_version": PROTOCOL_VERSION,
        "modern_protocol_supported": False,
        "capabilities_contract_version": "0.1.1",
        "tool_names": EXPECTED_TOOL_NAMES,
    }
    actual = {
        "schema_version": contract.get("schema_version"),
        "contract_version": (contract.get("contract") or {}).get("version"),
        "bundle_version": (contract.get("bundle") or {}).get("version"),
        "baseline_version": (contract.get("baseline") or {}).get("version"),
        "protocol_version": (contract.get("mcp") or {}).get("protocol_version"),
        "modern_protocol_supported": (contract.get("mcp") or {}).get("modern_protocol_supported"),
        "capabilities_contract_version": (contract.get("api") or {}).get("capabilities_contract_version"),
        "tool_names": [tool.get("name") for tool in contract.get("tools", []) if isinstance(tool, dict)],
    }
    if actual != expected:
        raise ValueError(f"Contract pins do not match the r0.1.0 server: {actual!r}")

    capabilities = (contract.get("api") or {}).get("expected_capabilities_response")
    if not isinstance(capabilities, dict):
        raise ValueError("Contract is missing api.expected_capabilities_response.")
    if capabilities.get("contract_version") != "0.1.1":
        raise ValueError("Capability fixture must pin API contract 0.1.1.")
    return contract


class ApiClient:
    def __init__(self, base_url: str, timeout: float = 3.0):
        parsed = urllib.parse.urlsplit(base_url)
        if parsed.scheme != "http" or parsed.hostname not in {"127.0.0.1", "localhost"}:
            raise ValueError("Open Ephys API base URL must be loopback HTTP.")
        if parsed.query or parsed.fragment or parsed.path not in {"", "/"}:
            raise ValueError("Open Ephys API base URL must not contain a path, query, or fragment.")
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def request(self, method: str, path: str, body: dict[str, Any] | None = None) -> Any:
        data = None
        headers = {"Accept": "application/json"}
        if body is not None:
            data = json.dumps(body, separators=(",", ":")).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(
            self.base_url + path,
            data=data,
            headers=headers,
            method=method,
        )
        try:
            with self.opener.open(request, timeout=self.timeout) as response:
                payload = response.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            raw = exc.read().decode("utf-8", errors="replace")
            try:
                body_payload: Any = json.loads(raw)
            except json.JSONDecodeError:
                body_payload = raw
            raise ApiHttpError(exc.code, body_payload) from exc
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            raise ApiHttpError(None, str(exc)) from exc

        try:
            return json.loads(payload)
        except json.JSONDecodeError as exc:
            raise ToolError(
                "response_schema_mismatch",
                "Open Ephys API returned non-JSON content.",
                path=path,
            ) from exc


def text_result(payload: Any, *, is_error: bool = False) -> dict[str, Any]:
    result: dict[str, Any] = {
        "content": [{"type": "text", "text": json.dumps(payload, separators=(",", ":"), sort_keys=True)}]
    }
    if is_error:
        result["isError"] = True
    return result


def _expect_object(payload: Any, name: str) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise ToolError("response_schema_mismatch", f"{name} response must be a JSON object.")
    return payload


def validate_status(payload: Any) -> dict[str, str]:
    value = _expect_object(payload, "Status")
    if set(value) != {"mode"} or value.get("mode") not in ALLOWED_MODES:
        raise ToolError(
            "response_schema_mismatch",
            "Status response must contain only mode with IDLE, ACQUIRE, or RECORD.",
        )
    return {"mode": value["mode"]}


def validate_recording(payload: Any) -> dict[str, Any]:
    value = _expect_object(payload, "Recording")
    required_strings = {
        "parent_directory", "prepend_text", "base_text", "append_text", "default_record_engine"
    }
    if not required_strings.issubset(value):
        raise ToolError("response_schema_mismatch", "Recording response is missing required fields.")
    if any(not isinstance(value[field], str) for field in required_strings):
        raise ToolError("response_schema_mismatch", "Recording text fields must be strings.")
    if not isinstance(value.get("record_nodes"), list):
        raise ToolError("response_schema_mismatch", "Recording record_nodes must be an array.")
    return value


def filename_projection(payload: Any) -> dict[str, str]:
    recording = validate_recording(payload)
    return {field: recording[field] for field in FILENAME_FIELDS}


def validate_cpu(payload: Any) -> dict[str, float]:
    value = _expect_object(payload, "CPU")
    usage = value.get("usage")
    if set(value) != {"usage"} or isinstance(usage, bool) or not isinstance(usage, (int, float)):
        raise ToolError("response_schema_mismatch", "CPU response must contain only numeric usage.")
    if not 0.0 <= float(usage) <= 1.0:
        raise ToolError("response_schema_mismatch", "CPU usage must be between 0.0 and 1.0.")
    return {"usage": usage}


class McpServer:
    def __init__(self, contract_path: Path = DEFAULT_CONTRACT, base_url: str | None = None):
        self.contract_path = Path(contract_path)
        self.contract = load_contract(self.contract_path)
        configured_url = self.contract["transport"]["http_base_url"]
        self.api = ApiClient(base_url or configured_url)
        self.connection_state = "new"

    def handle(self, request: Any) -> dict[str, Any] | None:
        request_id = request.get("id") if isinstance(request, dict) else None
        try:
            self._validate_request(request)
            method = request["method"]
            params = request.get("params", {})

            if method == "initialize":
                result = self._initialize(params)
            elif method == "notifications/initialized":
                if self.connection_state != "initialize_responded":
                    raise JsonRpcError(INVALID_REQUEST, "Unexpected initialized notification.")
                self.connection_state = "ready"
                return None
            elif method == "tools/list":
                self._require_ready()
                result = {"tools": self.contract["tools"]}
            elif method == "tools/call":
                self._require_ready()
                result = self._call_tool(params)
            else:
                raise JsonRpcError(METHOD_NOT_FOUND, "Method not found")
        except JsonRpcError as exc:
            if request_id is None:
                return None
            return {"jsonrpc": "2.0", "id": request_id, "error": {"code": exc.code, "message": exc.message}}
        except Exception as exc:  # Defensive JSON-RPC boundary; diagnostics never go to stdout.
            print(f"open-ephys-mcp internal error: {exc}", file=sys.stderr)
            if request_id is None:
                return None
            return {
                "jsonrpc": "2.0",
                "id": request_id,
                "error": {"code": INTERNAL_ERROR, "message": "Internal error"},
            }
        return {"jsonrpc": "2.0", "id": request_id, "result": result}

    @staticmethod
    def _validate_request(request: Any) -> None:
        if not isinstance(request, dict) or request.get("jsonrpc") != "2.0":
            raise JsonRpcError(INVALID_REQUEST, "Invalid Request")
        if not isinstance(request.get("method"), str):
            raise JsonRpcError(INVALID_REQUEST, "Invalid Request")
        if "params" in request and not isinstance(request["params"], dict):
            raise JsonRpcError(INVALID_PARAMS, "params must be an object")

    def _initialize(self, params: dict[str, Any]) -> dict[str, Any]:
        if self.connection_state != "new":
            raise JsonRpcError(INVALID_REQUEST, "Server is already initialized.")
        if (
            params.get("protocolVersion") != PROTOCOL_VERSION
            or not isinstance(params.get("capabilities"), dict)
            or not isinstance(params.get("clientInfo"), dict)
        ):
            raise JsonRpcError(INVALID_PARAMS, "Invalid legacy initialize parameters.")
        self.connection_state = "initialize_responded"
        return {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {"tools": {}},
            "serverInfo": {
                "name": self.contract["mcp"]["server_name"],
                "version": CONTRACT_VERSION,
            },
        }

    def _require_ready(self) -> None:
        if self.connection_state != "ready":
            raise JsonRpcError(SERVER_NOT_INITIALIZED, "Server not initialized")

    def _call_tool(self, params: dict[str, Any]) -> dict[str, Any]:
        name = params.get("name")
        arguments = params.get("arguments", {})
        if name not in EXPECTED_TOOL_NAMES or not isinstance(arguments, dict):
            raise JsonRpcError(INVALID_PARAMS, "Unknown tool or invalid arguments object.")
        try:
            self._validate_arguments(name, arguments)
            payload = self._execute_tool(name, arguments)
            return text_result(payload)
        except ToolError as exc:
            return text_result(exc.payload(), is_error=True)

    @staticmethod
    def _validate_arguments(name: str, arguments: dict[str, Any]) -> None:
        if name in {"oe_get_capabilities", "oe_get_status", "oe_get_recording_filename", "oe_get_cpu"}:
            if arguments:
                raise ToolError("invalid_arguments", f"{name} does not accept arguments.")
            return
        if name == "oe_set_status":
            if not set(arguments).issubset({"mode", "approve_recording"}):
                raise ToolError("invalid_arguments", "oe_set_status received an unknown argument.")
            if not isinstance(arguments.get("mode"), str) or arguments["mode"] not in ALLOWED_MODES:
                raise ToolError("invalid_arguments", "mode must be IDLE, ACQUIRE, or RECORD.")
            if "approve_recording" in arguments and not isinstance(arguments["approve_recording"], bool):
                raise ToolError("invalid_arguments", "approve_recording must be boolean.")
            return
        if name == "oe_set_recording_filename":
            if not arguments or not set(arguments).issubset(FILENAME_FIELDS):
                raise ToolError("invalid_arguments", "Provide at least one supported filename field.")
            if any(not isinstance(value, str) for value in arguments.values()):
                raise ToolError("invalid_arguments", "Recording filename fields must be strings.")

    def _verify_capabilities(self) -> dict[str, Any]:
        actual = self.api.request("GET", "/api/capabilities")
        expected = self.contract["api"]["expected_capabilities_response"]
        if actual != expected:
            raise ToolError(
                "capability_contract_mismatch",
                "Open Ephys capabilities do not exactly match the pinned r0.1.0 contract.",
            )
        return actual

    def _execute_tool(self, name: str, arguments: dict[str, Any]) -> Any:
        capabilities = self._verify_capabilities()
        if name == "oe_get_capabilities":
            return capabilities
        if name == "oe_get_status":
            return validate_status(self.api.request("GET", "/api/status"))
        if name == "oe_get_recording_filename":
            return filename_projection(self.api.request("GET", "/api/recording"))
        if name == "oe_get_cpu":
            return validate_cpu(self.api.request("GET", "/api/cpu"))
        if name == "oe_set_status":
            before = validate_status(self.api.request("GET", "/api/status"))
            requested_mode = arguments["mode"]
            if (
                requested_mode == "RECORD"
                and before["mode"] != "RECORD"
                and arguments.get("approve_recording") is not True
            ):
                raise ToolError(
                    "recording_approval_required",
                    "Entering RECORD requires approve_recording=true.",
                )
            response = validate_status(self.api.request("PUT", "/api/status", {"mode": requested_mode}))
            after = validate_status(self.api.request("GET", "/api/status"))
            if response["mode"] != requested_mode or after["mode"] != requested_mode:
                raise ToolError(
                    "postcondition_failed",
                    "Open Ephys did not confirm the requested status after mutation.",
                    requested_mode=requested_mode,
                    actual_mode=after["mode"],
                )
            return {"before": before, "requested": {"mode": requested_mode}, "after": after}
        if name == "oe_set_recording_filename":
            before = filename_projection(self.api.request("GET", "/api/recording"))
            response = filename_projection(self.api.request("PUT", "/api/recording", arguments))
            after = filename_projection(self.api.request("GET", "/api/recording"))
            expected = {**before, **arguments}
            if response != expected or after != expected:
                raise ToolError(
                    "postcondition_failed",
                    "Open Ephys did not confirm the requested recording filename after mutation.",
                    requested=arguments,
                    actual=after,
                )
            return {"before": before, "requested": arguments, "after": after}
        raise JsonRpcError(INVALID_PARAMS, "Unknown tool.")


def decode_stdio_line(line: str) -> Any:
    try:
        return json.loads(line)
    except json.JSONDecodeError as exc:
        raise JsonRpcError(PARSE_ERROR, "Parse error") from exc


def run_stdio(server: McpServer) -> None:
    for line in sys.stdin:
        if not line.strip():
            continue
        try:
            request = decode_stdio_line(line)
            response = server.handle(request)
        except JsonRpcError as exc:
            response = {"jsonrpc": "2.0", "id": None, "error": {"code": exc.code, "message": exc.message}}
        if response is not None:
            sys.stdout.write(json.dumps(response, separators=(",", ":")) + "\n")
            sys.stdout.flush()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT)
    parser.add_argument("--base-url")
    args = parser.parse_args(argv)
    try:
        server = McpServer(args.contract, args.base_url)
    except (OSError, ValueError) as exc:
        print(f"open-ephys-mcp startup error: {exc}", file=sys.stderr)
        return 2
    run_stdio(server)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
