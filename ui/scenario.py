"""Scenario defaults, UI-side validation and CSV preparation (no simulation logic here)."""
from __future__ import annotations

import csv
import io
import math
from typing import Any

import pandas as pd

SCHEMA_VERSION = 1
MAX_PASSENGERS = 200_000
MAX_COUNTERS = 200
MAX_REPLICATIONS = 1_000
MAX_THREADS = 64
MAX_WINDOW_MIN = 10_080.0
CSV_COLUMNS = ("passenger_id", "arrival_time_min", "service_time_min")

SAMPLE_SCENARIO: dict[str, Any] = {
    "name": "Airport security morning peak (illustrative)",
    "input_mode": "synthetic",
    "arrival_window_minutes": 240.0,
    "arrival_rate_per_hour": 48.0,
    "mean_service_time_minutes": 5.5,
    "service_time_distribution": "exponential",
    "min_counters": 1,
    "max_counters": 8,
    "current_counters": 4,
    "counter_cost_per_hour": 250.0,
    "waiting_cost_per_passenger_hour": 35.0,
    "sla_wait_minutes": 10.0,
    "sla_penalty_per_passenger": 20.0,
    "replications": 10,
    "seed": 42,
    "parallel": True,
    "max_threads": 4,
    "record_trace": True,
}

SYNTHETIC_ONLY = ("arrival_window_minutes", "arrival_rate_per_hour", "mean_service_time_minutes",
                  "service_time_distribution")


def _finite(x: Any) -> bool:
    return isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x)


def validate_scenario(s: dict[str, Any], passengers: list[dict] | None = None) -> list[str]:
    """Returns human-readable problems. The C++ engine re-validates everything."""
    errors: list[str] = []
    if not str(s.get("name", "")).strip():
        errors.append("Scenario name must not be empty.")
    mode = s.get("input_mode")
    if mode == "synthetic":
        rate, window, svc = s.get("arrival_rate_per_hour"), s.get("arrival_window_minutes"), s.get("mean_service_time_minutes")
        if not (_finite(rate) and rate > 0):
            errors.append("Arrival rate must be greater than zero.")
        if not (_finite(window) and 0 <= window <= MAX_WINDOW_MIN):
            errors.append(f"Arrival window must be between 0 and {MAX_WINDOW_MIN:g} minutes.")
        if not (_finite(svc) and 0 < svc <= 1440):
            errors.append("Mean service time must be greater than 0 and at most 1440 minutes.")
        if _finite(rate) and _finite(window) and rate * window / 60 > MAX_PASSENGERS:
            errors.append(f"Expected passengers (rate x window) exceed the limit of {MAX_PASSENGERS:,}.")
        if s.get("service_time_distribution") not in ("exponential", "deterministic"):
            errors.append("Service-time distribution must be exponential or deterministic.")
    elif mode == "csv":
        if not passengers:
            errors.append("CSV mode needs an uploaded passenger file with at least one valid row.")
    else:
        errors.append("Input mode must be synthetic or csv.")

    lo, hi, cur = s.get("min_counters"), s.get("max_counters"), s.get("current_counters")
    if not (isinstance(lo, int) and isinstance(hi, int) and 1 <= lo <= hi <= MAX_COUNTERS):
        errors.append(f"Counters must satisfy 1 <= minimum <= maximum <= {MAX_COUNTERS}.")
    if cur is not None and not (isinstance(cur, int) and 1 <= cur <= MAX_COUNTERS):
        errors.append(f"Current counters must be between 1 and {MAX_COUNTERS}.")
    for key, label in (("counter_cost_per_hour", "Counter cost"), ("waiting_cost_per_passenger_hour", "Waiting cost"),
                       ("sla_wait_minutes", "SLA wait threshold"), ("sla_penalty_per_passenger", "SLA penalty")):
        if not (_finite(s.get(key)) and s[key] >= 0):
            errors.append(f"{label} must be a non-negative number.")
    reps = s.get("replications")
    if not (isinstance(reps, int) and 1 <= reps <= MAX_REPLICATIONS):
        errors.append(f"Replications must be between 1 and {MAX_REPLICATIONS}.")
    seed = s.get("seed")
    if not (isinstance(seed, int) and 0 <= seed <= 2**53 - 1):
        errors.append("Seed must be a non-negative integer.")
    threads = s.get("max_threads")
    if not (isinstance(threads, int) and 1 <= threads <= MAX_THREADS):
        errors.append(f"Thread limit must be between 1 and {MAX_THREADS}.")
    return errors


def build_document(s: dict[str, Any], passengers: list[dict] | None = None) -> dict[str, Any]:
    scenario = dict(s)
    if scenario.get("current_counters") is None:
        scenario.pop("current_counters", None)
    if scenario.get("input_mode") == "csv":
        for key in SYNTHETIC_ONLY:
            scenario.pop(key, None)
        scenario["passengers"] = passengers or []
        scenario["replications"] = 1
    return {"schema_version": SCHEMA_VERSION, "scenario": scenario}


def parse_passenger_csv(data: bytes) -> tuple[pd.DataFrame | None, list[str]]:
    """Parses and checks an uploaded CSV. Returns (frame, errors); frame is None when unusable."""
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError as exc:
        return None, [f"Could not read CSV: {exc}"]
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    try:
        parsed = list(csv.reader(io.StringIO(text), skipinitialspace=True, strict=True))
    except csv.Error as exc:
        return None, [f"Could not read CSV: {exc}"]
    rows = [r for r in parsed if any(field.strip() for field in r)]
    if not rows:
        return None, ["Could not read CSV: the file is empty."]
    header = [c.strip() for c in rows[0]]
    duplicated = sorted({c for c in header if header.count(c) > 1})
    if duplicated:
        return None, [f"Duplicate column name(s): {', '.join(duplicated)}."]
    bad_width = [i + 1 for i, r in enumerate(rows[1:], start=1) if len(r) != len(header)]
    if bad_width:
        listed = ", ".join(str(n) for n in bad_width[:5])
        return None, [f"Row(s) {listed}: expected {len(header)} fields to match the header."]
    df = pd.DataFrame(rows[1:], columns=header, dtype=str)
    missing = [c for c in CSV_COLUMNS if c not in df.columns]
    if missing:
        return None, [f"Missing required column(s): {', '.join(missing)}. Expected {', '.join(CSV_COLUMNS)}."]
    if len(df) > MAX_PASSENGERS:
        return None, [f"CSV has {len(df):,} rows; the limit is {MAX_PASSENGERS:,}."]

    df = df[list(CSV_COLUMNS)].copy()
    df["passenger_id"] = df["passenger_id"].str.strip()
    errors: list[str] = []
    for col in ("arrival_time_min", "service_time_min"):
        raw = df[col].str.strip()
        df[col] = pd.to_numeric(raw, errors="coerce")
        bad = df[col].isna() | ~df[col].apply(lambda v: math.isfinite(v) if pd.notna(v) else False)
        for idx in df.index[bad][:5]:
            errors.append(f"Row {idx + 2}: {col} '{raw[idx]}' is not a finite number.")
    empty = df["passenger_id"] == ""
    for idx in df.index[empty][:5]:
        errors.append(f"Row {idx + 2}: passenger_id is empty.")
    dups = df["passenger_id"][df["passenger_id"].duplicated() & ~empty].unique()
    if len(dups):
        errors.append(f"Duplicate passenger_id values: {', '.join(map(str, dups[:5]))}.")
    for idx in df.index[df["arrival_time_min"] < 0][:5]:
        errors.append(f"Row {idx + 2}: arrival_time_min must be non-negative.")
    for idx in df.index[df["service_time_min"] <= 0][:5]:
        errors.append(f"Row {idx + 2}: service_time_min must be greater than zero.")
    return df, errors


def passengers_from_frame(df: pd.DataFrame) -> list[dict[str, Any]]:
    return [
        {"passenger_id": str(r.passenger_id), "arrival_time_min": float(r.arrival_time_min),
         "service_time_min": float(r.service_time_min)}
        for r in df.itertuples(index=False)
    ]
