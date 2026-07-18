from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    path = ROOT / relative
    assert path.is_file(), f"missing qualification file: {relative}"
    return path.read_text(encoding="utf-8")


def main() -> None:
    creator = read("tools/windows/New-FileReaderSimulationRun.ps1")
    runner = read("tools/windows/Invoke-FileReaderGuiQualification.ps1")
    checks = read("tools/windows/Invoke-AgentChecks.ps1")
    packager = read("tools/windows/New-AgentRuntimePackage.ps1")

    for token in (
        "BlockCount = 8",
        "TargetSeconds = 10.0",
        "MinimumSeconds = 10.0",
        "MaximumSeconds = 12.0",
        "C:\\OE-Agent-Simulation",
        "FILE_READER_EIGHT_BLOCK_QUALIFICATION",
        "EIGHT_SHANK_CLAIM",
    ):
        assert token in creator, token

    for token in (
        "RandomNumberGenerator",
        "[byte[]]::new(48)",
        "OE_AGENT_TOKEN",
        "Test-UnauthorizedRequest",
        "Test-StaleRevisionRejection",
        "Test-DirectoryCollisionRejection",
        "Test-RequestIdSemantics",
        "oe.transport.acquisition",
        "oe.transport.recording",
        "Get-Item -LiteralPath $candidates[0].FullName",
        "StabilityObservationCount = 3",
        "StabilityObservationSpanSeconds = 10",
        "num_channels",
        "sample_rate",
        "Get-FileHash",
        "NEUROPIXELS_SIM_CAPABILITY",
        "EIGHT_PRESET_SIM_ACCEPTANCE",
        "EIGHT_SHANK_CLAIM",
        "SCIENTIFIC_SIGNAL_QC",
    ):
        assert token in runner, token

    assert "test_file_reader_qualification_source.py" in checks
    assert "New-FileReaderSimulationRun.ps1" in packager
    assert "Invoke-FileReaderGuiQualification.ps1" in packager
    print("PASS complete File Reader qualification source contract")


if __name__ == "__main__":
    main()
