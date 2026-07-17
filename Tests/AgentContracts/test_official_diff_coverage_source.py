from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    path = ROOT / relative
    return path.read_text(encoding="utf-8") if path.exists() else ""


def main() -> None:
    package = read("tools/windows/New-AgentRuntimePackage.ps1")
    checks = read("tools/windows/Invoke-AgentChecks.ps1")
    registry = read("OFFICIAL-DIFF.md")

    assert "Test-OfficialDiffCoverage.ps1" in package, (
        "Runtime packaging must reject undisclosed production changes"
    )
    assert "Test-OfficialDiffCoverage.ps1" in checks, (
        "Full local checks must run the official-difference coverage gate"
    )
    assert "PXI plugin modification | NONE" in registry, (
        "The unchanged PXI plugin boundary must be explicit"
    )
    assert "Record Engine | UNCHANGED_BOUNDARY" in registry, (
        "The unchanged Record Engine boundary must be explicit"
    )
    print("PASS official difference coverage source contract")


if __name__ == "__main__":
    main()
