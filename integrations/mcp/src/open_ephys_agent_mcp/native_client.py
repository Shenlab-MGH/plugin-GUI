"""Bounded authenticated client for the fork's native status endpoint."""

from __future__ import annotations

import json
import urllib.error
import urllib.request

from pydantic import ValidationError

from .config import Settings
from .models import NativeStatus


class NativeClientError(RuntimeError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


class NativeClient:
    _MAX_RESPONSE_BYTES = 65_536

    def __init__(self, settings: Settings) -> None:
        self._base_url = settings.base_url.rstrip("/")
        self._bearer_token = settings.bearer_token

    def get_status(self) -> NativeStatus:
        request = urllib.request.Request(
            f"{self._base_url}/v1/status",
            method="GET",
            headers={
                "Accept": "application/json",
                "Authorization": f"Bearer {self._bearer_token}",
            },
        )
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                body = response.read(self._MAX_RESPONSE_BYTES + 1)
        except urllib.error.HTTPError as error:
            code = (
                "NATIVE_UNAUTHORIZED"
                if error.code in {401, 403}
                else "NATIVE_HTTP_ERROR"
            )
            raise NativeClientError(
                code, f"Native endpoint returned HTTP {error.code}."
            ) from error
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            raise NativeClientError(
                "NATIVE_UNAVAILABLE", "Native endpoint is unavailable."
            ) from error

        if len(body) > self._MAX_RESPONSE_BYTES:
            raise NativeClientError(
                "RESPONSE_TOO_LARGE", "Native status response exceeds 65536 bytes."
            )

        try:
            payload = json.loads(body)
        except (json.JSONDecodeError, UnicodeDecodeError) as error:
            raise NativeClientError(
                "INVALID_JSON", "Native status response is not valid JSON."
            ) from error

        try:
            return NativeStatus.model_validate(payload)
        except ValidationError as error:
            raise NativeClientError(
                "INVALID_NATIVE_STATUS",
                "Native status response does not match the strict schema.",
            ) from error
