"""Python client: subprocess invocation, error propagation, parsing and CSV preparation."""
import copy

import pytest

from conftest import ROOT
from flowsim_client import FlowSimClient, FlowSimError
from scenario import SAMPLE_SCENARIO, build_document, parse_passenger_csv, passengers_from_frame, validate_scenario


def test_simulate_and_optimize_round_trip(client, sample_doc):
    sim = client.simulate(sample_doc, 4)
    assert sim["counter_count"] == 4 and sim["metrics"]["customers_served"] > 0
    opt = client.optimize(sample_doc)
    assert 1 <= opt["best_counter_count"] <= 8
    assert opt["baseline_counter_count"] == 4


def test_engine_validation_error_propagates(client, sample_doc):
    sample_doc["scenario"]["max_counters"] = 0
    with pytest.raises(FlowSimError) as exc:
        client.optimize(sample_doc)
    assert exc.value.code == "INVALID_INPUT"
    assert exc.value.field == "scenario.max_counters"


def test_missing_engine_is_reported(sample_doc, tmp_path):
    with pytest.raises(FlowSimError) as exc:
        FlowSimClient(cli_path=tmp_path / "missing_cli").optimize(sample_doc)
    assert exc.value.code == "ENGINE_NOT_FOUND"


def test_timeout_is_reported(client):
    doc = build_document({**copy.deepcopy(SAMPLE_SCENARIO), "arrival_rate_per_hour": 3000.0,
                          "arrival_window_minutes": 3000.0, "max_counters": 200, "replications": 200,
                          "parallel": False, "record_trace": False})
    with pytest.raises(FlowSimError) as exc:
        FlowSimClient(cli_path=client.cli_path, timeout_s=0.2).optimize(doc)
    assert exc.value.code == "TIMEOUT"


def test_csv_parsing_and_engine_run(client):
    frame, errors = parse_passenger_csv((ROOT / "data" / "sample_passengers.csv").read_bytes())
    assert errors == [] and len(frame) == 60
    passengers = passengers_from_frame(frame)
    scenario = {**copy.deepcopy(SAMPLE_SCENARIO), "input_mode": "csv"}
    assert validate_scenario(scenario, passengers) == []
    doc = build_document(scenario, passengers)
    assert "arrival_rate_per_hour" not in doc["scenario"] and doc["scenario"]["replications"] == 1
    result = client.optimize(doc)
    assert result["run_info"]["replications"] == 1
    assert result["recommended_simulation"]["metrics"]["customers_served"] == 60


@pytest.mark.parametrize("text, fragment", [
    (b"passenger_id,arrival_time_min\nA,1\n", "Missing required column"),
    (b"passenger_id,arrival_time_min,service_time_min\nA,x,1\n", "not a finite number"),
    (b"passenger_id,arrival_time_min,service_time_min\nA,1,0\n", "greater than zero"),
    (b"passenger_id,arrival_time_min,service_time_min\nA,-1,2\n", "non-negative"),
    (b"passenger_id,arrival_time_min,service_time_min\nA,1,2\nA,2,2\n", "Duplicate"),
    (b"passenger_id,arrival_time_min,service_time_min\n,1,2\n", "empty"),
])
def test_csv_errors(text, fragment):
    _, errors = parse_passenger_csv(text)
    assert any(fragment in e for e in errors), errors


def test_ui_validation_catches_bad_form_values():
    s = {**copy.deepcopy(SAMPLE_SCENARIO), "min_counters": 5, "max_counters": 2, "arrival_rate_per_hour": 0.0}
    errors = validate_scenario(s)
    assert any("Counters" in e for e in errors)
    assert any("Arrival rate" in e for e in errors)
