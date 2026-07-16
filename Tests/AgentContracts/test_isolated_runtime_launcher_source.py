from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = (
    ROOT / "tools" / "windows" /
    "Start-IsolatedAgentRuntime.ps1"
).read_text(encoding="utf-8")
PACKAGER = (
    ROOT / "tools" / "windows" /
    "New-AgentRuntimePackage.ps1"
).read_text(encoding="utf-8")


def require(source: str, fragment: str, message: str) -> None:
    if fragment not in source:
        raise AssertionError(message)


def main() -> None:
    launcher_contract = {
        "[switch] $Launch":
            "Launcher must default to inspect-only",
        "[switch] $MaintenanceWindowApproved":
            "Launch must require a separate maintenance approval",
        "Get-Process -Name 'open-ephys'":
            "Any running Open Ephys process must block launch",
        "'--no-http'":
            "Native Open Ephys HTTP must be locked off",
        "'--no-user-plugins'":
            "User-installed plugins must be locked off",
        "'--state-dir'":
            "A dedicated state directory must be passed",
        "OE_AGENT_TOKEN":
            "A strong per-launch token must be required",
        "Get-NetTCPConnection":
            "The requested Agent port must be checked",
        "Start-Process":
            "The approved launcher must start the packaged executable",
        "if (-not $Launch)":
            "Inspection must return without starting a process",
    }
    for fragment, message in launcher_contract.items():
        require(LAUNCHER, fragment, message)

    package_contract = {
        "Get-FileHash":
            "Every packaged file must be hashed",
        "RUN-MANIFEST.json":
            "Package provenance must be persisted",
        "git rev-parse HEAD":
            "Package manifest must identify its source commit",
        "source_dirty":
            "Package manifest must disclose uncommitted source",
    }
    for fragment, message in package_contract.items():
        require(PACKAGER, fragment, message)

    print("PASS isolated runtime launcher source contract")


if __name__ == "__main__":
    main()
