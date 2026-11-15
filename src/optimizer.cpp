#include "flowsim/optimizer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "flowsim/passenger_generator.hpp"
#include "flowsim/simulator.hpp"
#include "flowsim/validation.hpp"

namespace flowsim {

EvaluationGrid evaluate_grid(const EvaluationPlan& plan) {
    if (plan.datasets == nullptr || plan.datasets->empty()) throw std::invalid_argument("no datasets to evaluate");
    if (plan.counter_values.empty()) throw std::invalid_argument("no counter counts to evaluate");

    const auto& datasets = *plan.datasets;
    const std::size_t reps = datasets.size();
    const std::size_t k_count = plan.counter_values.size();
    const std::size_t tasks = reps * k_count;

    EvaluationGrid grid;
    grid.metrics.assign(reps, std::vector<SimulationMetrics>(k_count));

    std::atomic<std::size_t> next_task{0};
    std::atomic<bool> stop{false};
    std::mutex error_mutex;
    std::exception_ptr first_error;

    // Each task reads one immutable dataset and writes only its own result slot.
    auto worker = [&]() {
        try {
            for (std::size_t t = next_task.fetch_add(1); t < tasks && !stop.load(); t = next_task.fetch_add(1)) {
                const std::size_t r = t / k_count;
                const std::size_t k = t % k_count;
                SimulationResult run = simulate(datasets[r], plan.counter_values[k], plan.costs);
                if (!run.completed) {
                    throw std::runtime_error("simulation incomplete for " + std::to_string(plan.counter_values[k]) +
                                             " counters: " + run.incomplete_reason);
                }
                grid.metrics[r][k] = run.metrics;
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(error_mutex);
            if (!first_error) first_error = std::current_exception();
            stop.store(true);
        }
    };

    std::size_t threads = 1;
    if (plan.parallel) threads = std::clamp<std::size_t>(plan.max_threads, 1, std::min(tasks, limits::kMaxThreads));
    grid.threads_used = threads;

    if (threads == 1) {
        worker();
    } else {
        std::vector<std::jthread> pool;
        pool.reserve(threads);
        for (std::size_t i = 0; i < threads; ++i) pool.emplace_back(worker);
    }  // jthreads join here

    if (first_error) std::rethrow_exception(first_error);
    return grid;
}

CandidateSummary summarize_candidate(const EvaluationGrid& grid, std::size_t k, std::size_t counter_count) {
    CandidateSummary s;
    s.counter_count = counter_count;
    s.replications = grid.metrics.size();
    const double n = static_cast<double>(s.replications);
    for (const auto& rep : grid.metrics) {
        const SimulationMetrics& m = rep[k];
        s.mean_objective_cost += m.total_objective_cost;
        s.mean_staffing_cost += m.staffing_cost;
        s.mean_waiting_cost += m.waiting_cost;
        s.mean_sla_penalty += m.sla_penalty;
        s.mean_wait_min += m.mean_wait_min;
        s.mean_p95_wait_min += m.p95_wait_min;
        s.mean_max_wait_min += m.max_wait_min;
        s.mean_utilization_percent += m.utilization_percent;
        s.mean_queue_length += m.mean_queue_length;
        s.mean_max_queue_length += static_cast<double>(m.max_queue_length);
        s.mean_service_level_percent += m.service_level_percent;
        s.mean_finish_time_min += m.finish_time_min;
        s.mean_customers_served += static_cast<double>(m.customers_served);
    }
    for (double* v : {&s.mean_objective_cost, &s.mean_staffing_cost, &s.mean_waiting_cost, &s.mean_sla_penalty,
                      &s.mean_wait_min, &s.mean_p95_wait_min, &s.mean_max_wait_min, &s.mean_utilization_percent,
                      &s.mean_queue_length, &s.mean_max_queue_length, &s.mean_service_level_percent,
                      &s.mean_finish_time_min, &s.mean_customers_served}) {
        *v /= n;
    }
    if (s.replications > 1) {
        double ss = 0.0;
        for (const auto& rep : grid.metrics) {
            const double d = rep[k].total_objective_cost - s.mean_objective_cost;
            ss += d * d;
        }
        s.stddev_objective_cost = std::sqrt(ss / (n - 1.0));  // sample standard deviation
    }
    return s;
}

std::size_t select_best(const std::vector<CandidateSummary>& candidates) {
    if (candidates.empty()) throw std::invalid_argument("no candidates to select from");
    std::size_t best = 0;
    for (std::size_t i = 1; i < candidates.size(); ++i) {
        const double incumbent = candidates[best].mean_objective_cost;
        const double tolerance = limits::kTieTolerance * std::max(1.0, std::abs(incumbent));
        const bool clearly_cheaper = candidates[i].mean_objective_cost < incumbent - tolerance;
        const bool tie_fewer = std::abs(candidates[i].mean_objective_cost - incumbent) <= tolerance &&
                               candidates[i].counter_count < candidates[best].counter_count;
        if (clearly_cheaper || tie_fewer) best = i;
    }
    return best;
}

OptimizationResult optimize(const Scenario& scenario) {
    validate_scenario(scenario);
    const auto started = std::chrono::steady_clock::now();

    const std::size_t reps = scenario.input_mode == InputMode::Csv ? 1 : scenario.replications;
    std::vector<std::vector<Passenger>> datasets;
    datasets.reserve(reps);
    for (std::size_t r = 0; r < reps; ++r) datasets.push_back(scenario_dataset(scenario, r));

    EvaluationPlan plan;
    plan.datasets = &datasets;
    plan.costs = scenario.costs;
    plan.parallel = scenario.parallel;
    plan.max_threads = scenario.max_threads;
    for (std::size_t c = scenario.min_counters; c <= scenario.max_counters; ++c) plan.counter_values.push_back(c);
    const std::size_t range_size = plan.counter_values.size();
    std::optional<std::size_t> baseline_k;
    if (scenario.current_counters) {
        const std::size_t cur = *scenario.current_counters;
        if (cur >= scenario.min_counters && cur <= scenario.max_counters) {
            baseline_k = cur - scenario.min_counters;
        } else {
            baseline_k = plan.counter_values.size();
            plan.counter_values.push_back(cur);  // evaluated for the baseline only, not a candidate
        }
    }

    const EvaluationGrid grid = evaluate_grid(plan);

    OptimizationResult out;
    out.scenario_name = scenario.name;
    out.replications = reps;
    out.parallel = scenario.parallel;
    out.threads_used = grid.threads_used;
    for (std::size_t k = 0; k < range_size; ++k) {
        out.results_by_counter_count.push_back(summarize_candidate(grid, k, plan.counter_values[k]));
    }
    const std::size_t best = select_best(out.results_by_counter_count);
    out.best_counter_count = out.results_by_counter_count[best].counter_count;
    out.best_objective_cost = out.results_by_counter_count[best].mean_objective_cost;

    const SimulationOptions trace_options{scenario.record_trace, 0};
    out.recommended_simulation = simulate(datasets.front(), out.best_counter_count, scenario.costs, trace_options);

    if (baseline_k) {
        out.baseline = summarize_candidate(grid, *baseline_k, plan.counter_values[*baseline_k]);
        const double base_cost = out.baseline->mean_objective_cost;
        const double base_wait = out.baseline->mean_wait_min;
        const double best_wait = out.results_by_counter_count[best].mean_wait_min;
        out.objective_savings = base_cost - out.best_objective_cost;
        if (base_cost > 0.0) out.objective_savings_percent = *out.objective_savings / base_cost * 100.0;
        if (base_wait > 0.0) {
            out.wait_change_percent = (best_wait - base_wait) / base_wait * 100.0;
        } else if (best_wait == 0.0) {
            out.wait_change_percent = 0.0;
        }
        out.baseline_simulation = simulate(datasets.front(), out.baseline->counter_count, scenario.costs, trace_options);
    }

    out.runtime_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return out;
}

}  // namespace flowsim
