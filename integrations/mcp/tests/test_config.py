import pytest

from open_ephys_agent_mcp.config import ConfigurationError, Settings


TOKEN = "x" * 32


def test_accepts_explicit_loopback_http(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("OE_AGENT_BASE_URL", "http://127.0.0.1:38498")
    monkeypatch.setenv("OE_AGENT_TOKEN", TOKEN)

    settings = Settings.from_environment()

    assert settings.base_url == "http://127.0.0.1:38498"
    assert settings.bearer_token == TOKEN
    assert TOKEN not in repr(settings)


@pytest.mark.parametrize(
    ("base_url", "token", "code"),
    [
        ("http://127.0.0.1:38498", None, "TOKEN_MISSING"),
        ("http://127.0.0.1:38498", "short", "TOKEN_TOO_SHORT"),
        ("https://127.0.0.1:38498", TOKEN, "BASE_URL_NOT_HTTP"),
        ("http://localhost:38498", TOKEN, "BASE_URL_NOT_LITERAL_LOOPBACK"),
        ("http://0.0.0.0:38498", TOKEN, "BASE_URL_NOT_LITERAL_LOOPBACK"),
        ("http://192.168.1.10:38498", TOKEN, "BASE_URL_NOT_LITERAL_LOOPBACK"),
        ("http://127.0.0.1", TOKEN, "BASE_URL_PORT_REQUIRED"),
        ("http://127.0.0.1:37497", TOKEN, "NATIVE_HTTP_FORBIDDEN"),
        ("http://127.0.0.1:38498/v1", TOKEN, "BASE_URL_PATH_FORBIDDEN"),
        ("http://user@127.0.0.1:38498", TOKEN, "BASE_URL_CREDENTIALS_FORBIDDEN"),
    ],
)
def test_rejects_unsafe_configuration(
    monkeypatch: pytest.MonkeyPatch,
    base_url: str,
    token: str | None,
    code: str,
) -> None:
    monkeypatch.setenv("OE_AGENT_BASE_URL", base_url)
    if token is None:
        monkeypatch.delenv("OE_AGENT_TOKEN", raising=False)
    else:
        monkeypatch.setenv("OE_AGENT_TOKEN", token)

    with pytest.raises(ConfigurationError) as caught:
        Settings.from_environment()

    assert caught.value.code == code
    assert TOKEN not in str(caught.value)
