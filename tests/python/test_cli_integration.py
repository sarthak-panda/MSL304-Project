"""Calls the real flowsim_cli and checks the JSON contract."""
import json
import math
import subprocess

from conftest import ROOT

EXAMPLE = ROOT / "config" / "example_scenario.json"
SAMPLE_CSV = ROOT / "data" / "sample_passengers.csv"
METRIC_KEYS = {
    "customers_arrived", "customers_served", "finish_time_min", "mean_wait_min", "median_wait_min", "p95_wait_min",
    "max_wait_min", "mean_queue_length", "max_queue_length", "utilization_percent", "throughput_per_hour",
    "service_level_percent", "sla_breach_count", "staffing_cost", "waiting_cost", "sla_penalty", "total_objective_cost",
}


def run_cli(client, *args):
    return subprocess.run([str(client.cli_path), *args], capture_output=True, text=True, timeout=60, check=False)


def test_version(client):
    proc = run_cli(client, "version")
    assert proc.returncode == 0
    assert proc.stdout.startswith("flowsim_cli ")


def test_simulate_example_schema(client, tmp_path):
    out = tmp_path / "sim.json"
    proc = run_cli(client, "simulate", "--input", str(EXAMPLE), "--output", str(out), "--counters", "4")
    assert proc.returncode == 0, proc.stderr
    doc = json.loads(out.read_text())
    assert doc["ok"] is True and doc["schema_version"] == 1 and doc["run_type"] == "simulation"
    assert doc["counter_count"] == 4
    assert METRIC_KEYS <= set(doc["metrics"])
    m = doc["metrics"]
    assert m["customers_served"] == m["customers_arrived"] == len(doc["service_records"])
    assert all(math.isfinite(v) for v in m.values())
    assert math.isclose(m["total_objective_cost"], m["staffing_cost"] + m["waiting_cost"] + m["sla_penalty"], rel_tol=1e-9)
    for rec in doc["service_records"]:
        assert rec["service_start_min"] >= rec["arrival_time_min"]
        assert rec["service_end_min"] > rec["service_start_min"]


def test_optimize_example_schema(client, tmp_path):
    out = tmp_path / "opt.json"
    proc = run_cli(client, "optimize", "--input", str(EXAMPLE), "--output", str(out))
    assert proc.returncode == 0, proc.stderr
    doc = json.loads(out.read_text())
    assert doc["ok"] is True and doc["run_type"] == "optimization"
    counts = [r["counter_count"] for r in doc["results_by_counter_count"]]
    assert counts == list(range(1, 9))
    best = min(doc["results_by_counter_count"], key=lambda r: r["mean_objective_cost"])
    assert doc["best_counter_count"] == best["counter_count"]
    assert doc["baseline_counter_count"] == 4
    assert doc["recommended_simulation"]["counter_count"] == doc["best_counter_count"]
    assert doc["run_info"]["replications"] == 10


def test_simulate_matches_recommended_run(client, tmp_path):
    opt_out, sim_out = tmp_path / "opt.json", tmp_path / "sim.json"
    run_cli(client, "optimize", "--input", str(EXAMPLE), "--output", str(opt_out))
    best = json.loads(opt_out.read_text())["best_counter_count"]
    run_cli(client, "simulate", "--input", str(EXAMPLE), "--output", str(sim_out), "--counters", str(best))
    opt = json.loads(opt_out.read_text())["recommended_simulation"]["metrics"]
    sim = json.loads(sim_out.read_text())["metrics"]
    assert opt == sim


def test_csv_override(client, tmp_path):
    out = tmp_path / "csv.json"
    proc = run_cli(client, "optimize", "--input", str(EXAMPLE), "--output", str(out), "--passengers-csv", str(SAMPLE_CSV))
    assert proc.returncode == 0, proc.stderr
    doc = json.loads(out.read_text())
    assert doc["scenario"]["input_mode"] == "csv"
    assert doc["run_info"]["replications"] == 1
    assert all(r["stddev_objective_cost"] == 0 for r in doc["results_by_counter_count"])


def test_invalid_input_returns_error_json(client, tmp_path):
    bad = json.loads(EXAMPLE.read_text())
    bad["scenario"]["arrival_rate_per_hour"] = -5
    inp, out = tmp_path / "bad.json", tmp_path / "out.json"
    inp.write_text(json.dumps(bad))
    proc = run_cli(client, "optimize", "--input", str(inp), "--output", str(out))
    assert proc.returncode == 3
    doc = json.loads(out.read_text())
    assert doc["ok"] is False
    assert doc["error"]["code"] == "INVALID_INPUT"
    assert doc["error"]["field"] == "scenario.arrival_rate_per_hour"
    assert "arrival_rate_per_hour" in proc.stderr


def test_usage_errors(client, tmp_path):
    assert run_cli(client).returncode == 2
    assert run_cli(client, "simulate", "--input", str(EXAMPLE), "--output", str(tmp_path / "x.json")).returncode == 2
    assert run_cli(client, "optimize", "--input", str(EXAMPLE), "--output", str(tmp_path / "x.json"), "--bogus", "1").returncode == 2
    assert run_cli(client, "simulate", "--input", str(EXAMPLE), "--output", str(tmp_path / "x.json"), "--counters", "0").returncode == 3
    assert run_cli(client, "nonsense").returncode == 2


def test_malformed_json_rejected(client, tmp_path):
    inp, out = tmp_path / "bad.json", tmp_path / "out.json"
    inp.write_text("{not json")
    proc = run_cli(client, "optimize", "--input", str(inp), "--output", str(out))
    assert proc.returncode == 3
    assert json.loads(out.read_text())["error"]["code"] == "INVALID_INPUT"
