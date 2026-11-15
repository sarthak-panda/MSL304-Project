#include <stdexcept>

#include "flowsim/passenger_generator.hpp"
#include "test_helpers.hpp"

using namespace fs_test;
using flowsim::simulate;

TEST_CASE("simulator: empty input produces zeros and no NaN") {
    const std::vector<flowsim::Passenger> none;
    const auto r = simulate(none, 3, costs(), {true, 0});
    CHECK(r.completed);
    CHECK_EQ(r.metrics.customers_served, 0u);
    CHECK_EQ(r.metrics.finish_time_min, 0.0);
    CHECK_EQ(r.metrics.mean_wait_min, 0.0);
    CHECK_EQ(r.metrics.p95_wait_min, 0.0);
    CHECK_EQ(r.metrics.utilization_percent, 0.0);
    CHECK_EQ(r.metrics.throughput_per_hour, 0.0);
    CHECK_EQ(r.metrics.mean_queue_length, 0.0);
    CHECK_EQ(r.metrics.total_objective_cost, 0.0);
    CHECK_EQ(r.metrics.service_level_percent, 100.0);
}

TEST_CASE("simulator: one customer on one counter has zero wait") {
    const std::vector<flowsim::Passenger> ps{P("A", 0.0, 3.5)};
    const auto r = simulate(ps, 1, costs(), {true, 0});
    check_invariants(ps, r);
    CHECK_EQ(r.metrics.mean_wait_min, 0.0);
    CHECK_EQ(r.metrics.finish_time_min, 3.5);
    CHECK_EQ(record(r, "A").service_end_min, 3.5);
    CHECK_NEAR(r.metrics.utilization_percent, 100.0, 1e-12);
}

TEST_CASE("simulator: one counter sequential arrivals give exact waits and finish times") {
    const std::vector<flowsim::Passenger> ps{P("A", 0, 4), P("B", 1, 3), P("C", 2, 2)};
    const auto r = simulate(ps, 1, costs(), {true, 0});
    check_invariants(ps, r);
    CHECK_EQ(record(r, "A").wait_time_min, 0.0);
    CHECK_EQ(record(r, "B").service_start_min, 4.0);
    CHECK_EQ(record(r, "B").wait_time_min, 3.0);
    CHECK_EQ(record(r, "C").service_start_min, 7.0);
    CHECK_EQ(record(r, "C").service_end_min, 9.0);
    CHECK_EQ(record(r, "C").wait_time_min, 5.0);
    CHECK_EQ(r.metrics.finish_time_min, 9.0);
}

TEST_CASE("simulator: multiple counters start simultaneously when idle") {
    const std::vector<flowsim::Passenger> ps{P("A", 0, 5), P("B", 0, 5), P("C", 0, 5)};
    const auto r = simulate(ps, 2, costs(), {true, 0});
    check_invariants(ps, r);
    CHECK_EQ(record(r, "A").service_start_min, 0.0);
    CHECK_EQ(record(r, "A").counter_id, 1u);
    CHECK_EQ(record(r, "B").service_start_min, 0.0);
    CHECK_EQ(record(r, "B").counter_id, 2u);
    CHECK_EQ(record(r, "C").service_start_min, 5.0);
    CHECK_EQ(record(r, "C").counter_id, 1u);  // lowest-numbered counter freed first (completion sequence)
    CHECK_EQ(r.metrics.finish_time_min, 10.0);
}

TEST_CASE("simulator: queued customers start in FIFO arrival order") {
    const std::vector<flowsim::Passenger> ps{P("A", 0, 10), P("B", 1, 1), P("C", 2, 1), P("D", 3, 1)};
    const auto r = simulate(ps, 1, costs(), {true, 0});
    check_invariants(ps, r);
    CHECK_EQ(record(r, "B").service_start_min, 10.0);
    CHECK_EQ(record(r, "C").service_start_min, 11.0);
    CHECK_EQ(record(r, "D").service_start_min, 12.0);
    CHECK_EQ(r.metrics.max_queue_length, 3u);
}

TEST_CASE("simulator: completion is processed before arrival at the same time") {
    const std::vector<flowsim::Passenger> ps{P("A", 0, 5), P("B", 5, 2)};
    const auto r = simulate(ps, 1, costs(), {true, 0});
    check_invariants(ps, r);
    CHECK_EQ(record(r, "B").wait_time_min, 0.0);
    CHECK_EQ(r.metrics.max_queue_length, 0u);

    // A waiting customer keeps FIFO priority over the simultaneous arrival.
    const std::vector<flowsim::Passenger> ps2{P("A", 0, 5), P("B", 1, 1), P("C", 5, 1)};
    const auto r2 = simulate(ps2, 1, costs(), {true, 0});
    check_invariants(ps2, r2);
    CHECK_EQ(record(r2, "B").service_start_min, 5.0);
    CHECK_EQ(record(r2, "C").service_start_min, 6.0);
}

TEST_CASE("simulator: equal arrival timestamps are processed deterministically by id") {
    std::vector<flowsim::Passenger> ps{P("B", 0, 1), P("A", 0, 1), P("C", 0, 1)};
    flowsim::sort_passengers(ps);
    CHECK_EQ(ps[0].id, std::string("A"));
    CHECK_EQ(ps[1].id, std::string("B"));
    const auto r = simulate(ps, 1, costs(), {true, 0});
    check_invariants(ps, r);
    CHECK_EQ(record(r, "A").service_start_min, 0.0);
    CHECK_EQ(record(r, "B").service_start_min, 1.0);
    CHECK_EQ(record(r, "C").service_start_min, 2.0);
}

TEST_CASE("simulator: invariants hold on a random synthetic dataset for every counter count") {
    const flowsim::ArrivalModel model{60.0, 300.0, 4.0, flowsim::ServiceDistribution::Exponential};
    const auto ps = flowsim::generate_passengers(model, 7, 100000);
    CHECK(ps.size() > 200);
    for (std::size_t c = 1; c <= 8; ++c) check_invariants(ps, simulate(ps, c, costs(), {true, 0}));
}

TEST_CASE("simulator: unsorted input and zero counters are rejected") {
    const std::vector<flowsim::Passenger> ps{P("A", 5, 1), P("B", 1, 1)};
    CHECK_THROWS_AS(simulate(ps, 1, costs()), std::invalid_argument);
    CHECK_THROWS_AS(simulate({}, 0, costs()), std::invalid_argument);
}

TEST_CASE("simulator: event safety limit reports an incomplete run without metrics") {
    const std::vector<flowsim::Passenger> ps{P("A", 0, 4), P("B", 1, 3), P("C", 2, 2)};
    const auto r = simulate(ps, 1, costs(), {true, 3});
    CHECK(!r.completed);
    CHECK(!r.incomplete_reason.empty());
    CHECK(r.service_records.empty());
    CHECK_EQ(r.metrics.customers_served, 0u);
}

TEST_CASE("simulator: queue trace follows events and ends empty") {
    const std::vector<flowsim::Passenger> ps{P("A", 0, 4), P("B", 1, 3), P("C", 2, 2)};
    const auto r = simulate(ps, 1, costs(), {true, 0});
    CHECK(!r.queue_trace.empty());
    CHECK_EQ(r.queue_trace.front().time_min, 0.0);
    CHECK_EQ(r.queue_trace.back().time_min, 9.0);
    CHECK_EQ(r.queue_trace.back().queue_length, 0u);
    CHECK_EQ(r.queue_trace.back().busy_counters, 0u);
    std::size_t max_q = 0;
    for (const auto& t : r.queue_trace) max_q = std::max(max_q, t.queue_length);
    CHECK_EQ(max_q, 2u);
    const auto no_trace = simulate(ps, 1, costs(), {false, 0});
    CHECK(no_trace.queue_trace.empty());
    CHECK(no_trace.service_records.empty());
    CHECK_EQ(no_trace.metrics.total_objective_cost, r.metrics.total_objective_cost);
}

TEST_CASE("generator: same seed reproduces passengers and results; different seed differs") {
    const flowsim::ArrivalModel model{48.0, 240.0, 5.5, flowsim::ServiceDistribution::Exponential};
    const auto a = flowsim::generate_passengers(model, 42, 100000);
    const auto b = flowsim::generate_passengers(model, 42, 100000);
    const auto c = flowsim::generate_passengers(model, 43, 100000);
    CHECK_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK_EQ(a[i].id, b[i].id);
        CHECK_EQ(a[i].arrival_time_min, b[i].arrival_time_min);
        CHECK_EQ(a[i].service_time_min, b[i].service_time_min);
    }
    CHECK(a.size() != c.size() || a[0].arrival_time_min != c[0].arrival_time_min);
    const auto ra = simulate(a, 4, costs(), {true, 0});
    const auto rb = simulate(b, 4, costs(), {true, 0});
    CHECK_EQ(ra.metrics.total_objective_cost, rb.metrics.total_objective_cost);
    CHECK_EQ(ra.metrics.p95_wait_min, rb.metrics.p95_wait_min);
}

TEST_CASE("generator: arrivals are sorted, inside the window, with plausible Poisson count") {
    const flowsim::ArrivalModel model{60.0, 6000.0, 2.0, flowsim::ServiceDistribution::Deterministic};
    const auto ps = flowsim::generate_passengers(model, 1, 100000);
    CHECK(ps.size() > 5700 && ps.size() < 6300);  // mean 6000, sd about 77
    for (std::size_t i = 0; i < ps.size(); ++i) {
        CHECK(ps[i].arrival_time_min >= 0.0 && ps[i].arrival_time_min <= 6000.0);
        CHECK_EQ(ps[i].service_time_min, 2.0);
        if (i > 0) CHECK(ps[i].arrival_time_min >= ps[i - 1].arrival_time_min);
    }
}

TEST_CASE("generator: exponential service mean is close to the requested mean") {
    const flowsim::ArrivalModel model{600.0, 6000.0, 5.0, flowsim::ServiceDistribution::Exponential};
    const auto ps = flowsim::generate_passengers(model, 9, 200000);
    double sum = 0.0;
    for (const auto& p : ps) sum += p.service_time_min;
    CHECK_NEAR(sum / static_cast<double>(ps.size()), 5.0, 0.1);
}

TEST_CASE("generator: zero-length window yields no customers and passenger limit is enforced") {
    const flowsim::ArrivalModel empty{48.0, 0.0, 5.0, flowsim::ServiceDistribution::Exponential};
    CHECK(flowsim::generate_passengers(empty, 1, 100).empty());
    const flowsim::ArrivalModel big{6000.0, 600.0, 5.0, flowsim::ServiceDistribution::Exponential};
    CHECK_THROWS_AS(flowsim::generate_passengers(big, 1, 100), flowsim::ValidationError);
}
