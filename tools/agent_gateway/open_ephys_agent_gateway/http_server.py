import hmac
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote

from .dashboard import DASHBOARD_HTML


class AgentGatewayHttpServer(ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address, handler, service, token):
        super().__init__(address, handler)
        self.service = service
        self.token = token
        self._audit_closed = False

    def server_close(self):
        if not self._audit_closed:
            self._audit_closed = True
            self.service.audit_event(
                "session_stopped",
                {"reason": "server_close"},
            )
        super().server_close()


class AgentGatewayHandler(BaseHTTPRequestHandler):
    server_version = "OpenEphysAgentGateway/0.0.1"

    def log_message(self, format_string, *args):
        return

    def _authorized(self):
        supplied = self.headers.get("Authorization", "")
        expected = f"Bearer {self.server.token}"
        return hmac.compare_digest(supplied, expected)

    def _send(self, status, payload):
        encoded = json.dumps(
            payload,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def _send_html(self, content):
        encoded = content.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header(
            "Content-Security-Policy",
            "default-src 'self'; style-src 'unsafe-inline'; "
            "script-src 'unsafe-inline'; connect-src 'self'",
        )
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def _authenticate(self):
        if self._authorized():
            return True
        self.server.service.audit_event(
            "authentication_failed",
            {
                "path": self.path,
                "client": self.client_address[0],
            },
        )
        self._send(401, {"error": "UNAUTHORIZED"})
        return False

    def do_GET(self):
        if self.path == "/":
            self._send_html(DASHBOARD_HTML)
            return
        if not self._authenticate():
            return
        if self.path == "/v1/status":
            self._send(200, self.server.service.status())
            return
        prefix = "/v1/transport/requests/"
        if self.path.startswith(prefix):
            request_id = unquote(self.path[len(prefix):])
            result = self.server.service.query(request_id)
            self._send(
                200 if result["state"] != "UNKNOWN" else 404,
                result,
            )
            return
        self._send(404, {"error": "NOT_FOUND"})

    def do_POST(self):
        if not self._authenticate():
            return
        if self.path != "/v1/transport/requests":
            self._send(404, {"error": "NOT_FOUND"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 16_384:
                raise ValueError("invalid body length")
            payload = json.loads(
                self.rfile.read(length).decode("utf-8")
            )
            if not isinstance(payload, dict):
                raise ValueError("body must be an object")
        except (ValueError, UnicodeDecodeError, json.JSONDecodeError):
            self._send(400, {"error": "INVALID_JSON"})
            return
        result = self.server.service.submit(payload)
        status = 200 if result["state"] == "COMPLETED" else 409
        self._send(status, result)


def create_server(service, *, host, port, token):
    if host not in {"127.0.0.1", "::1", "localhost"}:
        raise ValueError("Agent Gateway must bind to loopback")
    if not token or len(token) < 8:
        raise ValueError("A non-trivial bearer token is required")
    return AgentGatewayHttpServer(
        (host, port),
        AgentGatewayHandler,
        service,
        token,
    )
