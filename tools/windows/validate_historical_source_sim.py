"""Print deterministic read-only historical Source Sim validation JSON."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "integrations" / "mcp" / "src"))

from open_ephys_agent_mcp.experiment.historical_source_sim import (  # noqa: E402
    HistoricalEvidenceError,
    validate_historical_source_sim,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("evidence_root", type=Path)
    args = parser.parse_args()
    try:
        result = validate_historical_source_sim(args.evidence_root)
    except HistoricalEvidenceError as error:
        print(json.dumps({"ok": False, "error": error.code}, sort_keys=True))
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
