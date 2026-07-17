from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    path = ROOT / relative
    return path.read_text(encoding="utf-8") if path.exists() else ""


def require(source: str, fragment: str, message: str) -> None:
    if fragment not in source:
        raise AssertionError(message)


def main() -> None:
    launcher = read("tools/windows/Start-OpenEphysAgentMcp.ps1")
    configurator = read("tools/windows/New-OpenEphysAgentMcpConfig.ps1")
    builder = read("tools/windows/New-OpenEphysAgentMcpExecutable.ps1")
    packager = read("tools/windows/New-AgentRuntimePackage.ps1")
    checks = read("tools/windows/Invoke-AgentChecks.ps1")

    launcher_contract = {
        "OE_AGENT_TOKEN": "MCP launcher must require a process-environment token",
        "OE_AGENT_BASE_URL": "MCP launcher must pass the native endpoint by environment",
        "open-ephys-agent-mcp.exe": "MCP launcher must use the packaged executable",
        "& $executablePath": "MCP launcher must preserve stdio instead of detaching",
        "TOKEN_TOO_SHORT": "MCP launcher must fail closed on weak credentials",
        "NATIVE_HTTP_FORBIDDEN": "MCP launcher must reject legacy port 37497",
    }
    for fragment, message in launcher_contract.items():
        require(launcher, fragment, message)
    if "Start-Process" in launcher:
        raise AssertionError("MCP stdio launcher must not detach from its host")

    configurator_contract = {
        "[mcp_servers.open-ephys-agent]": "Configurator must emit Codex TOML",
        'env_vars = ["OE_AGENT_TOKEN", "OE_AGENT_BASE_URL"]':
            "Codex must forward credentials by variable name only",
        '"type": "stdio"': "Configurator must emit Claude stdio JSON",
        '"oe_get_runtime_status"': "Vendor configs must use an exact tool allowlist",
        "Set-Content": "Configurator must write only to an explicit output directory",
    }
    for fragment, message in configurator_contract.items():
        require(configurator, fragment, message)

    builder_contract = {
        "pyinstaller": "Builder must produce a self-contained MCP executable",
        "--onefile": "MCP executable must be a single-file Windows artifact",
        "uv.lock": "Builder must require the locked dependency graph",
        "open-ephys-agent-mcp.exe": "Builder must verify its expected artifact",
    }
    for fragment, message in builder_contract.items():
        require(builder, fragment, message)

    package_contract = {
        "McpExecutable": "Runtime packager must consume an explicit MCP artifact",
        "open-ephys-agent-mcp.exe": "Runtime package must include the MCP executable",
        "integrations\\mcp\\uv.lock": "Runtime package must bind the uv lockfile",
        "open-ephys-operator": "Runtime package must include the portable Skill",
        "Start-OpenEphysAgentMcp.ps1": "Runtime package must include the stdio launcher",
        "New-OpenEphysAgentMcpConfig.ps1": "Runtime package must include config adapters",
    }
    for fragment, message in package_contract.items():
        require(packager, fragment, message)

    require(
        checks,
        "test_mcp_skill_packaging_source.py",
        "Full local checks must run the MCP/Skill packaging contract",
    )
    print("PASS MCP and Skill packaging source contract")


if __name__ == "__main__":
    main()
