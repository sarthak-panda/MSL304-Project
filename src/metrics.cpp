#include "flowsim/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

#include "flowsim/validation.hpp"

namespace flowsim {

double nearest_rank_percentile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const double n = static_cast<double>(sorted.size());
    auto rank = static_cast<std::size_t>(std::ceil(p * n));
    rank = std::clamp<std::size_t>(rank, 1, sorted.size());
    return sorted[rank - 1];
}

SimulationMetrics compute_metrics(MetricInputs in, const CostParams& costs) {
    SimulationMetrics m;
    const std::size_t served = in.waits.size();
    m.customers_arrived = in.customers_arrived;
    m.customers_served = served;
    m.finish_time_min = in.finish_time_min;
    m.max_queue_length = in.max_queue_length;

    std::sort(in.waits.begin(), in.waits.end());
    m.total_wait_min = std::accumulate(in.waits.begin(), in.waits.end(), 0.0);
    if (served > 0) {
        m.mean_wait_min = m.total_wait_min / static_cast<double>(served);
        m.median_wait_min = nearest_rank_percentile(in.waits, 0.50);
        m.p95_wait_min = nearest_rank_percentile(in.waits, 0.95);
        m.max_wait_min = in.waits.back();
    }

    for (double w : in.waits) {
        if (w > costs.sla_wait_minutes) ++m.sla_breach_count;
    }
    m.service_level_percent =
        served > 0 ? 100.0 * static_cast<double>(served - m.sla_breach_count) / static_cast<double>(served) : 100.0;

    if (in.finish_time_min > 0.0) {
        const double finish_hours = in.finish_time_min / 60.0;
        m.mean_queue_length = in.queue_area / in.finish_time_min;
        m.throughput_per_hour = static_cast<double>(served) / finish_hours;
        double util = 100.0 * in.total_busy_time_min /
                      (static_cast<double>(in.counter_count) * in.finish_time_min);
        if (util > 100.0 && util <= 100.0 * (1.0 + limits::kUtilizationSlack)) util = 100.0;
        m.utilization_percent = util;
        m.staffing_cost = static_cast<double>(in.counter_count) * finish_hours * costs.counter_cost_per_hour;
    }

    m.waiting_cost = m.total_wait_min / 60.0 * costs.waiting_cost_per_passenger_hour;
    m.sla_penalty = static_cast<double>(m.sla_breach_count) * costs.sla_penalty_per_passenger;
    m.total_objective_cost = m.staffing_cost + m.waiting_cost + m.sla_penalty;

    for (double v : {m.finish_time_min, m.mean_wait_min, m.p95_wait_min, m.mean_queue_length, m.utilization_percent,
                     m.throughput_per_hour, m.total_objective_cost}) {
        if (!std::isfinite(v)) throw std::runtime_error("non-finite metric produced; check input magnitudes");
    }
    return m;
}

}  // namespace flowsim
