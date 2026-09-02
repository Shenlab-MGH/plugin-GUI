import json
import unittest

from agent_native import open_ephys_mcp_server as mcp


class FakeApi:
    def __init__(self, capabilities, config):
        self.capabilities = capabilities
        self.config = config
        self.requests = []

    def request(self, method, path, body=None):
        self.requests.append((method, path, body))
        if (method, path) == ("GET", "/api/capabilities"):
            return self.capabilities
        if (method, path) == ("GET", "/api/config"):
            return self.config
        raise AssertionError(f"unexpected request: {(method, path, body)!r}")


class ConfigurationSnapshot003Tests(unittest.TestCase):
    def ready_server(self, config):
        server = mcp.McpServer()
        server.api = FakeApi(
            server.contract["api"]["expected_capabilities_response"], config
        )
        server.connection_state = "ready"
        return server

    @staticmethod
    def call_config(server, arguments=None):
        response = server.handle(
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "tools/call",
                "params": {
                    "name": "oe_get_config",
                    "arguments": {} if arguments is None else arguments,
                },
            }
        )
        return response["result"], json.loads(response["result"]["content"][0]["text"])

    def test_get_config_returns_exact_settings_xml(self):
        xml = '<SETTINGS><SIGNALCHAIN name="agent" /></SETTINGS>'
        server = self.ready_server({"info": xml})

        result, payload = self.call_config(server)

        self.assertNotIn("isError", result)
        self.assertEqual(payload, {"info": xml})
        self.assertEqual(
            server.api.requests,
            [
                ("GET", "/api/capabilities", None),
                ("GET", "/api/config", None),
            ],
        )

    def test_get_config_rejects_non_settings_or_malformed_xml(self):
        for info in ("<ROOT />", "<SETTINGS>"):
            with self.subTest(info=info):
                server = self.ready_server({"info": info})

                result, payload = self.call_config(server)

                self.assertTrue(result["isError"])
                self.assertEqual(payload["error"]["code"], "response_schema_mismatch")

    def test_get_config_rejects_arguments_before_http(self):
        server = self.ready_server({"info": "<SETTINGS />"})

        result, payload = self.call_config(server, {"unexpected": True})

        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "invalid_arguments")
        self.assertEqual(server.api.requests, [])


if __name__ == "__main__":
    unittest.main()
