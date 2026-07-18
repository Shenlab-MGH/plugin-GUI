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
            "Verifier must inspect InvokePattern capability",
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
        "[switch] $ExpectInteractive":
            "Verifier must distinguish read-only and interactive modes",
        "[switch] $ExerciseTransport":
            "Live transport exercise must require a separate opt-in",
        "Wait-NativeMode":
            "Interactive exercise must read back authoritative state",
        "$acquisitionInvoke.value.Invoke()":
            "Interactive exercise must invoke the semantic acquisition node",
        "-Mode 'ACQUIRE'":
            "Interactive exercise must verify acquisition started",
        "-Mode 'IDLE'":
            "Interactive exercise must restore the safe idle state",
        "finally {":
            "Interactive exercise must run a recovery check on failure",
        "if ($recoveryStatus.mode -eq 'ACQUIRE')":
            "Recovery must act only after authoritative ACQUIRE readback",
        "Unsafe recovery state":
            "Unexpected recovery modes must fail closed",
        "RECOVERY_REQUIRED":
            "Unconfirmed recovery must be reported explicitly",
        "Get-ProcessElements":
            "Verifier must audit the complete PID-scoped UIA tree",
        "action_allowlist_matches":
            "Verifier must enforce the exact actionable-element allowlist",
        "WindowPattern]::Pattern":
            "Verifier must detect top-level close and visual-state control",
        "TransformPattern]::Pattern":
            "Verifier must detect top-level move and resize control",
        "os_window_patterns_acknowledged":
            "Verifier must report the unavoidable top-level HWND boundary",
        "ApprovedSimulationProfile":
            "Transport exercise must require a named simulation profile",
        "FILE_READER_GUI_SMOKE":
            "The live exercise must be limited to the approved File Reader profile",
        "ExpectedConfigurationSha256":
            "Simulation approval must bind the exact config digest",
        "CommandLineToArgvW":
            "Windows command line must be parsed into exact argv tokens",
        "$stateIndexes.Count -eq 1":
            "Exactly one state-directory flag must be present",
        "$configMatches.Count -ne 1":
            "Exactly one normalized config token must match",
        "LastWriteTimeUtc":
            "Runtime recovery evidence must be newer than process start",
        "Assert-FileReaderConfig":
            "Both approved and runtime recovery graphs must be validated",
        "processorType":
            "Every allowlisted processor must match its exact schema type",
        "recoveryConfig.xml":
            "Verifier must inspect the runtime-published processor graph",
        "--no-user-plugins":
            "Hardware plugins must remain disabled during File Reader smoke",
        "Wait-TransportTerminal":
            "Every UIA request must expose a queryable terminal state",
        "|REQUEST=":
            "UIA ValuePattern must expose the request correlation id",
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
