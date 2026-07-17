import json
import threading
from collections.abc import Iterator
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pytest

from open_ephys_agent_mcp.config import Settings
from open_ephys_agent_mcp.native_client import NativeClient, NativeClientError


TOKEN = "t" * 32
VALID_STATUS = {
    "schema_version": "oe-agent-control-preview/v0.0.1",
    "backend": "in-process-agent-endpoint",
    "session_id": "0123456789abcdef",
    "online": True,
    "mutation_allowed": False,
    "mutation_disabled_reason": "NOT_ARMED",
    "phase": "READY",
    "gui_version": "1.0.2",
    "mode": "IDLE",
    "revision": 7,
}


@contextmanager
def fake_status_server(
    body: bytes,
    *,
    status: int = 200,
) -> Iterator[tuple[str, list[str]]]:
    authorizations: list[str] = []

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            authorizations.append(self.headers.get("Authorization", ""))
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format: str, *args: object) -> None:
            return

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{server.server_port}", authorizations
    finally:
        server.shutdown()
        thread.join(timeout=5)
        server.server_close()


def client_for(url: str) -> NativeClient:
    return NativeClient(Settings(base_url=url, bearer_token=TOKEN))


def test_reads_and_strictly_validates_authenticated_status() -> None:
    with fake_status_server(json.dumps(VALID_STATUS).encode()) as (url, auth):
        status = client_for(url).get_status()

    assert status.mode == "IDLE"
    assert status.revision == 7
    assert auth == [f"Bearer {TOKEN}"]


@pytest.mark.parametrize(
    ("body", "code"),
    [
        (b"not-json", "INVALID_JSON"),
        (
            json.dumps({**VALID_STATUS, "unexpected": True}).encode(),
            "INVALID_NATIVE_STATUS",
        ),
        (
            json.dumps({**VALID_STATUS, "mode": "TOGGLE"}).encode(),
            "INVALID_NATIVE_STATUS",
        ),
        (b"x" * 65_537, "RESPONSE_TOO_LARGE"),
    ],
    ids=["invalid-json", "unknown-field", "invalid-mode", "oversized"],
)
def test_rejects_invalid_or_oversized_status(body: bytes, code: str) -> None:
    with fake_status_server(body) as (url, _):
        with pytest.raises(NativeClientError) as caught:
            client_for(url).get_status()

    assert caught.value.code == code


def test_maps_http_failure_without_leaking_token() -> None:
    with fake_status_server(b'{"error":"UNAUTHORIZED"}', status=401) as (url, _):
        with pytest.raises(NativeClientError) as caught:
            client_for(url).get_status()

    assert caught.value.code == "NATIVE_HTTP_ERROR"
    assert TOKEN not in str(caught.value)
