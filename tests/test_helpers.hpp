#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "flowsim/simulator.hpp"
#include "flowsim/types.hpp"
#include "flowsim/validation.hpp"
#include "test_framework.hpp"

namespace fs_test {

inline flowsim::Passenger P(const std::string& id, double arrival, double service) {
    return flowsim::Passenger{id, arrival, service};
}

inline flowsim::CostParams costs(double counter = 60.0, double waiting = 30.0, double sla = 5.0, double penalty = 10.0) {
    return flowsim::CostParams{counter, waiting, sla, penalty};
}

inline const flowsim::ServiceRecord& record(const flowsim::SimulationResult& r, const std::string& id) {
    for (const auto& rec : r.service_records)
        if (rec.passenger_id == id) return rec;
    throw std::runtime_error("no record for " + id);
}

// Invariants from section 11.4 of the specification.
inline void check_invariants(const std::vector<flowsim::Passenger>& input, const flowsim::SimulationResult& r) {
    CHECK(r.completed);
    const auto& m = r.metrics;
    CHECK_EQ(m.customers_served, input.size());
    CHECK_EQ(m.customers_arrived, input.size());
    CHECK_EQ(r.service_records.size(), input.size());
    std::set<std::string> ids;
    std::map<std::size_t, std::vector<std::pair<double, double>>> by_counter;
    for (const auto& rec : r.service_records) {
        CHECK(ids.insert(rec.passenger_id).second);
        CHECK(rec.wait_time_min >= 0.0);
        CHECK(rec.service_start_min >= rec.arrival_time_min);
        CHECK(rec.service_end_min > rec.service_start_min);
        CHECK(rec.counter_id >= 1 && rec.counter_id <= r.counter_count);
        by_counter[rec.counter_id].push_back({rec.service_start_min, rec.service_end_min});
    }
    for (auto& [counter, intervals] : by_counter) {
        std::sort(intervals.begin(), intervals.end());
        for (std::size_t i = 1; i < intervals.size(); ++i) CHECK(intervals[i].first >= intervals[i - 1].second);
    }
    CHECK(m.utilization_percent >= 0.0 && m.utilization_percent <= 100.0 + 1e-9);
    CHECK(m.mean_wait_min <= m.max_wait_min + 1e-12);
    CHECK(m.p95_wait_min <= m.max_wait_min);
    CHECK_NEAR(m.total_objective_cost, m.staffing_cost + m.waiting_cost + m.sla_penalty, 1e-9);
}

}  // namespace fs_test
