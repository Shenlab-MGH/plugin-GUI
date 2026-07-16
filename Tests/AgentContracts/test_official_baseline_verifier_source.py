from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = (
    ROOT / "tools" / "windows" / "Test-OfficialV102Baseline.ps1"
).read_text(encoding="utf-8")


def require(fragment: str, message: str) -> None:
    if fragment not in SCRIPT:
        raise AssertionError(message)


def main() -> None:
    required = {
        "c91afebcfb0678a667fb93f6312ed33c56ec640f":
            "Verifier must pin the official commit",
        "7f2541f24394ffdb204dc4326a2a86f1204beb43":
            "Verifier must pin the official tree",
        "5A61946F051E88947C2D08A5E0AA75A8457EDE1DC3F54238C569D23A6C55E429":
            "Verifier must pin the Artifactory ZIP checksum",
        "status --porcelain=v1":
            "Verifier must reject a dirty official worktree",
        "published_static_resources":
            "Verifier must compare published resources by hash",
        "published_builtin_plugins":
            "Verifier must validate the complete plugin inventory",
        "official_pe_identity":
            "Verifier must inspect official PE version identity",
        "official_zip_extraction_binding":
            "Verifier must bind the extracted directory to the verified ZIP",
        "missing_from_extraction":
            "Verifier must detect missing extracted files",
        "unexpected_in_extraction":
            "Verifier must detect extra extracted files",
        "$reader.ReadUInt16()":
            "Verifier must inspect the PE machine directly",
        "$officialPe.machine -eq 'AMD64'":
            "Verifier must require an x64 official executable",
        "local_pristine_release":
            "Verifier must support the later local pristine build",
        "LocalBuildEvidencePath is required":
            "Local build claims must require build provenance evidence",
        "LocalReleaseRoot must not be the extracted historical":
            "Historical release files must not masquerade as a local build",
        "$buildEvidence.executable_sha256 -eq $localExeHash":
            "Local evidence must bind to the exact local executable",
        "$buildEvidence.release_manifest":
            "Local evidence must bind the complete Release directory",
        "$buildManifestMatches":
            "Verifier must reject post-build Release changes",
        "$localLayoutDelta.Count -eq 0":
            "Verifier must reject missing or extra local Release files",
        "$officialPackagingExtras":
            "Verifier must distinguish packaging extras from Build Release output",
        "$configureLogMatches":
            "Verifier must bind the configure log",
        "$buildLogMatches":
            "Verifier must bind the build log",
        "$cmakeCacheMatches":
            "Verifier must bind the CMake cache",
        "Binary hashes are recorded, not required to match":
            "Verifier must not require historical and rebuilt PE hashes to match",
        "overall_status":
            "Verifier must emit a machine-readable verdict",
    }
    for fragment, message in required.items():
        require(fragment, message)

    forbidden = {
        "Remove-Item": "Verifier must be read-only",
        "Stop-Process": "Verifier must not touch running applications",
        "Start-Process": "Verifier must not launch Open Ephys",
        "/api/status": "Verifier must not probe the laboratory Open Ephys API",
    }
    for fragment, message in forbidden.items():
        if fragment in SCRIPT:
            raise AssertionError(message)

    print("PASS official baseline verifier source contract")


if __name__ == "__main__":
    main()
