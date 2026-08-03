import asyncio
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from mcp import StdioServerParameters, stdio_client
from mcp.client import Client

ROOT = Path(__file__).resolve().parents[2]
SERVER = ROOT / "agent_native" / "open_ephys_mcp_server.py"
EXPECTED = [
    "oe_get_capabilities", "oe_get_status", "oe_set_status",
    "oe_get_recording_filename", "oe_set_recording_filename", "oe_get_cpu",
]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        if self.path == "/api/capabilities":
            payload = json.loads((ROOT / "agent_native" / "open_ephys_agent_contract_v1_1_0_0_0_1.json").read_text(encoding="utf-8"))["api"]["expected_capabilities_response"]
        elif self.path == "/api/status":
            payload = {"mode": "IDLE"}
        else:
            self.send_error(404)
            return
        data = json.dumps(payload).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)


async def verify(base_url: str) -> None:
    params = StdioServerParameters(command=sys.executable, args=[str(SERVER), "--base-url", base_url], cwd=ROOT)
    async with Client(stdio_client(params), mode="auto") as client:
        tools = await client.list_tools()
        names = [tool.name for tool in tools.tools]
        assert names == EXPECTED, names
        result = await client.call_tool("oe_get_status", {})
        assert not result.is_error
        assert json.loads(result.content[0].text) == {"mode": "IDLE"}


def main() -> None:
    api = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=api.serve_forever, daemon=True); thread.start()
    try:
        asyncio.run(verify(f"http://127.0.0.1:{api.server_port}"))
    finally:
        api.shutdown(); api.server_close(); thread.join()
    print("official mcp 2.0.0 auto-mode stdio fallback verified")


if __name__ == "__main__":
    main()
