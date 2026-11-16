#!/usr/bin/env python3
"""Builds the static GitHub Pages site: runs the C++ optimizer on preset scenarios and bundles the live replay.

Usage: python scripts/build_pages.py [--out site]
"""
from __future__ import annotations

import argparse
import copy
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "ui"))

from flowsim_client import FlowSimClient  # noqa: E402
from live_demo import payload  # noqa: E402
from scenario import SAMPLE_SCENARIO, build_document  # noqa: E402

PRESETS = {
    "rush": {},
    "quiet": {"name": "Quiet midday (illustrative)", "arrival_rate_per_hour": 22.0},
    "peak": {"name": "Holiday peak (illustrative)", "arrival_rate_per_hour": 62.0},
}


def build(out: Path) -> dict[str, dict]:
    client = FlowSimClient()
    if not client.available():
        raise SystemExit("flowsim_cli is not built; run ./scripts/build.sh first")
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(ROOT / "ui" / "live_demo" / "frontend", out / "demo")
    (out / "assets").mkdir()
    for name in ("poster.jpg", "avatar.jpg"):
        shutil.copy(ROOT / "ui" / "static" / name, out / "assets" / name)
    shutil.copy(ROOT / "web" / "index.html", out / "index.html")
    (out / ".nojekyll").touch()
    (out / "data").mkdir()
    results = {}
    for run, overrides in PRESETS.items():
        scenario = {**copy.deepcopy(SAMPLE_SCENARIO), **overrides}
        opt = client.optimize(build_document(scenario))
        args = payload(opt, "₹")
        (out / "data" / f"{run}.json").write_text(json.dumps(args, separators=(",", ":")))
        results[run] = opt
        print(f"{run}: today {opt['baseline_simulation']['counter_count']} lanes, plan {opt['best_counter_count']} lanes, "
              f"mean saving {opt['objective_savings']:,.0f}")
    return results


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=ROOT / "site")
    build(ap.parse_args().out)


if __name__ == "__main__":
    main()
