import json
import re
from pathlib import Path

LOCK = Path(__file__).resolve().parents[2] / "agent_native" / "interop" / "mcp-v2-win-py312.lock"
MANIFEST = LOCK.with_name("wheelhouse-manifest.json")
LINE = re.compile(r"^([a-z0-9-]+)==([^ ]+) --hash=sha256:([0-9a-f]{64})$")


def main() -> int:
    lines = [line for line in LOCK.read_text(encoding="utf-8").splitlines() if line]
    parsed = [LINE.fullmatch(line) for line in lines]
    if not lines or any(match is None for match in parsed):
        raise SystemExit("lock must contain only exact versions with one sha256 hash")
    names = [match.group(1) for match in parsed]
    if len(names) != len(set(names)):
        raise SystemExit("lock contains duplicate normalized package names")
    if "mcp==2.0.0" not in lines[0]:
        raise SystemExit("official MCP SDK must be pinned to 2.0.0")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("target") != {"os": "windows", "arch": "amd64", "python": "cp312", "binary_only": True}:
        raise SystemExit("manifest target must be binary-only Windows CPython 3.12 amd64")
    manifest_hashes = {entry["sha256"] for entry in manifest.get("files", [])}
    lock_hashes = {match.group(3) for match in parsed}
    if len(manifest.get("files", [])) != len(lines) or manifest_hashes != lock_hashes:
        raise SystemExit("manifest files must correspond one-to-one with the complete lock")
    print(f"validated {len(lines)} locked Windows CPython 3.12 distributions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
