from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = (
    ROOT / "tools" / "windows" / "Invoke-OfficialV102Build.ps1"
).read_text(encoding="utf-8")


def require(fragment: str, message: str) -> None:
    if fragment not in SCRIPT:
        raise AssertionError(message)


def main() -> None:
    required = {
        "c91afebcfb0678a667fb93f6312ed33c56ec640f":
            "Build wrapper must pin the official commit",
        "7f2541f24394ffdb204dc4326a2a86f1204beb43":
            "Build wrapper must pin the official tree",
        "status --porcelain=v1":
            "Build wrapper must reject a dirty source tree",
        "'Visual Studio 17 2022'":
            "Build wrapper must use the official Windows generator",
        "'-A', 'x64'":
            "Build wrapper must use the official x64 architecture",
        "'--target', 'ALL_BUILD'":
            "Build wrapper must compile the official aggregate target",
        "BUILD_TESTS:BOOL=ON":
            "Build wrapper must reject tests in the production baseline",
        "Official baseline Build must be pristine":
            "Build wrapper must refuse an existing build cache or output",
        "ALL_BUILD.vcxproj":
            "Build wrapper must verify official project generation",
        "open-ephys.exe":
            "Build wrapper must verify the Release executable",
        "Compare-Object $expectedPlugins $observedPlugins":
            "Build wrapper must verify all built-in plugins",
        "release_manifest = Get-ReleaseManifest":
            "Build evidence must bind the complete Release directory",
        "cmake_cache_sha256":
            "Build evidence must bind the generated CMake cache",
        "configure_log_sha256":
            "Build evidence must bind the configure log",
        "build_log_sha256":
            "Build evidence must bind the build log",
    }
    for fragment, message in required.items():
        require(fragment, message)

    forbidden = {
        "-DOE_AGENT_NATIVE_FEATURES":
            "Official build must not enable Agent features",
        "-DBUILD_TESTS=ON":
            "Official production build must not enable tests",
        "Start-Process -FilePath \"open-ephys":
            "Build wrapper must not launch the GUI",
        "Remove-Item":
            "Build wrapper must not destroy an existing build tree",
    }
    for fragment, message in forbidden.items():
        if fragment in SCRIPT:
            raise AssertionError(message)

    print("PASS official build wrapper source contract")


if __name__ == "__main__":
    main()
