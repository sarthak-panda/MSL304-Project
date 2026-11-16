"""Live side-by-side playback of two engine simulations (Three.js custom component)."""
from __future__ import annotations

from pathlib import Path
from typing import Any

import streamlit.components.v1 as components

_FRONTEND = Path(__file__).parent / "frontend"
_component = components.declare_component("flowsim_live", path=str(_FRONTEND))


def _compact(sim: dict, label: str) -> dict[str, Any]:
    recs = sorted(sim["service_records"], key=lambda r: (r["arrival_time_min"], r["service_start_min"]))
    m = sim["metrics"]
    return {
        "label": label,
        "counters": sim["counter_count"],
        "arrival": [r["arrival_time_min"] for r in recs],
        "start": [r["service_start_min"] for r in recs],
        "end": [r["service_end_min"] for r in recs],
        "counter": [r["counter_id"] for r in recs],
        "metrics": {k: m[k] for k in ("mean_wait_min", "max_queue_length", "total_objective_cost",
                                       "staffing_cost", "waiting_cost", "sla_penalty", "finish_time_min",
                                       "customers_served", "service_level_percent")},
    }


def payload(opt: dict, currency: str) -> dict[str, Any]:
    """Component arguments for one optimizer result: baseline run, recommended run and cost settings."""
    s = opt["scenario"]
    return {
        "today": _compact(opt["baseline_simulation"], "Today"),
        "plan": _compact(opt["recommended_simulation"], "FlowSim plan"),
        "meta": {
            "currency": currency,
            "counter_cost_per_hour": s["counter_cost_per_hour"],
            "waiting_cost_per_passenger_hour": s["waiting_cost_per_passenger_hour"],
            "sla_penalty_per_passenger": s["sla_penalty_per_passenger"],
            "sla_wait_minutes": s["sla_wait_minutes"],
            "replications": opt["run_info"]["replications"],
            "mean_savings": opt["objective_savings"],
            "mean_savings_percent": opt["objective_savings_percent"],
        },
    }


def live_demo(opt: dict | None, currency: str, run_id: str, key: str = "live_demo") -> None:
    """Renders the animated comparison of the baseline and recommended representative runs.

    With ``opt=None`` the frame stays collapsed and only preloads the 3D models.
    """
    args = payload(opt, currency) if opt is not None else {"today": None, "plan": None, "meta": None}
    _component(run_id=run_id, **args, key=key, default=None)
