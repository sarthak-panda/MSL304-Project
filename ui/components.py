"""Shared UI helpers: formatting, KPI cards and session state."""
from __future__ import annotations

import copy
import html
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
        "live_run": 0,
        "live_busy": None,
        "live_errors": [],
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
    [data-testid="stMetricValue"] { font-size: clamp(1.15rem, 1.9vw, 2.1rem); font-family: 'Barlow Condensed', sans-serif; }
    [data-testid="stMetricValue"] > div, [data-testid="stMetricLabel"] p {
        white-space: normal; overflow: visible; text-overflow: clip; word-break: normal; }
    [data-testid="stMetricLabel"] p { text-transform: uppercase; letter-spacing: .08em; font-size: .78rem; color: #8a949e; }
    button[kind="primary"], button[kind="primaryFormSubmit"] { color: #111417 !important; font-weight: 600; }
    button[kind="primary"] p, button[kind="primaryFormSubmit"] p { color: #111417 !important; }
    button[kind="primary"]:disabled { background: #ffd23f !important; border-color: #ffd23f !important; opacity: .65; }
    .stButton button, .stDownloadButton button, .stFormSubmitButton button { min-height: 2.6rem; }
    .block-container { padding-top: 3.6rem; padding-bottom: 1.5rem; }
    .stElementContainer:has(> div > iframe[height="0"]) { margin-bottom: -1rem; }
    h1 { font-weight: 700; letter-spacing: .01em; }
    .fs-brand { font: 700 1.9rem 'Barlow Condensed', sans-serif; letter-spacing: .06em; color: #e9ecef; margin: -.4rem 0 .6rem; }
    .fs-brand span { color: #ffd23f; }
    .fs-kicker { font: 500 .8rem 'Barlow Condensed', sans-serif; text-transform: uppercase; letter-spacing: .14em; color: #8a949e; }
    .fs-title { font: 700 1.75rem/1.1 'Barlow Condensed', sans-serif; color: #e9ecef; }
    .fs-h2 { font: 700 1.6rem/1.15 'Barlow Condensed', sans-serif; color: #e9ecef; margin: .15rem 0 .5rem; }
    .fs-lead { color: #b9c1c8; font-size: 1.02rem; line-height: 1.5; max-width: 38rem; }
    .fs-legend { color: #8a949e; font-size: .82rem; text-align: center; }
    .fs-legend b { color: #d4d9de; font-weight: 600; }
    .fs-step { display: inline-grid; place-items: center; width: 1.45rem; height: 1.45rem; border-radius: 50%;
        background: #ffd23f; color: #111417; font: 700 .85rem 'Barlow Condensed', sans-serif; margin-right: .35rem; }
    .fs-sub { color: #8a949e; font-size: .86rem; padding-left: 1.85rem; }
    .fs-footer { display: flex; justify-content: space-between; align-items: center; gap: 12px; flex-wrap: wrap;
        margin-top: 2.5rem; padding-top: 1rem; border-top: 1px solid #2a3036; color: #8a949e; font-size: .9rem; }
    .fs-footer a.fs-me { display: flex; align-items: center; gap: 10px; color: inherit; text-decoration: none; }
    .fs-footer a.fs-me:hover b { color: #ffd23f; }
    .fs-footer img { width: 36px; height: 36px; border-radius: 50%; border: 1px solid #3a4048; object-fit: cover; }
    .fs-footer b { color: #e9ecef; font-weight: 600; }
    .fs-footer .fs-course { font: 600 .95rem 'Barlow Condensed', sans-serif; letter-spacing: .12em; text-transform: uppercase; }
    .fs-footer .fs-course span { color: #ffd23f; }
    .fs-chips { display: flex; flex-wrap: wrap; gap: 8px; margin: .4rem 0; }
    .fs-chips span { border: 1px solid #2a3036; border-radius: 4px; padding: 3px 10px; font-size: .85rem; color: #b9c1c8; }
    </style>""")


def footer() -> None:
    st.markdown(
        "<div class='fs-footer'>"
        "<a class='fs-me' href='https://github.com/sarthak-panda' target='_blank' rel='noopener'>"
        "<img src='app/static/avatar.jpg' alt='sarthak-panda'/>"
        "<span><b>sarthak-panda</b> &nbsp;2022CS51217</span></a>"
        "<span class='fs-course'><span>MSL304</span> Project</span></div>",
        unsafe_allow_html=True)


def chips(items: list[str]) -> None:
    body = "".join(f"<span>{html.escape(i)}</span>" for i in items)
    st.markdown(f"<div class='fs-chips'>{body}</div>", unsafe_allow_html=True)


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
