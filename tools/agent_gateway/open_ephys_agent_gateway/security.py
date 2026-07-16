import json
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import urlparse


def _powershell_json(script):
    completed = subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-NonInteractive",
            "-Command",
            script,
        ],
        check=False,
        capture_output=True,
        text=True,
        timeout=10,
    )
    if completed.returncode != 0:
        return None
    text = completed.stdout.strip()
    if not text:
        return None
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return None


def assess_native_api_exposure(
    *,
    port=37497,
    executable_path=None,
):
    listener = _powershell_json(
        "$c=Get-NetTCPConnection -State Listen "
        f"-LocalPort {int(port)} -ErrorAction SilentlyContinue | "
        "Select-Object -First 1;"
        "if($c){$p=Get-Process -Id $c.OwningProcess "
        "-ErrorAction SilentlyContinue;"
        "[pscustomobject]@{LocalAddress=$c.LocalAddress;"
        "LocalPort=$c.LocalPort;OwningProcess=$c.OwningProcess;"
        "OwnerPath=$p.Path}|ConvertTo-Json -Compress}"
    )
    addresses = []
    owner_paths = []
    listener_probe_failed = listener is None
    if isinstance(listener, dict):
        addresses.append(str(listener.get("LocalAddress", "")))
        owner_paths.append(str(listener.get("OwnerPath", "")))
    elif isinstance(listener, list):
        addresses.extend(
            str(item.get("LocalAddress", ""))
            for item in listener
            if isinstance(item, dict)
        )
        owner_paths.extend(
            str(item.get("OwnerPath", ""))
            for item in listener
            if isinstance(item, dict)
        )

    wildcard_listener = any(
        address in {"0.0.0.0", "::", "[::]"}
        for address in addresses
    )
    public_allow = False
    firewall_probe_failed = False
    if executable_path:
        escaped = executable_path.replace("'", "''")
        rules = _powershell_json(
            "$filters=Get-NetFirewallApplicationFilter "
            "-ErrorAction SilentlyContinue | "
            f"Where-Object {{$_.Program -ieq '{escaped}'}};"
            "$items=@();foreach($f in $filters){"
            "$r=Get-NetFirewallRule "
            "-AssociatedNetFirewallApplicationFilter $f "
            "-ErrorAction SilentlyContinue;"
            "$items += $r | Select-Object Enabled,Direction,Action,Profile};"
            "$items|ConvertTo-Json -Compress"
        )
        firewall_probe_failed = rules is None
        if isinstance(rules, dict):
            rules = [rules]
        if isinstance(rules, list):
            public_allow = any(
                str(rule.get("Enabled", "")).lower()
                in {"true", "1"}
                and str(rule.get("Direction", "")).lower()
                in {"inbound", "1"}
                and str(rule.get("Action", "")).lower()
                in {"allow", "2"}
                and (
                    "public"
                    in str(rule.get("Profile", "")).lower()
                    or str(rule.get("Profile", "")) == "4"
                )
                for rule in rules
                if isinstance(rule, dict)
            )

    reasons = []
    if listener_probe_failed:
        reasons.append("NATIVE_API_LISTENER_PROBE_FAILED")
    if firewall_probe_failed:
        reasons.append("NATIVE_API_FIREWALL_PROBE_FAILED")
    if executable_path:
        expected_path = str(Path(executable_path)).lower()
        if (
            not owner_paths
            or any(
                not path
                or str(Path(path)).lower() != expected_path
                for path in owner_paths
            )
        ):
            reasons.append("NATIVE_API_OWNER_MISMATCH")
    if wildcard_listener:
        reasons.append("NATIVE_API_WILDCARD_LISTENER")
    if public_allow:
        reasons.append("NATIVE_API_PUBLIC_FIREWALL_ALLOW")
    return {
        "safe_for_mutation": not reasons,
        "reasons": reasons,
        "listener_addresses": addresses,
        "owner_paths": owner_paths,
        "public_firewall_allow": public_allow,
    }


def is_simulated_config(xml):
    try:
        root = ET.fromstring(xml)
    except ET.ParseError:
        return False
    sources = [
        processor
        for processor in root.findall(".//PROCESSOR")
        if processor.get("processorType") == "2"
        or processor.get("type") == "4"
    ]
    if not sources:
        return False
    for source in sources:
        name = (source.get("name") or "").strip().lower()
        if name == "source sim":
            continue
        probes = source.findall(".//NP_PROBE")
        if not probes:
            return False
        if not all(
            (probe.get("bs_firmware_version") or "")
            .strip()
            .lower()
            .startswith("sim")
            and "simulated"
            in (probe.get("bs_part_number") or "").lower()
            for probe in probes
        ):
            return False
    return True


def validate_loopback_backend_url(url):
    parsed = urlparse(url)
    if parsed.scheme != "http":
        raise ValueError("Open Ephys backend must use local HTTP")
    if parsed.hostname not in {"127.0.0.1", "::1", "localhost"}:
        raise ValueError("Open Ephys backend must be loopback")
    if parsed.path not in {"", "/"} or parsed.query or parsed.fragment:
        raise ValueError("Open Ephys backend URL must not contain a path")
    return parsed.port or 80
