# Model and assumptions

FlowSim models a single service area (the demo uses an airport security checkpoint) as a multi-server queue
and evaluates how many identical counters to staff. All times are in minutes.

## Queue model

- One shared, unlimited FIFO queue feeds `c` identical parallel counters (an M/M/c or M/D/c style system for
  synthetic input, or a trace-driven G/G/c system for uploaded data).
- Every customer joins the queue, waits as long as needed and is served exactly once (no balking, reneging,
  priority classes or breaks).
- Service is non-preemptive. A free counter immediately takes the customer at the head of the queue.
- When several counters are idle, the lowest-numbered counter is used.
- Staffing is constant for the whole run: the arrival window plus the drain period after the last arrival.

## Inputs

### Synthetic mode

| Field | Meaning |
| --- | --- |
| `arrival_window_minutes` | Arrivals are generated in `[0, window)` |
| `arrival_rate_per_hour` | Poisson rate lambda; inter-arrival times are exponential with mean `60 / lambda` minutes |
| `mean_service_time_minutes` | Mean service time per customer |
| `service_time_distribution` | `exponential` or `deterministic` |
| `replications` | Number of independent datasets |
| `seed` | Replication `r` uses `seed + r` with `std::mt19937_64` |

Exponential samples use the inverse CDF `-mean * log1p(-u)` with a 53-bit uniform `u`, so the generated
data is identical across compilers and standard library implementations.

### CSV mode

Columns `passenger_id, arrival_time_min, service_time_min` in any order. IDs must be unique and non-empty,
arrival times finite and non-negative, service times finite and positive. Rows are sorted by arrival time and
then by ID. A fixed dataset is deterministic, so the optimizer evaluates it once per staffing level.

## Event logic

The simulator is event driven (no time stepping). A `std::priority_queue` holds two event types:

1. **Service completion**: frees the counter, records the service end, then starts the next queued customer.
2. **Arrival**: the customer starts service on the lowest idle counter or joins the back of the queue.

Ties are broken by (time, type, sequence number), where completions come before arrivals at the same timestamp.
A counter that becomes free at exactly the moment a customer arrives can therefore serve that customer with zero
wait. Customers arriving at the same time keep their sorted order.

A safety limit on processed events (default `2 * customers + 16`) stops runaway runs. If it is hit, the result is
reported as incomplete (`completed: false`) and the optimizer rejects it instead of using partial metrics.

## Metrics

Let `W_i = service_start_i - arrival_i` and `T` be the finish time (last service completion).

| Metric | Definition |
| --- | --- |
| Mean, median, max wait | Over all served customers |
| p95 wait | Nearest-rank percentile: sorted value at rank `ceil(0.95 * n)` |
| Mean queue length | `(1 / T) * integral of Q(t) dt`, customers waiting only (not in service) |
| Max queue length | Largest observed waiting-line length |
| Utilization | `total busy time / (c * T)` |
| Throughput | `served / T * 60` customers per hour |
| Service level | Share of customers with `W_i <= SLA` |
| SLA breaches | Customers with `W_i > SLA` (strictly greater) |

An empty dataset gives zero for every metric and a total cost of zero.

## Cost objective

```
staffing_cost = c * counter_cost_per_hour * T / 60
waiting_cost  = sum(W_i) / 60 * waiting_cost_per_passenger_hour
sla_penalty   = breaches * sla_penalty_per_passenger
objective     = staffing_cost + waiting_cost + sla_penalty
```

Staffing is charged over the full finish time `T` because counters stay open until the queue drains. Costs are in
generic monetary units; the UI currency label is display only.

## Optimization

For every integer `c` in `[min_counters, max_counters]` FlowSim simulates every replication and averages the
objective. The recommendation is the staffing level with the lowest mean objective. Candidates within a relative
tolerance of `1e-9` of the best are treated as ties and the smaller staffing level wins.

For synthetic input each replication's dataset is generated once and reused for every staffing level (common random
numbers). Differences between candidates are therefore caused by staffing, not by different random customers.

Parallel mode spreads `(replication, staffing level)` tasks over a bounded pool of `std::jthread` workers. Each task
reads shared immutable data and writes its own result slot, and the aggregation order is fixed, so sequential and
parallel results are identical.

When `current_counters` is set, the result also reports the baseline metrics, objective savings and wait change. One
representative replication (replication 0, seed `seed`) is re-simulated with traces for the recommended plan and the
baseline so the UI can draw charts. These charts show a single sample, while the comparison table uses means.

## Validation limits

| Limit | Value |
| --- | --- |
| Customers per dataset | 200,000 |
| Counters | 1 to 200 |
| Replications | 1 to 1,000 |
| Threads | 1 to 64 |
| Replications x staffing levels | 50,000 |
| Synthetic arrival window | 10,080 minutes (one week) |

Unknown JSON fields, non-finite numbers, negative costs and invalid ranges are rejected with a field-specific error.

## Limitations

- No time-varying arrival rates, shift schedules or per-time-block staffing.
- No customer classes, priority lanes, abandonment or counter breaks.
- No separate queue per counter or shortest-queue joining.
- Steady statistics are not warm-up adjusted; each run starts empty and drains completely.
- The sample scenario uses illustrative parameters, not measured airport data. Real use needs calibrated arrival and
  service data.
