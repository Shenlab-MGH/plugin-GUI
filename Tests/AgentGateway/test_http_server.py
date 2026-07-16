import json
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from open_ephys_agent_gateway.audit import JsonlAuditLog
from open_ephys_agent_gateway.http_server import create_server
from open_ephys_agent_gateway.service import AgentGatewayService

from test_gateway_core import FakeOpenEphysClient


class HttpServerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.service = AgentGatewayService(
            FakeOpenEphysClient(),
            JsonlAuditLog(Path(self.temporary.name) / "audit.jsonl"),
            mutation_allowed=False,
            mutation_disabled_reason="NATIVE_API_EXPOSED",
        )
        self.server = create_server(
            self.service,
            host="127.0.0.1",
            port=0,
            token="test-token",
        )
        self.thread = threading.Thread(
            target=self.server.serve_forever,
            daemon=True,
        )
        self.thread.start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)
        self.base = (
            f"http://127.0.0.1:{self.server.server_address[1]}"
        )

    def request(self, path, method="GET", body=None, token="test-token"):
        data = None if body is None else json.dumps(body).encode()
        request = urllib.request.Request(
            self.base + path,
            method=method,
            data=data,
            headers={
                "Authorization": f"Bearer {token}",
                "Content-Type": "application/json",
            },
        )
        try:
            with urllib.request.urlopen(request, timeout=2) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, json.load(error)

    def test_requires_bearer_token(self):
        status, body = self.request("/v1/status", token="wrong")
        self.assertEqual(status, 401)
        self.assertEqual(body["error"], "UNAUTHORIZED")

    def test_dashboard_is_available_but_does_not_embed_token(self):
        with urllib.request.urlopen(self.base + "/", timeout=2) as response:
            html = response.read().decode("utf-8")
        self.assertIn("Open Ephys Agent Gateway", html)
        self.assertNotIn("test-token", html)

    def test_status_and_fail_closed_request_are_real_http(self):
        status_code, status = self.request("/v1/status")
        request_code, receipt = self.request(
            "/v1/transport/requests",
            method="POST",
            body={
                "request_id": "http-blocked-1",
                "target_mode": "ACQUIRE",
                "expected_revision": status["revision"],
            },
        )
        query_code, queried = self.request(
            "/v1/transport/requests/http-blocked-1"
        )

        self.assertEqual(status_code, 200)
        self.assertEqual(request_code, 409)
        self.assertEqual(query_code, 200)
        self.assertEqual(receipt["reason"], "NATIVE_API_EXPOSED")
        self.assertEqual(queried, receipt)


if __name__ == "__main__":
    unittest.main()
