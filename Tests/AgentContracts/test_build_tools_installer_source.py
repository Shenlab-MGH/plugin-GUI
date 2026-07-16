from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = (
    ROOT / "tools" / "windows" / "Install-BuildTools.ps1"
).read_text(encoding="utf-8")


def require(fragment: str, message: str) -> None:
    if fragment not in SCRIPT:
        raise AssertionError(message)


def main() -> None:
    required = {
        "Start-Process": "Installer must capture a real process object",
        "-Wait": "Installer must wait for bootstrapper completion",
        "-PassThru": "Installer must read the process exit code",
        "-Verb RunAs": "UAC installation must use the same waited process",
        "$process.ExitCode": "Installer must not rely on stale LASTEXITCODE",
        "5AE95BB02BB3442441A8D891E5BB1D2975445E2E3EE16ADA5BC7BD17227F1DD7":
            "Installer must pin the downloaded bootstrapper hash",
        "-products Microsoft.VisualStudio.Product.BuildTools":
            "Installer verification must bind to the Build Tools product",
        "-version '[17.14,17.15)'":
            "Installer verification must bind to the pinned minor release",
        "catalog.productDisplayVersion -eq $packageVersion":
            "Installer verification must bind to the pinned display version",
        "$installation.isComplete -eq $true":
            "Installer must require a complete instance",
        "$installation.isLaunchable -eq $true":
            "Installer must require a launchable instance",
        "($vswhereOutput -join [Environment]::NewLine).Trim()":
            "Installer must tolerate empty vswhere output",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64":
            "Installer must verify the C++ workload with vswhere",
        "Microsoft.VisualStudio.Component.Windows11SDK.26100":
            "Installer must verify the pinned Windows SDK component",
        "MSBuild\\Current\\Bin\\MSBuild.exe":
            "Installer must verify MSBuild",
        "VC\\Tools\\MSVC":
            "Installer must verify the MSVC toolset",
        "bin\\Hostx64\\x64\\cl.exe":
            "Installer must verify the real x64 compiler",
        "Windows Kits\\10\\Include\\10.0.26100.0":
            "Installer must verify the pinned Windows SDK",
        "[DateTime]::UtcNow.AddMinutes(10)":
            "Installer must tolerate bootstrapper handoff registration",
    }
    for fragment, message in required.items():
        require(fragment, message)

    if "& $installer.FullName @installArguments" in SCRIPT:
        raise AssertionError(
            "Installer must not use unreliable direct invocation"
        )
    uac_start = SCRIPT.index("if ($isAdministrator)")
    exit_capture = SCRIPT.index("$installerExitCode = $process.ExitCode")
    uac_block = SCRIPT[uac_start:exit_capture]
    for fragment in ("-Verb RunAs", "-Wait", "-PassThru"):
        if fragment not in uac_block:
            raise AssertionError(
                f"UAC control flow must include {fragment}"
            )
    if "return" in uac_block:
        raise AssertionError(
            "UAC control flow must not return before verification"
        )

    print("PASS build tools installer source contract")


if __name__ == "__main__":
    main()
