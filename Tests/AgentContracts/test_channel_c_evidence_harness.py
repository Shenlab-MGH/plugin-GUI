from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
QUALIFICATION = ROOT / "tools" / "qualification" / "run_qualification_batch.py"
AGENT_CHECKS = ROOT / "tools" / "windows" / "Invoke-ChannelCAgentChecks.ps1"


def require_source(path: Path, fragments: tuple[str, ...]) -> None:
    assert path.is_file(), f"missing evidence harness: {path.relative_to(ROOT)}"
    source = path.read_text(encoding="utf-8")
    for fragment in fragments:
        assert fragment in source, f"{path.name} is missing {fragment!r}"


def test_qualification_batch_has_reproducible_machine_contract() -> None:
    require_source(
        QUALIFICATION,
        (
            "DEFAULT_SEED = 0xC0DE1701",
            "DEFAULT_RANDOM_CASES = 100_000",
            "EXPECTED_BOUNDARY_CASES = 502",
            "def oracle_evaluate(",
            '"source_commit"',
            '"script_sha256"',
            '"result_sha256"',
            '"failures"',
        ),
    )
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / "qualification.json"
        completed = subprocess.run(
            [
                sys.executable,
                str(QUALIFICATION),
                "--random-cases",
                "25",
                "--output",
                str(output),
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        assert completed.returncode == 0, completed.stderr or completed.stdout
        report = json.loads(output.read_text(encoding="utf-8"))
        assert report["boundary_cases"] == 502
        assert report["random_cases"] == 25
        assert report["total_cases"] == 527
        assert report["seed"] == 0xC0DE1701
        assert report["failures"] == []
        assert report["decision"] == "PASS"
        assert len(report["source_commit"]) == 40
        assert len(report["script_sha256"]) == 64
        assert len(report["result_sha256"]) == 64


def test_agent_checks_wrapper_is_repo_scoped_and_machine_verifiable() -> None:
    require_source(
        AGENT_CHECKS,
        (
            "Invoke-AgentChecks.ps1",
            "channel-c-logs",
            "Get-FileHash",
            "ElapsedSeconds",
            "PytestPassed",
            "ExplicitPassLines",
            "FinalLine",
            "ExitCode",
            "Resolve-PathWithinEvidenceRoot",
            "ReparsePoint",
        ),
    )
    with tempfile.TemporaryDirectory() as temporary:
        completed = subprocess.run(
            [
                "pwsh",
                "-NoProfile",
                "-File",
                str(AGENT_CHECKS),
                "-OutputDirectory",
                temporary,
                "-ValidateOnly",
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        assert completed.returncode != 0
        assert "docs\\agent\\evidence" in (completed.stderr + completed.stdout)
