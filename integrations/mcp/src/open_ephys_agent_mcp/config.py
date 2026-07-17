"""Fail-closed process configuration."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from urllib.parse import urlsplit


class ConfigurationError(ValueError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class Settings:
    base_url: str = "http://127.0.0.1:38498"
    bearer_token: str = field(repr=False, default="")

    def __post_init__(self) -> None:
        parsed = urlsplit(self.base_url)
        if parsed.scheme != "http":
            raise ConfigurationError(
                "BASE_URL_NOT_HTTP", "Native endpoint must use loopback HTTP."
            )
        if parsed.username is not None or parsed.password is not None:
            raise ConfigurationError(
                "BASE_URL_CREDENTIALS_FORBIDDEN",
                "Native endpoint URL must not contain credentials.",
            )
        if parsed.hostname not in {"127.0.0.1", "::1"}:
            raise ConfigurationError(
                "BASE_URL_NOT_LITERAL_LOOPBACK",
                "Native endpoint must use a literal loopback address.",
            )
        try:
            port = parsed.port
        except ValueError as error:
            raise ConfigurationError(
                "BASE_URL_PORT_INVALID", "Native endpoint port is invalid."
            ) from error
        if port is None:
            raise ConfigurationError(
                "BASE_URL_PORT_REQUIRED", "Native endpoint port is required."
            )
        if port == 37497:
            raise ConfigurationError(
                "NATIVE_HTTP_FORBIDDEN",
                "The legacy Open Ephys HTTP endpoint is forbidden.",
            )
        if parsed.path not in {"", "/"} or parsed.query or parsed.fragment:
            raise ConfigurationError(
                "BASE_URL_PATH_FORBIDDEN",
                "Native endpoint URL must not contain a path, query, or fragment.",
            )
        if len(self.bearer_token) < 32:
            raise ConfigurationError(
                "TOKEN_TOO_SHORT", "OE_AGENT_TOKEN must contain at least 32 characters."
            )

    @classmethod
    def from_environment(cls) -> "Settings":
        token = os.environ.get("OE_AGENT_TOKEN")
        if token is None:
            raise ConfigurationError(
                "TOKEN_MISSING", "OE_AGENT_TOKEN is required."
            )
        return cls(
            base_url=os.environ.get(
                "OE_AGENT_BASE_URL", "http://127.0.0.1:38498"
            ),
            bearer_token=token,
        )
