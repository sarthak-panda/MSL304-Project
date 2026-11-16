"""Headless Streamlit smoke test: every page renders and the main workflow runs the real engine."""
import json

from streamlit.testing.v1 import AppTest

from conftest import ROOT

APP = str(ROOT / "ui" / "app.py")


def page(at, name):
    at.sidebar.radio[0].set_value(name).run()
    assert not at.exception, at.exception


def click(at, label):
    buttons = [b for b in at.button if b.label == label]
    assert buttons, f"no button '{label}'"
    buttons[0].click().run()
    assert not at.exception, at.exception


def test_all_pages_render(client):
    at = AppTest.from_file(APP, default_timeout=90).run()
    assert not at.exception, at.exception
    for name in ["Live demo", "Scenario Builder", "Simulation", "Optimize Staffing", "Experiments & Export"]:
        page(at, name)


def test_main_workflow(client):
    at = AppTest.from_file(APP, default_timeout=90).run()
    page(at, "Simulation")
    click(at, "Run simulation")
    assert not at.error, [e.value for e in at.error]
    assert any(m.label == "Average wait" for m in at.metric)
    page(at, "Optimize Staffing")
    click(at, "Optimize staffing")
    assert not at.error, [e.value for e in at.error]
    assert any(m.label == "Recommended counters" for m in at.metric)
    page(at, "Experiments & Export")
    click(at, "Optimize with this seed")
    assert len(at.session_state["history"]) == 3
    assert at.session_state["opt_scenario"]["seed"] == 43


def test_csv_mode_workflow(client):
    at = AppTest.from_file(APP, default_timeout=90).run()
    page(at, "Scenario Builder")
    at.radio[0].set_value("csv").run()
    click(at, "Use sample CSV (synthetic data)")
    assert at.session_state["csv_frame"] is not None and not at.session_state["csv_errors"]
    page(at, "Optimize Staffing")
    click(at, "Optimize staffing")
    assert not at.error, [e.value for e in at.error]
    assert at.session_state["opt_result"]["run_info"]["replications"] == 1


def test_sample_csv_loads_in_csv_mode(client):
    at = AppTest.from_file(APP, default_timeout=90).run()
    page(at, "Scenario Builder")
    at.radio[0].set_value("csv").run()
    click(at, "Use sample CSV (synthetic data)")
    assert len(at.session_state["csv_frame"]) == 60


def test_live_demo_runs_sample(client):
    at = AppTest.from_file(APP, default_timeout=90).run()
    click(at, "Load sample and simulate")
    assert not at.error, [e.value for e in at.error]
    opt = at.session_state["opt_result"]
    assert opt["baseline_simulation"]["counter_count"] == 4
    assert opt["recommended_simulation"]["counter_count"] == opt["best_counter_count"]
    assert at.session_state["live_run"] == 1


def test_live_payload_matches_engine_records(client):
    import sys
    sys.path.insert(0, str(ROOT / "ui"))
    from live_demo import _compact
    sim = client.simulate(json.loads((ROOT / "config" / "example_scenario.json").read_text()), 4)
    payload = _compact(sim, "Today")
    recs = sim["service_records"]
    assert payload["counters"] == 4 and len(payload["arrival"]) == len(recs)
    assert payload["arrival"] == sorted(payload["arrival"])
    assert sorted(zip(payload["arrival"], payload["start"], payload["end"], payload["counter"])) == sorted(
        (r["arrival_time_min"], r["service_start_min"], r["service_end_min"], r["counter_id"]) for r in recs)
    assert payload["metrics"]["total_objective_cost"] == sim["metrics"]["total_objective_cost"]
