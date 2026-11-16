"""Plotly Express charts for engine results (presentation only)."""
from __future__ import annotations

import pandas as pd
import plotly.express as px

QUEUE = "#FF8A3D"
SERVICE = "#5AA9E6"
IDLE = "#6B7682"
ALERT = "#EF5B4C"
OK = "#3FCF8E"
COST_COLORS = {"Staffing cost": SERVICE, "Waiting cost": QUEUE, "SLA penalty": ALERT}

_LAYOUT = dict(margin=dict(l=10, r=10, t=40, b=10), legend_title_text="", hovermode="x unified")


def records_frame(sim: dict) -> pd.DataFrame:
    return pd.DataFrame(sim.get("service_records", []))


def trace_frame(sim: dict) -> pd.DataFrame:
    return pd.DataFrame(sim.get("queue_trace", []))


def queue_chart(sim: dict, title: str = "Queue length and busy counters over time"):
    df = trace_frame(sim)
    if df.empty:
        return None
    long = df.melt(id_vars="time_min", value_vars=["queue_length", "busy_counters"], var_name="series", value_name="count")
    long["series"] = long["series"].map({"queue_length": "Customers waiting", "busy_counters": "Busy counters"})
    fig = px.line(long, x="time_min", y="count", color="series", line_shape="hv", title=title,
                  color_discrete_map={"Customers waiting": QUEUE, "Busy counters": SERVICE},
                  labels={"time_min": "Time since start (min)", "count": "Count"})
    fig.update_layout(**_LAYOUT)
    return fig


def compare_queue_chart(sims: dict[str, dict]):
    frames = []
    for label, sim in sims.items():
        df = trace_frame(sim)
        if not df.empty:
            frames.append(df.assign(plan=label))
    if not frames:
        return None
    df = pd.concat(frames, ignore_index=True)
    fig = px.line(df, x="time_min", y="queue_length", color="plan", line_shape="hv",
                  title="Queue length: current vs recommended (replication 0)",
                  color_discrete_sequence=[QUEUE, OK],
                  labels={"time_min": "Time since start (min)", "queue_length": "Customers waiting"})
    fig.update_layout(**_LAYOUT)
    return fig


def gantt_chart(sim: dict, sla_wait: float, max_rows: int = 3000):
    df = records_frame(sim)
    if df.empty:
        return None
    df = df.sort_values("service_start_min").head(max_rows).copy()
    df["Counter"] = "Counter " + df["counter_id"].astype(str)
    df["SLA"] = df["wait_time_min"].gt(sla_wait).map({True: "Waited beyond SLA", False: "Within SLA"})
    order = [f"Counter {i}" for i in range(1, int(sim["counter_count"]) + 1)]
    fig = px.bar(df, x="service_time_min", base="service_start_min", y="Counter", orientation="h", color="SLA",
                 color_discrete_map={"Within SLA": SERVICE, "Waited beyond SLA": ALERT},
                 category_orders={"Counter": order},
                 hover_data={"passenger_id": True, "arrival_time_min": ":.2f", "service_start_min": ":.2f",
                             "service_end_min": ":.2f", "wait_time_min": ":.2f", "service_time_min": ":.2f",
                             "Counter": False},
                 title="Service timeline by counter",
                 labels={"service_time_min": "Time since start (min)"})
    fig.update_traces(marker_line_width=0.5, marker_line_color="#111417")
    fig.update_layout(margin=_LAYOUT["margin"], legend_title_text="", bargap=0.25)
    fig.update_yaxes(title=None)
    return fig


def counter_utilization_chart(sim: dict):
    df = records_frame(sim)
    finish = sim["metrics"]["finish_time_min"]
    if df.empty or finish <= 0:
        return None
    busy = df.groupby("counter_id")["service_time_min"].sum()
    util = pd.DataFrame({"counter_id": range(1, int(sim["counter_count"]) + 1)})
    util["Busy %"] = util["counter_id"].map(busy).fillna(0.0) / finish * 100
    util["Counter"] = "Counter " + util["counter_id"].astype(str)
    fig = px.bar(util, x="Counter", y="Busy %", title="Busy time per counter (share of finish time)",
                 color_discrete_sequence=[SERVICE], range_y=[0, 100])
    fig.update_layout(**_LAYOUT)
    return fig


def cost_breakdown_chart(results: pd.DataFrame, best: int, baseline: int | None):
    long = results.melt(id_vars="counter_count",
                        value_vars=["mean_staffing_cost", "mean_waiting_cost", "mean_sla_penalty"],
                        var_name="component", value_name="cost")
    long["component"] = long["component"].map({"mean_staffing_cost": "Staffing cost",
                                               "mean_waiting_cost": "Waiting cost",
                                               "mean_sla_penalty": "SLA penalty"})
    fig = px.bar(long, x="counter_count", y="cost", color="component", color_discrete_map=COST_COLORS,
                 title="Mean objective cost by number of counters (stacked components)",
                 labels={"counter_count": "Counters", "cost": "Mean cost"})
    fig.update_xaxes(dtick=1)
    fig.add_vline(x=best, line_dash="dash", line_color=OK, annotation_text=f"Recommended: {best}",
                  annotation_position="top")
    if baseline is not None and baseline != best and baseline in set(results["counter_count"]):
        fig.add_vline(x=baseline, line_dash="dot", line_color="#8A949E", annotation_text=f"Current: {baseline}",
                      annotation_position="bottom")
    fig.update_layout(margin=_LAYOUT["margin"], legend_title_text="")
    return fig


def objective_curve_chart(results: pd.DataFrame):
    fig = px.line(results, x="counter_count", y="mean_objective_cost", markers=True,
                  error_y="stddev_objective_cost" if results["stddev_objective_cost"].gt(0).any() else None,
                  title="Total objective cost vs counters (error bars: 1 std. dev. across replications)",
                  labels={"counter_count": "Counters", "mean_objective_cost": "Mean total cost"},
                  color_discrete_sequence=[SERVICE])
    fig.update_xaxes(dtick=1)
    fig.update_layout(**_LAYOUT)
    return fig


def wait_curve_chart(results: pd.DataFrame, sla_wait: float):
    long = results.melt(id_vars="counter_count", value_vars=["mean_wait_min", "mean_p95_wait_min"],
                        var_name="measure", value_name="minutes")
    long["measure"] = long["measure"].map({"mean_wait_min": "Average wait", "mean_p95_wait_min": "95th percentile wait"})
    fig = px.line(long, x="counter_count", y="minutes", color="measure", markers=True,
                  color_discrete_map={"Average wait": SERVICE, "95th percentile wait": QUEUE},
                  title="Waiting time vs counters", labels={"counter_count": "Counters", "minutes": "Minutes"})
    fig.add_hline(y=sla_wait, line_dash="dot", line_color=ALERT, annotation_text=f"SLA {sla_wait:g} min")
    fig.update_xaxes(dtick=1)
    fig.update_layout(**_LAYOUT)
    return fig


def utilization_curve_chart(results: pd.DataFrame):
    fig = px.line(results, x="counter_count", y="mean_utilization_percent", markers=True,
                  title="Counter utilization vs counters", range_y=[0, 105],
                  labels={"counter_count": "Counters", "mean_utilization_percent": "Utilization (%)"},
                  color_discrete_sequence=[SERVICE])
    fig.update_xaxes(dtick=1)
    fig.update_layout(**_LAYOUT)
    return fig


def wait_histogram(sim: dict, sla_wait: float):
    df = records_frame(sim)
    if df.empty:
        return None
    fig = px.histogram(df, x="wait_time_min", nbins=40, title="Distribution of customer waiting times",
                       labels={"wait_time_min": "Wait (min)"}, color_discrete_sequence=[SERVICE])
    fig.add_vline(x=sla_wait, line_dash="dot", line_color=ALERT, annotation_text="SLA")
    fig.update_layout(margin=_LAYOUT["margin"], yaxis_title="Customers")
    return fig
