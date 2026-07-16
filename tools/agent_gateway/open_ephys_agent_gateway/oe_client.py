import json
import time
import urllib.error
import urllib.request


class OpenEphysError(RuntimeError):
    pass


class OpenEphysClient:
    def __init__(
        self,
        base_url="http://127.0.0.1:37497",
        timeout_seconds=2.0,
    ):
        self.base_url = base_url.rstrip("/")
        self.timeout_seconds = timeout_seconds

    def _request(self, path, method="GET", body=None):
        encoded = None
        headers = {"Accept": "application/json"}
        if body is not None:
            encoded = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(
            self.base_url + path,
            method=method,
            data=encoded,
            headers=headers,
        )
        try:
            with urllib.request.urlopen(
                request,
                timeout=self.timeout_seconds,
            ) as response:
                return json.load(response)
        except (
            urllib.error.URLError,
            TimeoutError,
            json.JSONDecodeError,
        ) as error:
            raise OpenEphysError(str(error)) from error

    def get_mode(self):
        payload = self._request("/api/status")
        return str(payload.get("mode", "UNKNOWN")).upper()

    def get_recording(self):
        return self._request("/api/recording")

    def get_config_xml(self):
        payload = self._request("/api/config")
        return str(payload.get("info", ""))

    def set_mode(self, mode):
        self._request(
            "/api/status",
            method="PUT",
            body={"mode": mode},
        )

    def wait_for_mode(
        self,
        expected,
        timeout_seconds=5.0,
        poll_seconds=0.1,
    ):
        deadline = time.monotonic() + timeout_seconds
        last = "UNKNOWN"
        while time.monotonic() < deadline:
            last = self.get_mode()
            if last == expected:
                return last
            time.sleep(poll_seconds)
        return last

