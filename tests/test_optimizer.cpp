#include <stdexcept>

#include "flowsim/optimizer.hpp"
#include "flowsim/passenger_generator.hpp"
#include "test_helpers.hpp"

using namespace fs_test;

namespace {

flowsim::Scenario synthetic_scenario() {
    flowsim::Scenario s;
    s.name = "opt test";
    s.input_mode = flowsim::InputMode::Synthetic;
    s.arrival_window_minutes = 180;
    s.arrival_rate_per_hour = 48;
    s.mean_service_time_minutes = 5.5;
    s.min_counters = 1;
    s.max_counters = 8;
    s.current_counters = 4;
    s.costs = flowsim::CostParams{250.0, 35.0, 10.0, 20.0};
    s.replications = 5;
    s.seed = 42;
    s.record_trace = true;
    return s;
}

void check_same(const flowsim::CandidateSummary& a, const flowsim::CandidateSummary& b) {
    CHECK_EQ(a.counter_count, b.counter_count);
    CHECK_NEAR(a.mean_objective_cost, b.mean_objective_cost, 1e-9);
    CHECK_NEAR(a.stddev_objective_cost, b.stddev_objective_cost, 1e-9);
    CHECK_NEAR(a.mean_wait_min, b.mean_wait_min, 1e-9);
    CHECK_NEAR(a.mean_utilization_percent, b.mean_utilization_percent, 1e-9);
}

}  // namespace

TEST_CASE("optimizer: every staffing level in range is evaluated, in order") {
    const auto r = flowsim::optimize(synthetic_scenario());
    CHECK_EQ(r.results_by_counter_count.size(), 8u);
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK_EQ(r.results_by_counter_count[i].counter_count, i + 1);
        CHECK_EQ(r.results_by_counter_count[i].replications, 5u);
    }
}

TEST_CASE("optimizer: recommendation has the lowest mean objective among candidates") {
    const auto r = flowsim::optimize(synthetic_scenario());
    for (const auto& c : r.results_by_counter_count) CHECK(r.best_objective_cost <= c.mean_objective_cost);
    CHECK_EQ(r.recommended_simulation.counter_count, r.best_counter_count);
    CHECK(!r.recommended_simulation.service_records.empty());
    for (const auto& c : r.results_by_counter_count) {
        CHECK_NEAR(c.mean_objective_cost, c.mean_staffing_cost + c.mean_waiting_cost + c.mean_sla_penalty, 1e-6);
    }
    // Under this load one counter is overloaded, so the trade-off has an interior optimum.
    CHECK(r.best_counter_count > 1);
}

TEST_CASE("optimizer: ties go to fewer counters") {
    std::vector<flowsim::CandidateSummary> cands(4);
    for (std::size_t i = 0; i < cands.size(); ++i) cands[i].counter_count = i + 2;
    cands[0].mean_objective_cost = 105.0;
    cands[1].mean_objective_cost = 100.0;
    cands[2].mean_objective_cost = 100.0 + 1e-12;
    cands[3].mean_objective_cost = 100.0;
    CHECK_EQ(flowsim::select_best(cands), 1u);

    auto s = synthetic_scenario();
    s.costs = flowsim::CostParams{0.0, 0.0, 10.0, 0.0};  // every candidate costs zero
    s.min_counters = 3;
    const auto r = flowsim::optimize(s);
    CHECK_EQ(r.best_counter_count, 3u);
}

TEST_CASE("optimizer: baseline metrics and savings are reported for current staffing") {
    const auto r = flowsim::optimize(synthetic_scenario());
    CHECK(r.baseline.has_value());
    CHECK_EQ(r.baseline->counter_count, 4u);
    check_same(*r.baseline, r.results_by_counter_count[3]);
    CHECK(r.objective_savings.has_value());
    CHECK_NEAR(*r.objective_savings, r.baseline->mean_objective_cost - r.best_objective_cost, 1e-9);
    CHECK(*r.objective_savings >= 0.0);
    CHECK(r.baseline_simulation.has_value());

    auto outside = synthetic_scenario();
    outside.current_counters = 12;
    const auto r2 = flowsim::optimize(outside);
    CHECK_EQ(r2.results_by_counter_count.size(), 8u);
    CHECK_EQ(r2.baseline->counter_count, 12u);
    CHECK(r2.best_counter_count <= 8u);

    auto none = synthetic_scenario();
    none.current_counters.reset();
    const auto r3 = flowsim::optimize(none);
    CHECK(!r3.baseline.has_value());
    CHECK(!r3.objective_savings.has_value());
}

TEST_CASE("optimizer: candidates in one replication share the same passenger dataset") {
    const auto s = synthetic_scenario();
    std::vector<std::vector<flowsim::Passenger>> datasets{flowsim::scenario_dataset(s, 0), flowsim::scenario_dataset(s, 1)};
    flowsim::EvaluationPlan plan;
    plan.datasets = &datasets;
    plan.counter_values = {2, 3, 4, 5};
    plan.costs = s.costs;
    const auto grid = flowsim::evaluate_grid(plan);
    for (std::size_t r = 0; r < datasets.size(); ++r) {
        for (std::size_t k = 0; k < plan.counter_values.size(); ++k) {
            CHECK_EQ(grid.metrics[r][k].customers_served, datasets[r].size());
            const auto direct = flowsim::simulate(datasets[r], plan.counter_values[k], s.costs);
            CHECK_EQ(grid.metrics[r][k].total_objective_cost, direct.metrics.total_objective_cost);
        }
    }
    // Replication r uses seed + r, so replications differ but are individually reproducible.
    CHECK(datasets[0].size() != datasets[1].size() ||
          datasets[0][0].arrival_time_min != datasets[1][0].arrival_time_min);
    const auto again = flowsim::scenario_dataset(s, 1);
    CHECK_EQ(again.size(), datasets[1].size());
    CHECK_EQ(again.back().service_time_min, datasets[1].back().service_time_min);
}

TEST_CASE("optimizer: sequential and parallel runs agree") {
    auto seq = synthetic_scenario();
    seq.replications = 12;
    auto par = seq;
    par.parallel = true;
    par.max_threads = 6;
    const auto a = flowsim::optimize(seq);
    const auto b = flowsim::optimize(par);
    CHECK_EQ(a.threads_used, 1u);
    CHECK_EQ(b.threads_used, 6u);
    CHECK_EQ(a.best_counter_count, b.best_counter_count);
    for (std::size_t i = 0; i < a.results_by_counter_count.size(); ++i)
        check_same(a.results_by_counter_count[i], b.results_by_counter_count[i]);
}

TEST_CASE("optimizer: worker exceptions propagate in parallel mode") {
    std::vector<std::vector<flowsim::Passenger>> datasets{{P("A", 5, 1), P("B", 1, 1)}};  // unsorted -> throws
    flowsim::EvaluationPlan plan;
    plan.datasets = &datasets;
    plan.counter_values = {1, 2, 3, 4};
    plan.parallel = true;
    plan.max_threads = 4;
    CHECK_THROWS_AS(flowsim::evaluate_grid(plan), std::invalid_argument);
}

TEST_CASE("optimizer: fixed CSV data runs once and deterministically") {
    flowsim::Scenario s = synthetic_scenario();
    s.input_mode = flowsim::InputMode::Csv;
    s.replications = 1;
    s.passengers = {P("C", 2, 2), P("A", 0, 4), P("B", 1, 3)};
    s.min_counters = 1;
    s.max_counters = 3;
    s.current_counters = 1;
    const auto a = flowsim::optimize(s);
    const auto b = flowsim::optimize(s);
    CHECK_EQ(a.replications, 1u);
    for (std::size_t i = 0; i < a.results_by_counter_count.size(); ++i) {
        CHECK_EQ(a.results_by_counter_count[i].stddev_objective_cost, 0.0);
        CHECK_EQ(a.results_by_counter_count[i].mean_objective_cost, b.results_by_counter_count[i].mean_objective_cost);
    }
    CHECK_EQ(a.best_counter_count, b.best_counter_count);
    CHECK_EQ(a.recommended_simulation.service_records.size(), 3u);
}
