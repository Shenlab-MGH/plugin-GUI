import argparse
import hashlib
import os
import secrets
from pathlib import Path

from .audit import JsonlAuditLog
from .http_server import create_server
from .oe_client import OpenEphysClient
from .security import (
    assess_native_api_exposure,
    is_simulated_config,
    validate_loopback_backend_url,
)
from .service import AgentGatewayService


def build_parser():
    parser = argparse.ArgumentParser(
        description="Safe localhost Agent gateway for Open Ephys GUI",
    )
    parser.add_argument("--port", type=int, default=37498)
    parser.add_argument(
        "--oe-url",
        default="http://127.0.0.1:37497",
    )
    parser.add_argument(
        "--oe-executable",
        default=r"C:\Program Files\Open Ephys\open-ephys.exe",
    )
    parser.add_argument(
        "--audit",
        type=Path,
        default=Path(
            os.environ.get("LOCALAPPDATA", ".")
        )
        / "OpenEphysAgent"
        / "audit.jsonl",
    )
    parser.add_argument(
        "--token",
        default=None,
        help="Bearer token; generated when omitted",
    )
    parser.add_argument(
        "--arm-sim",
        action="store_true",
        help=(
            "Allow mutation only when the configuration is simulated "
            "and native API exposure checks pass"
        ),
    )
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    backend_port = validate_loopback_backend_url(args.oe_url)
    client = OpenEphysClient(args.oe_url)

    def check_safety():
        exposure = assess_native_api_exposure(
            port=backend_port,
            executable_path=args.oe_executable,
        )
        try:
            config_xml = client.get_config_xml()
            simulated = is_simulated_config(config_xml)
            config_sha256 = hashlib.sha256(
                config_xml.encode("utf-8")
            ).hexdigest()
            config_probe_error = None
        except Exception as error:
            simulated = False
            config_sha256 = None
            config_probe_error = type(error).__name__
        evidence = {
            "simulated_config": simulated,
            "config_sha256": config_sha256,
            "config_probe_error": config_probe_error,
            "native_api": exposure,
        }
        if not args.arm_sim:
            return False, "NOT_ARMED", evidence
        if config_probe_error:
            return False, "CONFIG_PROBE_FAILED", evidence
        if not simulated:
            return False, "NON_SIMULATED_CONFIG", evidence
        if not exposure["safe_for_mutation"]:
            return False, "NATIVE_API_EXPOSED", evidence
        return True, None, evidence

    mutation_allowed, disabled_reason, safety_evidence = (
        check_safety()
    )

    token = args.token or secrets.token_urlsafe(32)
    service = AgentGatewayService(
        client,
        JsonlAuditLog(args.audit),
        mutation_allowed=mutation_allowed,
        mutation_disabled_reason=disabled_reason,
        safety_check=check_safety,
        safety_evidence=safety_evidence,
    )
    server = create_server(
        service,
        host="127.0.0.1",
        port=args.port,
        token=token,
    )

    print("Open Ephys Agent Gateway v0.0.1")
    print(f"Dashboard: http://127.0.0.1:{args.port}/")
    print(f"Bearer token: {token}")
    print(f"Mutation allowed: {mutation_allowed}")
    if not mutation_allowed:
        print(f"Mutation disabled: {disabled_reason}")
    risks = safety_evidence["native_api"]["reasons"]
    if risks:
        print("Native API risks: " + ", ".join(risks))
    print(f"Audit log: {args.audit}")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
