"""Shared UI helpers: formatting, KPI cards and session state."""
from __future__ import annotations

import copy
from datetime import datetime
from typing import Any

import streamlit as st

from scenario import SAMPLE_SCENARIO

CURRENCIES = {"INR (₹)": "₹", "Generic units": "", "USD ($)": "$", "EUR (€)": "€"}

HELP = {
    "served": "Customers whose service completed in this run.",
    "mean_wait": "Average time between arrival and start of service, over all customers.",
    "p95_wait": "95% of customers waited no longer than this value (nearest-rank percentile).",
    "util": "Total counter busy time divided by (counters x finish time).",
    "cost": "Staffing cost + waiting cost + SLA penalty under the configured cost weights.",
    "queue": "Time-weighted average number of customers waiting (excluding those in service).",
    "sla": "Share of customers who waited no longer than the SLA threshold.",
    "finish": "Time at which the last customer finished service, measured from scenario start.",
}


def init_state() -> None:
    defaults = {
        "scenario": copy.deepcopy(SAMPLE_SCENARIO),
        "is_sample": True,
        "csv_frame": None,
        "csv_name": None,
        "csv_errors": [],
        "sim_result": None,
        "sim_scenario": None,
        "opt_result": None,
        "opt_scenario": None,
        "history": [],
        "currency": "INR (₹)",
        "form_version": 0,
    }
    for key, value in defaults.items():
        st.session_state.setdefault(key, value)


def load_sample() -> None:
    st.session_state.scenario = copy.deepcopy(SAMPLE_SCENARIO)
    st.session_state.is_sample = True
    st.session_state.form_version += 1  # forces the builder form to show the new values


def md_escape(text: str) -> str:
    """Escapes '$' so Streamlit markdown does not treat currency amounts as LaTeX."""
    return text.replace("$", "\\$")


def inject_css() -> None:
    st.html("""<style>
    [data-testid="stMetricValue"] { font-size: clamp(1.15rem, 1.9vw, 2.1rem); }
    [data-testid="stMetricValue"] > div, [data-testid="stMetricLabel"] p {
        white-space: normal; overflow: visible; text-overflow: clip; word-break: normal; }
    </style>""")


def money(value: float | None) -> str:
    if value is None:
        return "n/a"
    sym = CURRENCIES[st.session_state.currency]
    return f"{sym}{value:,.0f}" if sym else f"{value:,.0f} units"


def pct(value: float | None, signed: bool = False) -> str:
    if value is None:
        return "n/a"
    return f"{value:+.1f}%" if signed else f"{value:.1f}%"


def kpi_row(metrics: dict[str, Any]) -> None:
    cols = st.columns(5)
    cols[0].metric("Customers served", f"{metrics['customers_served']:,}", help=HELP["served"], border=True)
    cols[1].metric("Average wait", f"{metrics['mean_wait_min']:.2f} min", help=HELP["mean_wait"], border=True)
    cols[2].metric("p95 wait", f"{metrics['p95_wait_min']:.2f} min", help=HELP["p95_wait"], border=True)
    cols[3].metric("Utilization", pct(metrics["utilization_percent"]), help=HELP["util"], border=True)
    cols[4].metric("Total cost", money(metrics["total_objective_cost"]), help=HELP["cost"], border=True)
    cols = st.columns(5)
    cols[0].metric("Mean queue length", f"{metrics['mean_queue_length']:.2f}", help=HELP["queue"], border=True)
    cols[1].metric("Max queue length", f"{metrics['max_queue_length']:,}", border=True)
    cols[2].metric("Service level", pct(metrics["service_level_percent"]), help=HELP["sla"], border=True)
    cols[3].metric("SLA breaches", f"{metrics['sla_breach_count']:,}", border=True)
    cols[4].metric("Finish time", f"{metrics['finish_time_min']:.1f} min", help=HELP["finish"], border=True)


def add_history(kind: str, scenario: dict[str, Any], summary: dict[str, Any]) -> None:
    st.session_state.history.append({
        "time": datetime.now().strftime("%H:%M:%S"),
        "run": kind,
        "scenario": scenario["name"],
        "mode": scenario["input_mode"],
        "seed": str(scenario.get("seed")) if scenario["input_mode"] == "synthetic" else "fixed CSV",
        "replications": scenario.get("replications") if scenario["input_mode"] == "synthetic" else 1,
        **summary,
    })


def settings_table(scenario: dict[str, Any]) -> list[dict[str, str]]:
    rows = []
    for key, value in scenario.items():
        if key == "passengers":
            continue
        rows.append({"setting": key, "value": str(value)})
    return rows
