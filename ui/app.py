"""FlowSim Streamlit app. Run: streamlit run ui/app.py"""
from __future__ import annotations

import copy
import json
import sys
from pathlib import Path

import pandas as pd
import streamlit as st

sys.path.insert(0, str(Path(__file__).resolve().parent))

import charts  # noqa: E402
import components as ui  # noqa: E402
from flowsim_client import REPO_ROOT, FlowSimClient, FlowSimError  # noqa: E402
from scenario import (MAX_COUNTERS, SAMPLE_SCENARIO, MAX_REPLICATIONS, MAX_THREADS, build_document, parse_passenger_csv,  # noqa: E402
                      passengers_from_frame, validate_scenario)

SAMPLE_CSV = REPO_ROOT / "data" / "sample_passengers.csv"
PAGES = ["Overview", "Scenario Builder", "Simulation", "Optimize Staffing", "Experiments & Export"]

st.set_page_config(page_title="FlowSim", layout="wide", initial_sidebar_state="expanded")
ui.inject_css()
ui.init_state()
client = FlowSimClient()


def current_passengers() -> list[dict] | None:
    frame = st.session_state.csv_frame
    if frame is None or st.session_state.csv_errors:
        return None
    return passengers_from_frame(frame)


def on_csv_upload() -> None:
    upload = st.session_state.csv_upload
    if upload is None:
        return
    frame, errors = parse_passenger_csv(upload.getvalue())
    st.session_state.csv_frame, st.session_state.csv_errors = frame, errors
    st.session_state.csv_name = upload.name
    st.session_state.is_sample = False


def scenario_ready() -> tuple[dict, list[dict] | None, list[str]]:
    scenario = copy.deepcopy(st.session_state.scenario)
    passengers = current_passengers() if scenario["input_mode"] == "csv" else None
    errors = validate_scenario(scenario, passengers)
    if scenario["input_mode"] == "csv" and st.session_state.csv_errors:
        errors.insert(0, "The uploaded CSV has validation errors; fix them on the Scenario Builder page.")
    return scenario, passengers, errors


def show_engine_error(err: FlowSimError) -> None:
    st.error(f"**Engine error ({err.code})**: {err}")


def illustrative_badge() -> None:
    if st.session_state.is_sample and st.session_state.scenario["input_mode"] == "synthetic":
        st.caption(":orange-badge[Illustrative sample data] Parameters are example values, not measured airport data.")


def run_simulation(counters: int) -> bool:
    scenario, passengers, errors = scenario_ready()
    if errors:
        for e in errors:
            st.error(e)
        return False
    try:
        with st.spinner(f"Running C++ simulation with {counters} counter(s)..."):
            result = client.simulate(build_document(scenario, passengers), counters)
    except FlowSimError as err:
        show_engine_error(err)
        return False
    st.session_state.sim_result = result
    st.session_state.sim_scenario = scenario
    m = result["metrics"]
    ui.add_history("simulation", scenario, {"counters": counters, "total_cost": round(m["total_objective_cost"], 2),
                                            "mean_wait_min": round(m["mean_wait_min"], 3)})
    return True


def run_optimization(scenario_override: dict | None = None) -> bool:
    if scenario_override is not None:
        st.session_state.scenario = scenario_override
    scenario, passengers, errors = scenario_ready()
    if errors:
        for e in errors:
            st.error(e)
        return False
    try:
        with st.spinner("Evaluating every staffing level in the C++ engine..."):
            result = client.optimize(build_document(scenario, passengers))
    except FlowSimError as err:
        show_engine_error(err)
        return False
    st.session_state.opt_result = result
    st.session_state.opt_scenario = scenario
    ui.add_history("optimization", scenario, {"counters": result["best_counter_count"],
                                              "total_cost": round(result["best_objective_cost"], 2),
                                              "mean_wait_min": round(result["recommended_simulation"]["metrics"]["mean_wait_min"], 3)})
    return True


# ---------------------------------------------------------------- pages

def page_overview() -> None:
    st.title("FlowSim")
    st.markdown("Discrete-event simulation and staffing optimization for service counters. "
                "Test how many counters to open before committing staff, using a C++ simulation engine.")
    illustrative_badge()
    s = st.session_state.scenario
    left, right = st.columns([2, 1])
    with left:
        st.subheader("Latest result")
        opt, sim = st.session_state.opt_result, st.session_state.sim_result
        if opt:
            st.caption(f"Recommended plan from the last optimization: **{opt['best_counter_count']} counters** "
                       f"(representative replication, seed {opt['scenario'].get('seed', 'fixed CSV')}).")
            ui.kpi_row(opt["recommended_simulation"]["metrics"])
        elif sim:
            st.caption(f"Last simulation: **{sim['counter_count']} counters**.")
            ui.kpi_row(sim["metrics"])
        else:
            st.info("No runs yet. Load the sample scenario, then run a simulation or optimize staffing.")
        b1, b2, b3 = st.columns(3)
        if b1.button("Load sample airport scenario", width="stretch"):
            ui.load_sample()
            st.rerun()
        if b2.button("Run baseline simulation", width="stretch", type="primary"):
            if run_simulation(int(s.get("current_counters") or s["min_counters"])):
                st.rerun()
        if b3.button("Optimize staffing", width="stretch", type="primary", key="overview_opt"):
            if run_optimization():
                st.rerun()
    with right:
        st.subheader("Scenario")
        rows = {"Name": s["name"], "Input": s["input_mode"]}
        if s["input_mode"] == "synthetic":
            rows.update({"Arrival rate": f"{s['arrival_rate_per_hour']:g} / hour",
                         "Arrival window": f"{s['arrival_window_minutes']:g} min",
                         "Mean service": f"{s['mean_service_time_minutes']:g} min ({s['service_time_distribution']})",
                         "Seed / replications": f"{s['seed']} / {s['replications']}"})
        else:
            frame = st.session_state.csv_frame
            rows["Passengers"] = f"{0 if frame is None else len(frame):,} from {st.session_state.csv_name or 'no file'}"
        rows.update({"Counter range": f"{s['min_counters']} to {s['max_counters']}",
                     "Current counters": str(s.get("current_counters") or "not set"),
                     "Counter cost": f"{ui.money(s['counter_cost_per_hour'])} / hour",
                     "Waiting cost": f"{ui.money(s['waiting_cost_per_passenger_hour'])} / customer-hour",
                     "SLA": f"wait <= {s['sla_wait_minutes']:g} min, penalty {ui.money(s['sla_penalty_per_passenger'])}"})
        st.dataframe(pd.DataFrame(rows.items(), columns=["", "value"]), hide_index=True, width="stretch")
    st.caption("Educational decision-support prototype. Recommendations are minimum-cost under the stated "
               "assumptions and cost weights; they are not validated for a real facility without calibration.")


def page_builder() -> None:
    st.title("Scenario Builder")
    illustrative_badge()
    s = st.session_state.scenario
    top = st.columns([1, 1, 2])
    if top[0].button("Load sample airport scenario", width="stretch"):
        ui.load_sample()
        st.rerun()
    st.session_state.currency = top[1].selectbox("Currency label", list(ui.CURRENCIES),
                                                 index=list(ui.CURRENCIES).index(st.session_state.currency),
                                                 help="Display only. The engine treats costs as generic units.")

    mode = st.radio("Input mode", ["synthetic", "csv"], horizontal=True,
                    index=0 if s["input_mode"] == "synthetic" else 1,
                    format_func=lambda m: "Synthetic arrivals (Poisson)" if m == "synthetic" else "Uploaded customer CSV")
    if mode != s["input_mode"]:
        s["input_mode"] = mode
        st.rerun()

    if mode == "csv":
        st.subheader("Customer data")
        st.caption("Required columns: `passenger_id, arrival_time_min, service_time_min`. Rows may be in any order. "
                   "A fixed dataset is simulated once, so replication controls do not apply.")
        c1, c2 = st.columns([2, 1])
        c1.file_uploader("Upload CSV", type=["csv"], key="csv_upload", on_change=on_csv_upload)
        if c2.button("Use sample CSV (synthetic data)", width="stretch"):
            frame, errors = parse_passenger_csv(SAMPLE_CSV.read_bytes())
            st.session_state.csv_frame, st.session_state.csv_errors = frame, errors
            st.session_state.csv_name = SAMPLE_CSV.name
        if st.session_state.csv_errors:
            for e in st.session_state.csv_errors:
                st.error(e)
        frame = st.session_state.csv_frame
        if frame is not None:
            if st.session_state.csv_errors:
                st.warning(f"{len(frame):,} rows read from {st.session_state.csv_name}, but the file cannot be used "
                           "until the errors above are fixed.")
            else:
                st.success(f"{len(frame):,} rows loaded from {st.session_state.csv_name}.")
            st.dataframe(frame.head(10), hide_index=True, width="stretch")

    with st.form(f"scenario_form_{st.session_state.form_version}"):
        st.subheader("Scenario settings")
        name = st.text_input("Scenario name", s["name"])
        if mode == "synthetic":
            c = st.columns(4)
            window = c[0].number_input("Arrival window (minutes)", 0.0, 10080.0, float(s["arrival_window_minutes"]), 10.0)
            rate = c[1].number_input("Arrival rate (customers / hour)", 0.01, 1e6, float(s["arrival_rate_per_hour"]), 1.0)
            svc = c[2].number_input("Mean service time (minutes / customer)", 0.01, 1440.0,
                                    float(s["mean_service_time_minutes"]), 0.5)
            dist = c[3].selectbox("Service-time distribution", ["exponential", "deterministic"],
                                  index=0 if s["service_time_distribution"] == "exponential" else 1)
        c = st.columns(3)
        lo = c[0].number_input("Minimum counters", 1, MAX_COUNTERS, int(s["min_counters"]))
        hi = c[1].number_input("Maximum counters", 1, MAX_COUNTERS, int(s["max_counters"]))
        cur = c[2].number_input("Current counters (baseline, 0 = none)", 0, MAX_COUNTERS, int(s.get("current_counters") or 0))
        c = st.columns(4)
        sym = ui.CURRENCIES[st.session_state.currency] or "units"
        counter_cost = c[0].number_input(f"Counter cost ({sym} / counter-hour)", 0.0, 1e9, float(s["counter_cost_per_hour"]), 10.0)
        wait_cost = c[1].number_input(f"Waiting cost ({sym} / customer-hour)", 0.0, 1e9,
                                      float(s["waiting_cost_per_passenger_hour"]), 5.0)
        sla = c[2].number_input("SLA wait threshold (minutes)", 0.0, 1e9, float(s["sla_wait_minutes"]), 1.0)
        penalty = c[3].number_input(f"Penalty per SLA breach ({sym})", 0.0, 1e9, float(s["sla_penalty_per_passenger"]), 5.0)
        c = st.columns(4)
        synthetic = mode == "synthetic"
        reps = c[0].number_input("Replications", 1, MAX_REPLICATIONS, int(s["replications"]), disabled=not synthetic,
                                 help="Independent synthetic datasets (seed, seed+1, ...). Not used for CSV data.")
        seed = c[1].number_input("Random seed", 0, 2**53 - 1, int(s["seed"]), disabled=not synthetic)
        parallel = c[2].toggle("Parallel evaluation", bool(s["parallel"]))
        threads = c[3].number_input("Thread limit", 1, MAX_THREADS, int(s["max_threads"]))
        if st.form_submit_button("Save scenario", type="primary"):
            new = dict(s)
            new.update({"name": name.strip(), "input_mode": mode, "min_counters": int(lo), "max_counters": int(hi),
                        "current_counters": int(cur) or None, "counter_cost_per_hour": float(counter_cost),
                        "waiting_cost_per_passenger_hour": float(wait_cost), "sla_wait_minutes": float(sla),
                        "sla_penalty_per_passenger": float(penalty), "replications": int(reps), "seed": int(seed),
                        "parallel": bool(parallel), "max_threads": int(threads)})
            if synthetic:
                new.update({"arrival_window_minutes": float(window), "arrival_rate_per_hour": float(rate),
                            "mean_service_time_minutes": float(svc), "service_time_distribution": dist})
            errors = validate_scenario(new, current_passengers() if mode == "csv" else [{}])
            if errors:
                for e in errors:
                    st.error(e)
            else:
                st.session_state.scenario = new
                st.session_state.is_sample = new == SAMPLE_SCENARIO
                st.success("Scenario saved. Run it from the Simulation or Optimize Staffing page.")
    if mode == "synthetic":
        load = s["arrival_rate_per_hour"] * s["mean_service_time_minutes"] / 60.0
        st.caption(f"Offered load = arrival rate x mean service time = {load:.2f} counters busy on average. "
                   f"Fewer counters than this cannot keep up during the arrival window.")


def page_simulation() -> None:
    st.title("Simulation")
    illustrative_badge()
    s = st.session_state.scenario
    default = int(s.get("current_counters") or s["min_counters"])
    if st.session_state.opt_result:
        default = int(st.session_state.opt_result["best_counter_count"])
    c1, c2 = st.columns([1, 3])
    counters = c1.number_input("Counters to staff", 1, MAX_COUNTERS, default)
    c2.write("")
    if c2.button("Run simulation", type="primary"):
        run_simulation(int(counters))
    sim = st.session_state.sim_result
    if not sim:
        st.info("Choose a counter count and press **Run simulation**.")
        return
    used = st.session_state.sim_scenario
    st.caption(f"Scenario **{used['name']}**, {sim['counter_count']} counters, "
               + (f"seed {used['seed']} (replication 0)" if used["input_mode"] == "synthetic" else "fixed CSV dataset")
               + f", {sim['events_processed']:,} events processed.")
    ui.kpi_row(sim["metrics"])
    m = sim["metrics"]
    st.caption(ui.md_escape(f"Cost breakdown: staffing {ui.money(m['staffing_cost'])} + waiting {ui.money(m['waiting_cost'])} "
               f"+ SLA penalty {ui.money(m['sla_penalty'])} = {ui.money(m['total_objective_cost'])}."))

    tab_timeline, tab_queue, tab_play, tab_records = st.tabs(["Timeline", "Queue & utilization", "Playback", "Service records"])
    with tab_timeline:
        fig = charts.gantt_chart(sim, used["sla_wait_minutes"])
        if fig:
            st.plotly_chart(fig, width="stretch")
        else:
            st.info("No customers were served.")
        if len(sim["service_records"]) > 3000:
            st.caption("Timeline shows the first 3,000 services for readability; the table has all records.")
    with tab_queue:
        for fig in (charts.queue_chart(sim), charts.counter_utilization_chart(sim), charts.wait_histogram(sim, used["sla_wait_minutes"])):
            if fig:
                st.plotly_chart(fig, width="stretch")
    with tab_play:
        trace = charts.trace_frame(sim)
        finish = float(m["finish_time_min"])
        if trace.empty or finish <= 0:
            st.info("Nothing to play back for an empty run.")
        else:
            t = st.slider("Simulation time (min)", 0.0, finish, min(finish, finish / 2), step=max(finish / 500, 0.01))
            point = trace[trace["time_min"] <= t].iloc[-1]
            recs = charts.records_frame(sim)
            in_service = recs[(recs["service_start_min"] <= t) & (recs["service_end_min"] > t)]
            arrived = int((recs["arrival_time_min"] <= t).sum())
            done = int((recs["service_end_min"] <= t).sum())
            k = st.columns(4)
            k[0].metric("Customers waiting", int(point["queue_length"]), border=True)
            k[1].metric("Busy counters", f"{int(point['busy_counters'])} / {sim['counter_count']}", border=True)
            k[2].metric("Arrived so far", arrived, border=True)
            k[3].metric("Served so far", done, border=True)
            status = pd.DataFrame({"counter": range(1, sim["counter_count"] + 1)})
            busy = in_service.set_index("counter_id")
            status["state"] = status["counter"].map(lambda c: "Busy" if c in busy.index else "Idle")
            status["passenger"] = status["counter"].map(lambda c: busy.loc[c, "passenger_id"] if c in busy.index else "")
            status["finishes at (min)"] = status["counter"].map(
                lambda c: round(float(busy.loc[c, "service_end_min"]), 2) if c in busy.index else None)
            st.dataframe(status, hide_index=True, width="stretch")
    with tab_records:
        recs = charts.records_frame(sim)
        st.dataframe(recs.round(3), hide_index=True, width="stretch", height=380)
        st.download_button("Download service records (CSV)", recs.to_csv(index=False), "service_records.csv", "text/csv")


def explanation(opt: dict, results: pd.DataFrame) -> str:
    best = opt["best_counter_count"]
    row = results.set_index("counter_count")
    b = row.loc[best]
    parts = [f"**{best} counters** has the lowest mean total cost ({ui.money(b.mean_objective_cost)}) among the "
             f"{len(results)} staffing levels evaluated over {opt['run_info']['replications']} replication(s)."]
    if best - 1 in row.index:
        lo = row.loc[best - 1]
        parts.append(f"With {best - 1} counters staffing falls by {ui.money(b.mean_staffing_cost - lo.mean_staffing_cost)}, "
                     f"but waiting cost and SLA penalties rise by "
                     f"{ui.money((lo.mean_waiting_cost + lo.mean_sla_penalty) - (b.mean_waiting_cost + b.mean_sla_penalty))}.")
    if best + 1 in row.index:
        hi = row.loc[best + 1]
        parts.append(f"With {best + 1} counters waiting and penalties drop by only "
                     f"{ui.money((b.mean_waiting_cost + b.mean_sla_penalty) - (hi.mean_waiting_cost + hi.mean_sla_penalty))} "
                     f"while staffing grows by {ui.money(hi.mean_staffing_cost - b.mean_staffing_cost)}.")
    parts.append("This is the minimum-cost choice under the configured assumptions and cost weights, not a universal optimum.")
    return ui.md_escape(" ".join(parts))


def page_optimize() -> None:
    st.title("Optimize Staffing")
    illustrative_badge()
    s = st.session_state.scenario
    st.caption(f"Evaluates every counter count from {s['min_counters']} to {s['max_counters']} on the same customer "
               f"samples (common random numbers) and picks the lowest mean of staffing + waiting + SLA cost.")
    if st.button("Optimize staffing", type="primary"):
        run_optimization()
    opt = st.session_state.opt_result
    if not opt:
        st.info("Press **Optimize staffing** to evaluate all staffing levels.")
        return
    used = st.session_state.opt_scenario
    info = opt["run_info"]
    st.caption(f"Scenario **{opt['scenario_name']}**; "
               + (f"seed {used['seed']}, {info['replications']} replications" if used["input_mode"] == "synthetic"
                  else "fixed CSV dataset, 1 run per staffing level")
               + f"; {'parallel' if info['parallel'] else 'sequential'} on {info['threads_used']} thread(s), "
               f"engine time {info['runtime_ms']:.1f} ms.")
    results = pd.DataFrame(opt["results_by_counter_count"])
    best = opt["best_counter_count"]
    base = opt.get("baseline")
    k = st.columns(4)
    k[0].metric("Recommended counters", best, border=True)
    k[1].metric("Recommended mean cost", ui.money(opt["best_objective_cost"]), help=ui.HELP["cost"], border=True)
    if base:
        k[2].metric(f"Current ({base['counter_count']} counters) mean cost", ui.money(base["mean_objective_cost"]), border=True)
        k[3].metric("Estimated savings", ui.money(opt["objective_savings"]),
                    delta=ui.pct(opt.get("objective_savings_percent"), signed=True) if opt.get("objective_savings_percent") is not None else None,
                    border=True)
    else:
        k[2].info("Set current counters in the Scenario Builder to compare against a baseline.")
    st.markdown(explanation(opt, results))

    if base:
        best_row = results.set_index("counter_count").loc[best]
        comp = pd.DataFrame({
            "measure": ["Mean total cost", "Staffing cost", "Waiting cost", "SLA penalty", "Average wait (min)",
                        "p95 wait (min)", "Utilization (%)", "Service level (%)", "Max queue length"],
            f"Current ({base['counter_count']})": [base["mean_objective_cost"], base["mean_staffing_cost"],
                                                  base["mean_waiting_cost"], base["mean_sla_penalty"], base["mean_wait_min"],
                                                  base["mean_p95_wait_min"], base["mean_utilization_percent"],
                                                  base["mean_service_level_percent"], base["mean_max_queue_length"]],
            f"Recommended ({best})": [best_row.mean_objective_cost, best_row.mean_staffing_cost, best_row.mean_waiting_cost,
                                     best_row.mean_sla_penalty, best_row.mean_wait_min, best_row.mean_p95_wait_min,
                                     best_row.mean_utilization_percent, best_row.mean_service_level_percent,
                                     best_row.mean_max_queue_length]})
        st.subheader("Current vs recommended (means across replications)")
        st.dataframe(comp.round(2), hide_index=True, width="stretch")
        if opt.get("wait_change_percent") is not None:
            st.caption(f"Average wait changes by {ui.pct(opt['wait_change_percent'], signed=True)} versus the current plan.")

    t1, t2, t3 = st.tabs(["Cost trade-off", "Service and utilization", "Representative runs"])
    with t1:
        st.plotly_chart(charts.cost_breakdown_chart(results, best, base["counter_count"] if base else None), width="stretch")
        st.plotly_chart(charts.objective_curve_chart(results), width="stretch")
    with t2:
        st.plotly_chart(charts.wait_curve_chart(results, used["sla_wait_minutes"]), width="stretch")
        st.plotly_chart(charts.utilization_curve_chart(results), width="stretch")
    with t3:
        sims = {}
        if opt.get("baseline_simulation"):
            sims[f"Current ({opt['baseline_simulation']['counter_count']})"] = opt["baseline_simulation"]
        sims[f"Recommended ({best})"] = opt["recommended_simulation"]
        fig = charts.compare_queue_chart(sims)
        if fig:
            st.plotly_chart(fig, width="stretch")
        else:
            st.info("Enable trace recording to see representative runs.")
        fig = charts.gantt_chart(opt["recommended_simulation"], used["sla_wait_minutes"])
        if fig:
            st.plotly_chart(fig, width="stretch")
    st.subheader("All evaluated staffing levels")
    st.dataframe(results.round(3), hide_index=True, width="stretch")


def page_export() -> None:
    st.title("Experiments & Export")
    s = st.session_state.scenario
    st.subheader("Rerun")
    c = st.columns([1, 1, 2])
    if s["input_mode"] == "synthetic":
        new_seed = c[0].number_input("Seed for rerun", 0, 2**53 - 1, (int(s["seed"]) + 1) % 2**53)
        if c[1].button("Optimize with this seed", type="primary"):
            run_optimization({**s, "seed": int(new_seed)})
    else:
        st.caption("Reruns with a different seed apply to synthetic mode; CSV data is deterministic.")

    st.subheader("Downloads")
    opt, sim = st.session_state.opt_result, st.session_state.sim_result
    d = st.columns(4)
    if opt:
        d[0].download_button("Optimization results (CSV)", pd.DataFrame(opt["results_by_counter_count"]).to_csv(index=False),
                             "optimization_results.csv", "text/csv")
        d[1].download_button("Optimization result (JSON)", json.dumps(opt, indent=2), "optimization_result.json",
                             "application/json")
    if sim:
        d[2].download_button("Simulation result (JSON)", json.dumps(sim, indent=2), "simulation_result.json",
                             "application/json")
    passengers = current_passengers() if s["input_mode"] == "csv" else None
    d[3].download_button("Scenario input (JSON)", json.dumps(build_document(s, passengers), indent=2), "scenario.json",
                         "application/json")
    if not opt and not sim:
        st.info("Run a simulation or optimization to enable result downloads.")

    st.subheader("Run history (this session)")
    if st.session_state.history:
        st.dataframe(pd.DataFrame(st.session_state.history), hide_index=True, width="stretch")
    else:
        st.caption("No runs yet.")
    st.subheader("Assumptions used")
    shown = st.session_state.opt_scenario or st.session_state.sim_scenario or s
    st.dataframe(pd.DataFrame(ui.settings_table(shown)), hide_index=True, width="stretch")
    st.markdown("- Identical parallel counters, one FIFO queue, no abandonment or breaks.\n"
                "- Constant staffing over the arrival window and the drain period.\n"
                "- Costs are generic monetary units; the currency label is display only.")


# ---------------------------------------------------------------- layout

with st.sidebar:
    st.markdown("### FlowSim")
    st.caption("Queue simulation and staffing optimization")
    page = st.radio("Navigate", PAGES, label_visibility="collapsed")
    st.divider()
    if st.button("Load sample airport scenario", key="sidebar_sample", width="stretch"):
        ui.load_sample()
        st.rerun()
    if client.available():
        try:
            st.caption(f"Engine: `{client.version()}`")
        except FlowSimError as err:
            st.error(str(err))
    else:
        st.error("C++ engine not built. Run `./scripts/build.sh` and reload.")

PAGE_FUNCS = {"Overview": page_overview, "Scenario Builder": page_builder, "Simulation": page_simulation,
              "Optimize Staffing": page_optimize, "Experiments & Export": page_export}
PAGE_FUNCS[page]()
