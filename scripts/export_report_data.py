#!/usr/bin/env python3
"""Runs the C++ engine and writes the CSV tables plotted in the slides and the report (docs/latex/data)."""
from __future__ import annotations

import copy
import csv
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "ui"))

from flowsim_client import FlowSimClient  # noqa: E402
from scenario import SAMPLE_SCENARIO, build_document, parse_passenger_csv, passengers_from_frame  # noqa: E402

OUT = ROOT / "docs" / "latex" / "data"


def write(name: str, rows: list[dict]) -> None:
    with open(OUT / name, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    print(f"{name}: {len(rows)} rows")


def erlang_c_wait(lam: float, mean_service: float, c: int) -> float:
    """Mean wait in queue for a stationary M/M/c queue (minutes)."""
    a = lam * mean_service
    rho = a / c
    tail = a ** c / math.factorial(c) / (1 - rho)
    p_wait = tail / (sum(a ** k / math.factorial(k) for k in range(c)) + tail)
    return p_wait * mean_service / (c - a)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    client = FlowSimClient()
    base = copy.deepcopy(SAMPLE_SCENARIO)
    opt = client.optimize(build_document(base))

    write("per_counter.csv", [{
        "c": r["counter_count"], "staffing": round(r["mean_staffing_cost"], 1),
        "waiting": round(r["mean_waiting_cost"], 1), "sla": round(r["mean_sla_penalty"], 1),
        "total": round(r["mean_objective_cost"], 1), "sd": round(r["stddev_objective_cost"], 1),
        "wait": round(r["mean_wait_min"], 2), "p95": round(r["mean_p95_wait_min"], 2),
        "service_level": round(r["mean_service_level_percent"], 1), "util": round(r["mean_utilization_percent"], 1),
        "maxq": round(r["mean_max_queue_length"], 1), "finish": round(r["mean_finish_time_min"], 1),
    } for r in opt["results_by_counter_count"]])

    for tag, sim in (("today", opt["baseline_simulation"]), ("plan", opt["recommended_simulation"])):
        write(f"queue_{tag}.csv", [{"t": round(p["time_min"], 3), "q": p["queue_length"]} for p in sim["queue_trace"]])
        waits = [r["service_start_min"] - r["arrival_time_min"] for r in sim["service_records"]]
        edges = list(range(0, 50, 5))
        write(f"wait_hist_{tag}.csv", [{"lo": lo, "n": sum(lo <= w < lo + 5 or (lo == 45 and w >= 50) for w in waits)}
                                       for lo in edges])
    m_today, m_plan = opt["baseline_simulation"]["metrics"], opt["recommended_simulation"]["metrics"]
    write("rep_run.csv", [{"plan": name, "c": sim["counter_count"], **{k: round(m[k], 2) for k in (
        "mean_wait_min", "p95_wait_min", "max_queue_length", "service_level_percent", "utilization_percent",
        "staffing_cost", "waiting_cost", "sla_penalty", "total_objective_cost")}}
        for name, sim, m in (("Today", opt["baseline_simulation"], m_today),
                             ("FlowSim", opt["recommended_simulation"], m_plan))])
    write("summary.csv", [{"best": opt["best_counter_count"], "best_cost": round(opt["best_objective_cost"], 1),
                           "base_cost": round(opt["baseline_objective_cost"], 1),
                           "savings": round(opt["objective_savings"], 1),
                           "savings_pct": round(opt["objective_savings_percent"], 1),
                           "wait_change_pct": round(opt["wait_change_percent"], 1)}])

    rows = []
    for rate in range(20, 68, 4):
        s = {**copy.deepcopy(base), "arrival_rate_per_hour": float(rate), "max_counters": 12}
        o = client.optimize(build_document(s))
        best = next(r for r in o["results_by_counter_count"] if r["counter_count"] == o["best_counter_count"])
        rows.append({"rate": rate, "load": round(rate / 60 * 5.5, 2), "best": o["best_counter_count"],
                     "base_cost": round(o["baseline_objective_cost"], 1), "best_cost": round(o["best_objective_cost"], 1),
                     "savings_pct": round(o["objective_savings_percent"], 1), "best_wait": round(best["mean_wait_min"], 2)})
    write("sensitivity.csv", rows)

    lam = base["arrival_rate_per_hour"] / 60
    rows = []
    for c in (5, 6, 7, 8):
        s = {**copy.deepcopy(base), "arrival_window_minutes": 10080.0, "replications": 20, "min_counters": c,
             "max_counters": c, "current_counters": None, "record_trace": False}
        s.pop("current_counters")
        r = client.optimize(build_document(s))["results_by_counter_count"][0]
        theory = erlang_c_wait(lam, base["mean_service_time_minutes"], c)
        rows.append({"c": c, "rho": round(lam * base["mean_service_time_minutes"] / c, 3), "sim": round(r["mean_wait_min"], 3),
                     "theory": round(theory, 3), "util_sim": round(r["mean_utilization_percent"], 1),
                     "err_pct": round(100 * (r["mean_wait_min"] - theory) / theory, 1)})
    write("erlang.csv", rows)

    df, errors = parse_passenger_csv((ROOT / "data" / "sample_passengers.csv").read_bytes())
    assert not errors, errors
    s = {k: v for k, v in base.items() if k not in (
        "arrival_window_minutes", "arrival_rate_per_hour", "mean_service_time_minutes", "service_time_distribution",
        "replications", "seed")}
    s.update(input_mode="csv", max_counters=6, current_counters=2)
    o = client.optimize(build_document(s, passengers_from_frame(df)))
    write("csv_mode.csv", [{"c": r["counter_count"], "total": round(r["mean_objective_cost"], 1),
                            "wait": round(r["mean_wait_min"], 2), "util": round(r["mean_utilization_percent"], 1)}
                           for r in o["results_by_counter_count"]])


if __name__ == "__main__":
    main()
