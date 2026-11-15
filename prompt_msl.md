# FlowSim — End-to-End Engineering Prompt

> **Purpose:** Give this document to a high-capability coding model or coding agent as the implementation specification for the MSL304 project. The goal is a complete, runnable, well-tested academic software product—not just a design, scaffold, or collection of disconnected code snippets.

---

## 0. Instructions to the coding agent

Act as a senior C++ systems engineer, simulation/optimization engineer, and product-minded full-stack developer. Build **FlowSim**, an end-to-end discrete-event simulation and staffing-optimization application.

### How to work

1. **Inspect the existing repository first.** Preserve useful existing work. Do not overwrite files blindly. If the repository is empty, create the structure described below.
2. Before coding, write a concise implementation plan and identify assumptions or genuine blockers. Then proceed with implementation; do not stop after the plan.
3. Implement in small, coherent phases. After each phase, compile or run the relevant tests and fix failures before moving on.
4. Prefer a correct, understandable, maintainable implementation over clever or unnecessarily abstract code.
5. Do not put the simulation or optimization logic in Python. The **C++ engine is the source of truth**. Python is only the application/UI layer, input preparation, process orchestration, and visualization.
6. Do not use `std::system`, `shell=True`, or shell-interpolated user input to launch the C++ program. Invoke it with a safe subprocess argument list.
7. Do not invent successful benchmark results. Run benchmarks and report measured results, including the machine/environment and dataset size.
8. Do not silently ignore invalid input. Validate it and return a useful error message.
9. Do not leave placeholder functions, fake optimization results, nonfunctional buttons, or TODOs in core functionality.
10. If a dependency or environment limitation prevents a feature, explain it clearly and provide a working fallback. Do not claim that something was tested unless it was actually run.

At completion, provide:
- a concise summary of the implemented features;
- the final repository/file tree;
- exact build, test, and run commands;
- test results and any known limitations;
- a short suggested demonstration flow for a 5-minute course presentation.

---

# 1. Project identity and purpose

## Name

**FlowSim — Discrete-Event Simulation and Queue Optimization for Service Operations**

## Course context

MSL304 — Managing Operations, IIT Delhi.

## Business problem

A service facility such as an airport security checkpoint, hospital registration desk, bank, or customer-service centre must decide how many service counters to staff. Too few counters create long queues, poor service levels, and customer dissatisfaction. Too many counters increase staffing costs and leave expensive resources underutilized.

Managers need a way to test staffing decisions before implementing them. Real operations are stochastic: customer arrivals and service durations vary, and averages alone can hide peak-period congestion.

## Proposed solution

Build a configurable tool that:

1. accepts a customer-arrival scenario and service-counter assumptions;
2. simulates the operation using a **discrete-event simulation (DES)** engine written in modern C++;
3. measures waiting time, queue length, utilization, throughput, staffing cost, and service-level performance;
4. evaluates alternative staffing levels using the **same generated customer samples** for fair comparisons;
5. recommends a counter count that minimizes a configurable total operating-cost objective; and
6. displays the operation and the optimization results in an interactive dashboard.

The first demonstration scenario should be an airport security checkpoint. The software should be designed so that the same engine can also represent other single-stage service systems by changing the labels and input data.

## Academic/product positioning

This is an educational decision-support prototype, not a certified airport operations system. It uses explicit assumptions and illustrative/default parameters. The UI must label generated or example data as illustrative. Do not claim that a simulated recommendation is validated for a real airport without real operational data and calibration.

---

# 2. Scope and modelling assumptions

Keep the first version focused enough to finish well. Correctness and a polished, coherent demonstration matter more than adding many loosely implemented features.

## 2.1 Core model assumptions (MVP)

- There is one service facility with a configurable number of **identical, parallel service counters**.
- Customers join one common, unlimited-capacity **FIFO queue** when all counters are busy.
- If a counter is idle when a customer arrives, service begins immediately.
- A customer is served by one counter at a time. Service is non-preemptive: once service starts, it continues until completion.
- A counter serves at most one customer at a time.
- All staffed counters are available from time zero until the final customer is served. New arrivals stop at the configured end of the arrival window; the system is allowed to drain the remaining queue afterwards.
- There is no customer abandonment, balking, priority queue, counter breakdown, or staff break in the MVP.
- No customer is lost. Every valid customer must eventually be served unless a configured safety limit is reached, in which case the run must be reported as incomplete rather than silently treated as successful.
- Time is represented internally as `double` minutes from the start of the scenario. Validate finite values and avoid integer-time assumptions.

## 2.2 Arrival and service input modes

Support two input modes:

### A. Synthetic arrivals

- Generate arrivals according to a homogeneous Poisson process. Inter-arrival times therefore follow an exponential distribution.
- User specifies `arrival_rate_per_hour` and `arrival_window_minutes`.
- Generate service durations using either:
  - exponential distribution with user-specified mean; or
  - deterministic duration equal to the specified mean.
- Use `std::mt19937_64` and a user-specified seed.
- Sort generated passengers by arrival time; use stable passenger IDs to resolve ties.
- For a simulation replication, generate the passenger dataset **once**, then reuse the exact same arrivals and service requirements when evaluating every candidate staffing level. This common-random-numbers approach makes staffing comparisons less noisy and fairer.

### B. Uploaded customer data

Accept a CSV containing one row per customer with these exact required columns:

```csv
passenger_id,arrival_time_min,service_time_min
P001,0.0,4.2
P002,1.7,5.1
P003,2.1,3.8
```

- `passenger_id` must be non-empty and unique.
- `arrival_time_min` must be finite and non-negative.
- `service_time_min` must be finite and strictly positive.
- Rows may be supplied out of order; sort by arrival time, preserving a deterministic tie-break.
- For CSV mode, one deterministic dataset is used. Do not misleadingly run repeated stochastic replications of an unchanged fixed dataset. The UI should explain that replication controls apply to synthetic mode.

The Python UI may parse the uploaded CSV and convert it to the C++ JSON input format. The C++ layer must still validate the resulting data.

## 2.3 Staffing decision

The primary decision variable is the integer number of active counters, `c`:

```text
min_counters <= c <= max_counters
```

The optimizer evaluates every integer staffing level in this range. The user may also enter `current_counters` to establish a baseline. The first release optimizes a **constant staffing level over the arrival window and drain period**.

Do not implement time-varying shifts, breaks, heterogeneous counter speeds, multiple customer classes, or dynamic queue disciplines in the MVP. Keep these as future extensions unless the core is complete and tested.

---

# 3. Optimization model

The tool must make a real staffing recommendation rather than merely display a simulation.

For a candidate counter count `c`, calculate the following monetary components:

### 3.1 Staffing cost

```text
staffing_cost = c * system_finish_time_hours * counter_cost_per_hour
```

`system_finish_time_hours` is the elapsed time from scenario start until the last customer completes service. All `c` counters are considered staffed for this elapsed period.

### 3.2 Waiting cost

```text
waiting_cost = sum(customer_wait_minutes) / 60
                * waiting_cost_per_passenger_hour
```

Waiting cost is the total customer waiting time converted to customer-hours and multiplied by the configured cost per customer-hour.

### 3.3 Service-level penalty

```text
sla_breach_count = number of customers whose wait_minutes > sla_wait_minutes
sla_penalty = sla_breach_count * sla_penalty_per_passenger
```

Use a strict `>` comparison for an SLA breach. A customer waiting exactly the threshold has met the SLA.

### 3.4 Total objective

```text
total_objective_cost = staffing_cost + waiting_cost + sla_penalty
```

The optimizer chooses the staffing level with the lowest **mean total objective cost** across replications. In case of a numerical tie within a documented tolerance, prefer fewer counters.

All cost inputs should be non-negative. The UI may label the values in INR (₹), but the engine should treat them as generic monetary units and preserve the currency label in application configuration/documentation.

### 3.5 Optimization output

For every evaluated staffing level, show:
- mean total objective cost;
- mean staffing cost;
- mean waiting cost;
- mean SLA penalty;
- mean average waiting time;
- mean 95th-percentile waiting time;
- mean utilization;
- mean maximum queue length;
- standard deviation of objective cost when there are multiple synthetic replications.

Also return:
- best counter count;
- objective-cost savings versus `current_counters`, if supplied;
- percentage change in average waiting time versus baseline;
- a cost-versus-staffing-level curve suitable for visualization.

Do not optimize average waiting time alone: that would ignore the cost of staffing. The configurable total-cost objective is central to the business problem.

---

# 4. Discrete-event simulation engine (C++)

This is the most important technical component. Implement it cleanly and test it independently of the UI.

## 4.1 Event model

Use an event-driven simulator, not a minute-by-minute time-step loop.

Required event types:
- `Arrival`
- `ServiceCompletion`

Use a `std::priority_queue` with a custom comparator to process the next event in chronological order. Define deterministic tie-breaking:

1. earlier event time first;
2. at the same time, process `ServiceCompletion` before `Arrival`;
3. if both time and type are equal, use a monotonically increasing event sequence number.

This ordering makes a counter completing service at time `t` available to an arrival at the same time `t`, subject to any customer already waiting in the FIFO queue.

## 4.2 Suggested domain types

Use clear types in headers, with implementation in `.cpp` files where appropriate. Adapt names if a better design is justified.

```cpp
struct Passenger {
    std::string id;
    double arrival_time_min{};
    double service_time_min{};
};

enum class EventType { ServiceCompletion, Arrival };

struct Event {
    double time_min{};
    EventType type{};
    std::uint64_t sequence{};
    std::size_t passenger_index{};
    std::size_t counter_id{}; // meaningful for completion events
};

struct ServiceRecord {
    std::string passenger_id;
    std::size_t counter_id{};
    double arrival_time_min{};
    double service_start_min{};
    double service_end_min{};
    double wait_time_min{};
    double service_time_min{};
};

struct SimulationMetrics {
    std::size_t customers_arrived{};
    std::size_t customers_served{};
    double finish_time_min{};
    double mean_wait_min{};
    double median_wait_min{};
    double p95_wait_min{};
    double max_wait_min{};
    double mean_queue_length{};
    std::size_t max_queue_length{};
    double utilization_percent{};
    double throughput_per_hour{};
    double service_level_percent{};
    std::size_t sla_breach_count{};
    double staffing_cost{};
    double waiting_cost{};
    double sla_penalty{};
    double total_objective_cost{};
};
```

These are suggested interfaces, not a mandate to use exactly these declarations. Avoid unnecessary raw owning pointers. Use value semantics, standard containers, RAII, and clear ownership.

## 4.3 Simulation state

A simulation run should maintain:
- the event priority queue;
- a FIFO waiting queue (`std::deque` or equivalent);
- the state of each counter (`Idle` or `Busy`, plus active passenger and next completion time if useful);
- service records;
- queue-length statistics;
- optional queue/event trace data for visualization;
- deterministic event sequence state.

Use indices or stable IDs rather than pointers into containers that may reallocate.

## 4.4 Event processing behaviour

### Arrival event

1. Increment the arrived-customer count.
2. If one or more counters are idle, assign the customer to the lowest-numbered idle counter and start service immediately.
3. Otherwise append the customer to the common FIFO queue.

### Service completion event

1. Record the completed service interval and customer's waiting time.
2. Mark the counter idle.
3. If the FIFO queue is non-empty, remove its front customer and immediately start that customer's service on the same counter at the completion timestamp.
4. Otherwise leave the counter idle.

Starting service must create a completion event at:

```text
service_start_time + passenger.service_time_min
```

### No-customer case

The simulator must handle an empty input dataset gracefully: zero customers, zero wait, zero cost, zero utilization, and no NaN/Infinity values. Define the finish time and throughput as zero for this case.

## 4.5 Required metrics and definitions

Document exact definitions and use them consistently in output and UI.

- **Mean waiting time:** arithmetic mean of all customer wait times (`service_start - arrival`).
- **Median / p95 waiting time:** use a documented nearest-rank percentile method; p95 is the sorted element at rank `ceil(0.95 * N)` using one-based ranks, clamped to `[1, N]`.
- **Maximum waiting time:** maximum customer wait.
- **Mean queue length:** time-weighted mean of the number of customers waiting (not including customers in service), over `[0, finish_time]`. Maintain the area under the queue-length curve between event times. Do not use the simple average of event snapshots.
- **Maximum queue length:** maximum number waiting at any event state.
- **Utilization:** total counter busy time divided by `counter_count * finish_time`, multiplied by 100. Clamp only tiny floating-point overshoots caused by rounding; do not hide material errors.
- **Throughput:** customers served divided by finish time in hours. Zero when finish time is zero.
- **Service level:** percentage of served customers whose wait is less than or equal to `sla_wait_minutes`.
- **SLA breaches:** count with `wait > sla_wait_minutes`.
- **Finish time:** completion time of the final customer, measured from scenario start.

Use double precision for time and cost calculations. Guard against non-finite values and division by zero.

## 4.6 Trace for visualization

When `record_trace` is enabled, output enough information for the UI to visualize the run:
- service intervals per passenger/counter (`ServiceRecord` list);
- queue-length changes at event times, with time and queue length;
- optionally busy-counter count at each trace point.

Do not store a full copy of the entire simulation state at every event. Keep trace output bounded and efficient. Allow trace recording to be disabled for optimization batches to reduce memory and runtime; record a full trace for the selected/baseline demonstration run.

---

# 5. Optimizer and experimental design

## 5.1 Sequential optimizer first

Implement the correct sequential optimizer before parallelizing it.

For each replication:
1. Generate one passenger dataset (or load the fixed CSV dataset).
2. Reuse that exact dataset for each candidate counter count.
3. Run the simulator for every integer `c` from `min_counters` through `max_counters`.
4. Store metrics for each candidate.
5. Aggregate metrics across replications.

For synthetic mode, replication `r` should use a deterministic derived seed such as `base_seed + r`. Candidate counter counts within the same replication must use the same passenger dataset. For CSV mode, use a single replication.

## 5.2 Parallel evaluation (after sequential correctness)

Add an optional parallel mode to evaluate independent `(replication, counter_count)` tasks concurrently.

Requirements:
- use a bounded number of worker threads; never spawn an unbounded thread per candidate;
- each task owns its simulator state and reads immutable passenger input;
- do not share mutable RNG objects between workers;
- use unique result slots or another race-free aggregation strategy;
- preserve deterministic output ordering independent of task completion order;
- propagate worker exceptions to the caller and return a useful error;
- verify that sequential and parallel modes produce equivalent results within a documented floating-point tolerance.

Use `std::jthread`/`std::thread` with an atomic work index, or a clearly justified bounded task executor. Do not introduce a complicated generic thread-pool framework unless needed.

## 5.3 Experimental comparisons

The application must compare:
- the user's current staffing level (if supplied);
- the optimizer's recommended staffing level;
- all evaluated staffing levels in the objective-cost curve.

A good demonstration should reveal the trade-off: fewer counters can reduce staffing costs but increase queues and SLA breaches; more counters can improve service but may be unnecessarily expensive.

---

# 6. Application architecture and technology choices

Use a clear two-part architecture.

## 6.1 C++ backend — authoritative engine

- Language: **C++20**.
- Build: **CMake**.
- JSON: `nlohmann/json` (find the installed package through CMake; document installation for Ubuntu and other supported environments).
- Standard library: STL, `<queue>`, `<deque>`, `<random>`, `<chrono>`, `<thread>`, `<atomic>`, `<filesystem>`, etc. as appropriate.
- Testing: CTest plus a lightweight test executable or GoogleTest if already available. Do not make tests dependent on a network download without documenting it.

Responsibilities:
- input validation;
- passenger generation/loading;
- discrete-event simulation;
- metrics calculation;
- staffing optimization;
- optional parallel evaluation;
- JSON result generation.

## 6.2 Python UI — presentation and orchestration only

- Python 3.10+ (or another clearly documented version available in the environment).
- Streamlit for the interface.
- Pandas for tabular display/data preparation.
- Plotly Express and/or Altair for charts. **Do not use `plotly.graph_objects`**; use Plotly Express, Altair, or Streamlit-native charts.
- Invoke the compiled C++ executable using `subprocess.run([...], check=False, capture_output=True, text=True, timeout=...)` or an equally safe approach. Never use `shell=True`.
- The UI must not reimplement simulation, queueing, objective calculations, or optimization in Python.

The communication contract should be JSON files or standard input/output. Start with JSON input/output files because they are straightforward to inspect and debug.

## 6.3 Suggested repository layout

Adapt if the repository already has a sound structure, but preserve this separation.

```text
FlowSim/
├── CMakeLists.txt
├── README.md
├── LICENSE                 # optional; use an appropriate project licence if desired
├── .gitignore
├── config/
│   └── example_scenario.json
├── data/
│   └── sample_passengers.csv
├── include/flowsim/
│   ├── types.hpp
│   ├── validation.hpp
│   ├── passenger_generator.hpp
│   ├── event.hpp
│   ├── simulator.hpp
│   ├── metrics.hpp
│   ├── optimizer.hpp
│   └── json_io.hpp
├── src/
│   ├── main.cpp
│   ├── validation.cpp
│   ├── passenger_generator.cpp
│   ├── simulator.cpp
│   ├── metrics.cpp
│   ├── optimizer.cpp
│   └── json_io.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── test_main.cpp
│   ├── test_simulator.cpp
│   ├── test_metrics.cpp
│   ├── test_optimizer.cpp
│   └── test_validation.cpp
├── ui/
│   ├── app.py
│   ├── flowsim_client.py
│   ├── requirements.txt
│   └── .streamlit/
│       └── config.toml
├── scripts/
│   ├── build.sh
│   └── run_demo.sh
└── docs/
    ├── model_and_assumptions.md
    ├── architecture.md
    └── demo_script.md
```

Avoid creating one enormous `main.cpp` or one enormous Streamlit file. Keep modules cohesive, but do not split every trivial function into its own file.

---

# 7. C++ command-line contract

Create a CLI executable named `flowsim_cli` (platform-appropriate executable suffix is fine).

Required commands:

```bash
# Run one simulation for a specified counter count.
flowsim_cli simulate --input scenario.json --output simulation_result.json --counters 4

# Optimize staffing across the configured range.
flowsim_cli optimize --input scenario.json --output optimization_result.json

# Print version/build information.
flowsim_cli version
```

The exact CLI parser may be simple, but it must validate missing/unknown arguments and return meaningful process exit codes.

- Exit code `0`: success.
- Non-zero: invalid input, runtime failure, or internal error.
- Send human-readable diagnostics to stderr.
- Write machine-readable result JSON to the requested output path.
- Do not emit debug logs into JSON output.

---

# 8. JSON input and output contract

Use a versioned, documented JSON format. Validate all fields, reject unknown enum values, and provide defaults only where documented.

## 8.1 Example synthetic input

```json
{
  "schema_version": 1,
  "scenario": {
    "name": "Airport morning peak",
    "input_mode": "synthetic",
    "arrival_window_minutes": 240,
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
    "parallel": true,
    "max_threads": 4,
    "record_trace": true
  }
}
```

For CSV mode, use `input_mode: "csv"` and include a `passengers` array in JSON. Each element has `passenger_id`, `arrival_time_min`, and `service_time_min`. The Python adapter may construct this array from the uploaded CSV.

## 8.2 Simulation result (illustrative structure)

```json
{
  "schema_version": 1,
  "ok": true,
  "run_type": "simulation",
  "scenario_name": "Airport morning peak",
  "counter_count": 4,
  "metrics": {
    "customers_arrived": 190,
    "customers_served": 190,
    "finish_time_min": 278.4,
    "mean_wait_min": 3.1,
    "median_wait_min": 1.8,
    "p95_wait_min": 12.7,
    "max_wait_min": 21.4,
    "mean_queue_length": 2.2,
    "max_queue_length": 15,
    "utilization_percent": 83.0,
    "throughput_per_hour": 41.0,
    "service_level_percent": 91.0,
    "sla_breach_count": 17,
    "staffing_cost": 4640.0,
    "waiting_cost": 344.0,
    "sla_penalty": 340.0,
    "total_objective_cost": 5324.0
  },
  "service_records": [],
  "queue_trace": []
}
```

The numbers above are examples of field shape only. Do not hardcode them or claim they are actual program output.

## 8.3 Optimization result (illustrative structure)

```json
{
  "schema_version": 1,
  "ok": true,
  "run_type": "optimization",
  "scenario_name": "Airport morning peak",
  "best_counter_count": 5,
  "baseline_counter_count": 4,
  "baseline_objective_cost": 5600.0,
  "best_objective_cost": 5100.0,
  "objective_savings_percent": 8.9,
  "results_by_counter_count": [
    {
      "counter_count": 1,
      "mean_objective_cost": 12000.0,
      "stddev_objective_cost": 300.0,
      "mean_wait_min": 25.0,
      "mean_p95_wait_min": 70.0,
      "mean_utilization_percent": 99.0,
      "mean_staffing_cost": 1000.0,
      "mean_waiting_cost": 5000.0,
      "mean_sla_penalty": 6000.0
    }
  ],
  "recommended_simulation": {
    "metrics": {},
    "service_records": [],
    "queue_trace": []
  }
}
```

Again, sample numbers are illustrative only. The real output must be calculated.

## 8.4 Error result

Return a consistent structure where practical:

```json
{
  "schema_version": 1,
  "ok": false,
  "error": {
    "code": "INVALID_INPUT",
    "message": "arrival_rate_per_hour must be greater than zero",
    "field": "scenario.arrival_rate_per_hour"
  }
}
```

Do not include stack traces or sensitive local paths in user-facing UI errors. Detailed diagnostics may go to stderr/logs.

---

# 9. User interface and product experience

The final UI should look like a small, polished operations-control product—not a raw debugging page. Use a restrained dashboard design, clear labels, consistent spacing, readable typography, informative empty states, and responsive layout.

The user should be able to understand the result without reading source code.

## 9.1 Main navigation / pages

Use a compact sidebar or top navigation with these sections:

### A. Overview

- Product name and one-sentence purpose.
- Scenario summary.
- KPI cards for customers served, average wait, p95 wait, utilization, and estimated total cost.
- A visible action to create/load a scenario.
- Clearly identify sample/demo data.

### B. Scenario Builder

Provide a form for synthetic input:
- scenario name;
- arrival window (minutes or hours, with unambiguous units);
- arrival rate (customers/hour);
- mean service time (minutes/customer);
- service-time distribution (exponential or deterministic);
- minimum and maximum counters;
- current counter count;
- counter cost per hour;
- waiting cost per passenger-hour;
- SLA wait threshold;
- penalty per SLA breach;
- replication count;
- random seed;
- sequential/parallel evaluation toggle and thread limit where appropriate.

Also support uploading the specified CSV. Preview a small sample of rows and display validation errors before running.

### C. Simulation

Allow the user to select a counter count and run one simulation. Display:
- KPI cards;
- queue length over time;
- service-counter utilization;
- an interactive Gantt/timeline of service intervals by counter;
- a passenger/service-record table;
- a time slider or simple playback control that changes the displayed simulation time and shows the queue size / busy counters at that moment, if practical.

For charts, use Plotly Express, Altair, or Streamlit-native charts. For a Gantt chart, Plotly Express `timeline` is acceptable. Do not use `plotly.graph_objects`.

### D. Optimize Staffing

Provide a prominent **Optimize staffing** action. Display:
- recommended counter count;
- current-vs-recommended cost comparison;
- estimated objective savings;
- objective cost versus number of counters;
- breakdown of staffing cost, waiting cost, and SLA penalty;
- average and p95 waiting time versus staffing level;
- utilization versus staffing level;
- a concise explanation of why the recommendation was selected.

Make clear that the selected counter count is the minimum-cost solution under the configured assumptions and objective weights, not a universal optimum.

### E. Experiments / Export

At minimum:
- allow the user to rerun with a different seed or input scenario;
- export optimization results as CSV;
- download full result JSON;
- optionally export service records as CSV;
- show the assumptions and random seed used for each run.

Do not add a nonfunctional experiment-history database in the MVP. Session-based run history is sufficient.

## 9.2 Suggested visual language

- Professional operations dashboard: light or dark theme is acceptable, but use one consistent design system.
- Use a restrained palette. Distinguish queue, active service, idle counters, and alerts consistently.
- Avoid excessive gradients, glow effects, decorative animations, and tiny text.
- Prefer a few high-value charts over many decorative charts.
- Every KPI must have units and a tooltip/help text when the definition is not obvious.
- Explain p95 as “95% of customers waited no longer than this value.”
- Use clear loading/progress feedback for optimization and a clear error state on failure.
- Do not present illustrative mock numbers as if the backend calculated them.

## 9.3 UI interaction quality

- All visible buttons must work.
- Validate form values before launching the backend.
- Disable or guard actions while a run is executing.
- Show the exact scenario settings and seed used for a result.
- Keep the UI usable at laptop presentation resolution and on narrower screens.
- Include a “Load sample airport scenario” action for a reliable live demo.
- Do not require the evaluator to edit source files to try the application.

---

# 10. Error handling, validation, and safety limits

Validate at both the Python boundary and C++ engine boundary.

Minimum checks:
- all numeric inputs finite;
- arrival rate strictly positive for synthetic mode;
- mean service time strictly positive;
- arrival window non-negative and within a documented upper bound;
- `1 <= min_counters <= max_counters <= documented_max_counters`;
- current counter count positive when supplied;
- non-negative costs and penalty values;
- valid seed and replication count;
- a documented upper bound on customers generated/loaded;
- service duration strictly positive;
- arrival times non-negative;
- unique, non-empty passenger IDs;
- valid service-time distribution;
- no malformed or missing required JSON/CSV columns.

Add sensible resource guards, such as maximum passengers, maximum event count, maximum counters, and a subprocess timeout. Put limits in one documented configuration/validation module instead of scattering magic numbers.

If a simulation reaches a configured safety limit before all customers are served, return an explicit `incomplete`/failure status and explain why. Never report partial metrics as a completed successful simulation.

---

# 11. Testing requirements

The simulator and optimizer need deterministic automated tests. Do not rely only on the UI working visually.

## 11.1 Unit tests for the simulator

At least test:

1. **Empty input:** zero customers; no crash, NaN, or Infinity.
2. **One customer:** one counter; zero wait; finish time equals service duration.
3. **One counter, sequential arrivals:** verify exact waiting and finish times.
4. **Multiple counters:** verify that customers can start simultaneously on idle counters.
5. **FIFO behaviour:** queued customers start in arrival order.
6. **Simultaneous completion and arrival:** verify the documented event tie-break rule.
7. **Equal arrival timestamps:** deterministic processing order.
8. **No counter overlap:** no counter has overlapping service intervals.
9. **Conservation:** for a completed run, arrived customers equal served customers; no customer appears twice.
10. **Metrics:** manually calculable fixture for mean wait, p95 wait, queue area, utilization, and costs.
11. **Reproducibility:** same input and seed produce identical generated customers and simulation results.
12. **Invalid input:** malformed values are rejected cleanly.

## 11.2 Optimizer tests

- every staffing level in the configured range is evaluated;
- the selected staffing level has the lowest objective cost in the returned candidates;
- ties use the documented fewer-counters rule;
- baseline metrics are available when current staffing is supplied;
- candidate staffing levels in one replication use the same passenger dataset;
- sequential and parallel runs agree within tolerance;
- fixed CSV data runs deterministically and does not claim stochastic replication.

## 11.3 Integration tests

- build the C++ executable;
- call the CLI using an example JSON input;
- parse the output JSON and validate its schema/required fields;
- run a small Python client test to verify subprocess invocation, error propagation, and result parsing;
- ensure the Streamlit app starts and has a documented launch command.

## 11.4 Invariants

For every completed simulation:
- all waiting times are non-negative;
- service start is no earlier than arrival;
- service end is strictly later than service start;
- each passenger has exactly one service record;
- service records are valid and each counter is non-overlapping;
- served count equals input passenger count;
- utilization lies in `[0, 100]` up to a small documented numerical tolerance;
- mean wait is no greater than maximum wait;
- p95 wait is no greater than maximum wait;
- objective cost equals the sum of the three cost components within tolerance.

---

# 12. Performance and systems-engineering learning goals

The application is also intended to develop practical modern C++ skills. Use these concepts where they make engineering sense; do not add them artificially.

## Required C++ practices

- C++20 and a clean CMake build.
- RAII and value semantics; avoid manual `new`/`delete` for normal ownership.
- `std::vector`, `std::deque`, `std::priority_queue`, `std::optional`, `std::variant` only where appropriate, and standard algorithms.
- `std::mt19937_64` for reproducible random number generation.
- `std::chrono` for measuring runtime and benchmarking.
- Move semantics where large result/data objects genuinely benefit; do not add pointless `std::move` calls.
- Const-correctness and clear ownership/lifetime rules.
- A custom event comparator with a documented strict weak ordering.
- Bounded concurrency for independent optimization tasks.
- Exception-safe resource handling.
- Compiler warnings enabled (`-Wall -Wextra -Wpedantic` on GCC/Clang; use equivalent options on other compilers where feasible).

## Benchmarking

Provide a benchmark executable or documented benchmark mode for increasing passenger counts, for example 1,000; 10,000; and 100,000 customers, subject to machine capacity.

Measure and report:
- number of customers;
- number of counters evaluated;
- replication count;
- sequential versus parallel mode;
- wall-clock runtime;
- runs or customer-events processed per second, if useful.

Use `std::chrono::steady_clock`. Do not claim that parallel is always faster; report measured results and explain overhead for small workloads. Do not optimize prematurely. First ensure correctness, then profile before changing data structures.

## Optional performance investigation (only after correctness)

- compare sequential and parallel optimizer runtime;
- use Linux `perf` or another profiler if available;
- inspect allocation hot spots;
- discuss cache locality and false sharing conceptually or measure them if there is a real reason;
- use sanitizers in a debug build where supported (`address`, `undefined`, and `thread` sanitizers as separate configurations where practical).

Do not implement lock-free queues or custom allocators merely to claim HFT relevance. The project is a service-operations simulator; focus on correct event processing, data ownership, concurrency, and measured performance.

---

# 13. Documentation and academic deliverables

Create documentation that is useful to both the development team and MSL304 evaluator.

## Required files

### `README.md`
Include:
- problem statement and motivation;
- features and screenshots (add real screenshots only after UI is built);
- prerequisites;
- build instructions for C++;
- Python environment and UI setup;
- sample run commands;
- how to load the sample scenario;
- test commands;
- project structure;
- limitations and future extensions.

### `docs/model_and_assumptions.md`
Explain:
- the queueing-system model;
- arrival/service distributions;
- event ordering and FIFO policy;
- all metrics and formulas;
- objective function and decision variable;
- limitations of the assumptions.

### `docs/architecture.md`
Explain:
- C++/Python separation;
- module responsibilities;
- JSON interface;
- event loop and simulation state;
- optimizer and parallel execution design.

### `docs/demo_script.md`
Create a 5-minute demonstration script with approximate timing:
- introduce the business trade-off;
- load the sample scenario;
- run a baseline simulation;
- show queues/counters/timeline;
- optimize staffing;
- compare baseline and recommendation;
- close with limitations and possible extensions.

### MSL304 report / presentation support
The codebase should make it easy to prepare:
- a detailed project report;
- a presentation explaining the model, assumptions, algorithm, results, and UI;
- a short demonstration video.

Do not generate fake screenshots or fabricated experimental results for these deliverables.

---

# 14. Implementation phases and completion gates

Implement in this order. A later phase must not conceal a broken earlier phase.

## Phase 1 — C++ foundation and deterministic simulator

- create CMake project and domain types;
- implement JSON input validation;
- implement synthetic passenger generation and CSV-to-JSON support;
- implement event queue and FIFO multi-counter simulation;
- implement service records and all core metrics;
- add unit tests and a sample CLI simulation.

**Gate:** C++ builds; deterministic tests pass; a sample simulation produces valid JSON and manually verifiable results.

## Phase 2 — Staffing optimizer

- evaluate a range of counter counts;
- implement multiple synthetic replications with common random numbers;
- calculate cost components and objective;
- provide current-staffing baseline and recommended counter count;
- produce optimization result JSON and tests.

**Gate:** Optimizer's recommendation matches the minimum objective among its evaluated candidates; results are reproducible.

## Phase 3 — Concurrency and performance

- add bounded parallel candidate evaluation;
- verify sequential/parallel equivalence;
- add benchmark mode and runtime reporting;
- run sanitizers or profiling tools when available.

**Gate:** No races or nondeterministic results in the tests; benchmark output reflects actual measurements.

## Phase 4 — Streamlit product UI

- build scenario builder and CSV upload/preview;
- connect UI safely to the C++ executable;
- build simulation dashboard, Gantt chart, queue chart, and KPI cards;
- build optimization comparison and recommendation explanation;
- add export functions and a sample scenario.

**Gate:** A user can complete the main workflow from the UI without editing source files: load/edit scenario → run simulation → optimize staffing → inspect/export results.

## Phase 5 — Polish, documentation, and demo

- improve error/empty/loading states;
- run a full clean build and test suite;
- verify setup instructions on a clean environment if possible;
- capture real screenshots;
- write documentation and five-minute demo script.

**Gate:** Another student or TA can follow the README and run the application from a fresh checkout.

---

# 15. Acceptance criteria (definition of done)

The project is complete only when all of the following are true:

- [ ] C++20 simulation engine compiles through CMake.
- [ ] The event engine is event-driven and uses a priority queue, not fixed time-step polling.
- [ ] Synthetic Poisson arrivals and supported service-time distributions work reproducibly.
- [ ] CSV passenger input works and is validated.
- [ ] Multiple counters and one common FIFO queue behave correctly.
- [ ] All required metrics are calculated with documented definitions.
- [ ] Staffing optimizer evaluates every configured counter count.
- [ ] Objective includes staffing cost, waiting cost, and SLA penalty.
- [ ] Common random numbers are used for fair candidate comparisons in each synthetic replication.
- [ ] Optional parallel evaluation is bounded and agrees with sequential evaluation.
- [ ] CLI simulation and optimization commands work and return valid JSON.
- [ ] Automated unit, optimizer, and integration tests pass.
- [ ] Streamlit UI can create/load a scenario, run the C++ engine, optimize staffing, visualize results, and export outputs.
- [ ] The UI makes no hardcoded/fabricated metric claims.
- [ ] README and model/architecture documentation are sufficient for a new user to build and run the project.
- [ ] A five-minute demo script is included.

---

# 16. Future extensions (not required for MVP)

Only consider these after every MVP acceptance criterion is met:

1. Time-varying arrival rates (peak/off-peak schedule) using piecewise-constant Poisson rates.
2. Time-varying staffing by time slot, with opening/closing and staff-transition constraints.
3. Multiple customer classes (e.g. standard and priority) and explicitly modelled queue disciplines.
4. Heterogeneous counters with different service rates or operating costs.
5. Customer abandonment/balking and finite queue capacity.
6. Counter breakdowns, service interruptions, and maintenance.
7. More advanced search methods such as hill climbing or simulated annealing for time-varying staffing schedules.
8. Multi-scenario experiments and sensitivity analysis over arrival rates, service rates, costs, and SLA thresholds.
9. PDF report generation.

Do not implement these before the core simulator, optimizer, tests, and UI work reliably.

---

# 17. Important engineering decisions and non-goals

- This is **not** a high-frequency trading simulator. HFT relevance comes from transferable C++ engineering skills: event queues, careful ownership, STL, concurrency, reproducible benchmarking, and performance analysis.
- Do not implement a matching engine, network stack, or lock-free data structure as part of FlowSim.
- Do not put the optimization logic in JavaScript or Python.
- Do not make the UI dependent on an internet connection for its core workflow.
- Do not require cloud services, login, database hosting, or external APIs for the MVP.
- Do not use real passenger-identifiable data. Sample inputs must be synthetic or anonymized.
- Do not use an external optimizer to perform the core staffing search. The point is to implement and explain the optimization logic ourselves. Exhaustive enumeration over a modest counter range is an appropriate exact search for the MVP; more advanced heuristics belong to future time-varying staffing extensions.
- Do not overengineer. A small number of well-designed modules with clear tests is preferable to a large framework.

---

# 18. Final instruction

Build FlowSim end to end, following this specification. Start with repository inspection and a brief plan, then implement the working C++ engine, CLI, tests, optimizer, Python UI, documentation, and demo script. Make reasonable decisions where details are unspecified, record those decisions in the documentation, and keep the MVP scope intact.

**The most important quality bar:** a TA should be able to enter or upload a scenario, run the simulation, understand how queues and service counters behaved, optimize the staffing level, see why the recommended level is cheaper under the chosen objective, and reproduce the result from the included seed and input data.
