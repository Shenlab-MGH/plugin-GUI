from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VERIFIER_PATH = ROOT / "tools" / "windows" / "Test-AgentAccessibility.ps1"
VERIFIER = (
    VERIFIER_PATH.read_text(encoding="utf-8")
    if VERIFIER_PATH.exists()
    else ""
)


def require(fragment: str, message: str) -> None:
    if fragment not in VERIFIER:
        raise AssertionError(message)


def main() -> None:
    required = {
        "UIAutomationClient":
            "Verifier must use Windows UI Automation",
        "AutomationElement]::ProcessIdProperty":
            "Verifier must scope discovery to the fork PID",
        "AutomationElement]::AutomationIdProperty":
            "Verifier must query exact AutomationIds",
        '"oe.agent.root"':
            "Verifier must require the Agent root",
        '"oe.transport.acquisition"':
            "Verifier must require the acquisition node",
        '"oe.transport.recording"':
            "Verifier must require the recording node",
        "ValuePattern]::Pattern":
            "Verifier must require the read-only value pattern",
        "InvokePattern]::Pattern":
            "Verifier must prove InvokePattern is absent",
        "TogglePattern]::Pattern":
            "Verifier must prove TogglePattern is absent",
        "$ReadIterations = 10000":
            "Verifier must perform exactly 10,000 read iterations",
        "/v1/status":
            "Verifier must compare native status before and after reads",
        "pre_revision":
            "Verifier must report the pre-read revision",
        "post_revision":
            "Verifier must report the post-read revision",
        "Select-Object":
            "Verifier output must remain compact",
    }
    for fragment, message in required.items():
        require(fragment, message)

    if "OE_AGENT_TOKEN" not in VERIFIER:
        raise AssertionError(
            "Verifier must receive the token only through the environment"
        )
    if "AgentToken" in VERIFIER:
        raise AssertionError(
            "Verifier must not accept the bearer token as a command argument"
        )

    print("PASS Windows UIA verifier source contract")


if __name__ == "__main__":
    main()
