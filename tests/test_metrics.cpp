#include "flowsim/metrics.hpp"
#include "test_helpers.hpp"

using namespace fs_test;

TEST_CASE("metrics: nearest-rank percentile") {
    std::vector<double> v;
    for (int i = 1; i <= 20; ++i) v.push_back(i);
    CHECK_EQ(flowsim::nearest_rank_percentile(v, 0.95), 19.0);
    CHECK_EQ(flowsim::nearest_rank_percentile(v, 0.50), 10.0);
    CHECK_EQ(flowsim::nearest_rank_percentile(v, 1.0), 20.0);
    CHECK_EQ(flowsim::nearest_rank_percentile(v, 0.0), 1.0);  // clamped to rank 1
    CHECK_EQ(flowsim::nearest_rank_percentile({7.0}, 0.95), 7.0);
    CHECK_EQ(flowsim::nearest_rank_percentile({}, 0.95), 0.0);
}

TEST_CASE("metrics: hand-calculated fixture (waits, queue area, utilization, costs)") {
    // A(0,4) B(1,3) C(2,2) on one counter: waits 0,3,5; finish 9.
    // Queue length: 0 on [0,1), 1 on [1,2), 2 on [2,4), 1 on [4,7), 0 on [7,9] -> area 8.
    const std::vector<flowsim::Passenger> ps{P("A", 0, 4), P("B", 1, 3), P("C", 2, 2)};
    const auto r = flowsim::simulate(ps, 1, costs(60.0, 30.0, 5.0, 10.0), {true, 0});
    const auto& m = r.metrics;
    CHECK_NEAR(m.mean_wait_min, 8.0 / 3.0, 1e-12);
    CHECK_EQ(m.median_wait_min, 3.0);
    CHECK_EQ(m.p95_wait_min, 5.0);
    CHECK_EQ(m.max_wait_min, 5.0);
    CHECK_NEAR(m.mean_queue_length, 8.0 / 9.0, 1e-12);
    CHECK_EQ(m.max_queue_length, 2u);
    CHECK_NEAR(m.utilization_percent, 100.0, 1e-12);
    CHECK_NEAR(m.throughput_per_hour, 20.0, 1e-12);
    CHECK_EQ(m.sla_breach_count, 0u);  // C waited exactly 5 = threshold: not a breach
    CHECK_EQ(m.service_level_percent, 100.0);
    CHECK_NEAR(m.staffing_cost, 9.0, 1e-12);   // 1 counter x 0.15 h x 60
    CHECK_NEAR(m.waiting_cost, 4.0, 1e-12);    // 8 min / 60 x 30
    CHECK_NEAR(m.sla_penalty, 0.0, 1e-12);
    CHECK_NEAR(m.total_objective_cost, 13.0, 1e-12);
}

TEST_CASE("metrics: strict SLA breach and two-counter utilization") {
    // Same customers on two counters: A 0-4 (c1), B 1-4 (c2), C 4-6 (c1, wait 2). Busy 9 of 2x6 = 75%.
    const std::vector<flowsim::Passenger> ps{P("A", 0, 4), P("B", 1, 3), P("C", 2, 2)};
    const auto r = flowsim::simulate(ps, 2, costs(60.0, 30.0, 1.999, 10.0), {true, 0});
    const auto& m = r.metrics;
    CHECK_EQ(m.finish_time_min, 6.0);
    CHECK_NEAR(m.utilization_percent, 75.0, 1e-12);
    CHECK_EQ(m.sla_breach_count, 1u);
    CHECK_NEAR(m.service_level_percent, 200.0 / 3.0, 1e-12);
    CHECK_NEAR(m.sla_penalty, 10.0, 1e-12);
    CHECK_NEAR(m.staffing_cost, 2.0 * 0.1 * 60.0, 1e-12);
    CHECK_NEAR(m.mean_queue_length, 2.0 / 6.0, 1e-12);  // C waits on [2,4)
}
