#pragma once

#include <cstddef>
#include <vector>

#include "flowsim/types.hpp"

namespace flowsim {

struct EvaluationPlan {
    // datasets[r] is the passenger set for replication r, shared by all counter counts (common random numbers).
    const std::vector<std::vector<Passenger>>* datasets{nullptr};
    std::vector<std::size_t> counter_values;
    CostParams costs;
    bool parallel{false};
    std::size_t max_threads{1};
};

struct EvaluationGrid {
    std::size_t threads_used{1};
    // metrics[r][k] for replication r and counter_values[k].
    std::vector<std::vector<SimulationMetrics>> metrics;
};

// Evaluates every (replication, counter count) pair. Throws std::runtime_error if any run is incomplete.
EvaluationGrid evaluate_grid(const EvaluationPlan& plan);

CandidateSummary summarize_candidate(const EvaluationGrid& grid, std::size_t k, std::size_t counter_count);

// Index of the lowest mean objective; ties within limits::kTieTolerance go to fewer counters.
std::size_t select_best(const std::vector<CandidateSummary>& candidates);

OptimizationResult optimize(const Scenario& scenario);

}  // namespace flowsim
