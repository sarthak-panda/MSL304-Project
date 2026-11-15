#pragma once

#include <cstddef>
#include <vector>

#include "flowsim/types.hpp"

namespace flowsim {

// Nearest-rank percentile: sorted element at one-based rank ceil(p * N), clamped to [1, N].
// `sorted` must be ascending. Returns 0 for an empty vector.
double nearest_rank_percentile(const std::vector<double>& sorted, double p);

struct MetricInputs {
    std::size_t counter_count{};
    std::size_t customers_arrived{};
    double finish_time_min{};
    double total_busy_time_min{};
    double queue_area{};  // integral of queue length over [0, finish_time]
    std::size_t max_queue_length{};
    std::vector<double> waits;  // one entry per served customer
};

SimulationMetrics compute_metrics(MetricInputs inputs, const CostParams& costs);

}  // namespace flowsim
