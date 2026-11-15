# Architecture

```
Streamlit UI (ui/app.py)
  | builds scenario JSON, validates CSV, draws charts
  v
FlowSimClient (ui/flowsim_client.py)
  | subprocess.run([...], shell not used), temp JSON files, timeout
  v
flowsim_cli (src/main.cpp)
  | parse + validate (json_io, validation, csv_loader)
  v
flowsim_core (static library)
  passenger_generator -> simulator -> metrics
  optimizer (sequential or bounded std::jthread pool)
```

The C++ engine is the source of truth. Python never simulates, computes metrics or picks a recommendation; it only
prepares input, launches the CLI and displays the returned JSON.

## C++ modules

| File | Responsibility |
| --- | --- |
| `include/flowsim/types.hpp` | Value types: `Passenger`, `Scenario`, `CostParams`, `SimulationResult`, `CandidateSummary`, `OptimizationResult` |
| `validation.*` | Central limits and `ValidationError` with a field path |
| `passenger_generator.*` | Poisson arrivals and exponential or deterministic service, seeded per replication |
| `csv_loader.*` | Strict CSV parsing with row-level errors |
| `event.hpp` | Event type and deterministic ordering comparator |
| `simulator.*` | Event loop, FIFO queue, idle-counter min-heap, traces, safety limit |
| `metrics.*` | Wait statistics, queue area, utilization, costs |
| `optimizer.*` | Evaluation grid, common random numbers, parallel workers, aggregation, tie breaking, baseline |
| `json_io.*` | Versioned JSON schema (`schema_version: 1`), strict field checking, result serialization |
| `src/main.cpp` | CLI commands and exit codes |
| `bench/benchmark.cpp` | Sequential vs parallel benchmark |

Ownership is by value. The optimizer shares passenger datasets with workers through `const` references that outlive
the `std::jthread` scope, and every task writes only its own pre-allocated slot, so no locks are needed beyond an atomic
task counter. The first worker exception is captured and rethrown after all workers join.

## CLI contract

```bash
flowsim_cli simulate --input scenario.json --output simulation_result.json --counters 4 [--passengers-csv data.csv]
flowsim_cli optimize --input scenario.json --output optimization_result.json [--passengers-csv data.csv]
flowsim_cli version
```

| Exit code | Meaning |
| --- | --- |
| 0 | Success, result JSON written |
| 1 | Internal error |
| 2 | Usage error (unknown command or option, missing argument) |
| 3 | Invalid input (bad JSON, schema, CSV or limits); an error JSON is written when possible |
| 4 | Simulation incomplete (event safety limit reached) |

Diagnostics go to stderr. The output file only contains JSON. Error documents look like:

```json
{"schema_version": 1, "ok": false,
 "error": {"code": "INVALID_INPUT", "message": "...", "field": "scenario.arrival_rate_per_hour"}}
```

Success documents contain `ok: true`, `run_type`, the normalized `scenario`, metrics, and for optimization
`results_by_counter_count`, `best_counter_count`, `baseline`, `recommended_simulation`, `baseline_simulation` and
`run_info` (threads, parallel flag, runtime).

## Python layer

| File | Responsibility |
| --- | --- |
| `ui/flowsim_client.py` | Locates `flowsim_cli` (`FLOWSIM_CLI` env var or `build/flowsim_cli`), runs it with an argument list, maps failures to `FlowSimError(code, message, field)` |
| `ui/scenario.py` | Sample scenario, UI-side form validation, CSV parsing with pandas, scenario JSON construction |
| `ui/charts.py` | Plotly Express figures built from engine output |
| `ui/components.py` | Session state, KPI cards, formatting |
| `ui/app.py` | Pages: Overview, Scenario Builder, Simulation, Optimize Staffing, Experiments & Export |

UI validation exists only to give quick feedback; the engine validates everything again.

## Testing

- `tests/*.cpp`: deterministic unit tests for the simulator, metrics, validation and optimizer (custom minimal test
  framework, no external dependency), registered with CTest along with CLI integration checks.
- `tests/python`: client, CLI contract and headless Streamlit tests (`streamlit.testing.v1.AppTest`) that call the real
  engine.
- Optional sanitizer builds: `-DFLOWSIM_SANITIZE=address,undefined` or `-DFLOWSIM_SANITIZE=thread`.
