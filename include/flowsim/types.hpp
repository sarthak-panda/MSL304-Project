#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flowsim {

// All times are double minutes from scenario start; all costs are generic monetary units.

struct Passenger {
    std::string id;
    double arrival_time_min{};
    double service_time_min{};
};

enum class InputMode { Synthetic, Csv };
enum class ServiceDistribution { Exponential, Deterministic };

struct CostParams {
    double counter_cost_per_hour{};
    double waiting_cost_per_passenger_hour{};
    double sla_wait_minutes{};
    double sla_penalty_per_passenger{};
};

struct Scenario {
    std::string name;
    InputMode input_mode{InputMode::Synthetic};

    // Synthetic mode.
    double arrival_window_minutes{};
    double arrival_rate_per_hour{};
    double mean_service_time_minutes{};
    ServiceDistribution service_distribution{ServiceDistribution::Exponential};

    // CSV mode.
    std::vector<Passenger> passengers;

    std::size_t min_counters{1};
    std::size_t max_counters{1};
    std::optional<std::size_t> current_counters;

    CostParams costs;

    std::size_t replications{1};
    std::uint64_t seed{42};
    bool parallel{false};
    std::size_t max_threads{4};
    bool record_trace{true};
};

struct ServiceRecord {
    std::string passenger_id;
    std::size_t counter_id{};  // 1-based for presentation
    double arrival_time_min{};
    double service_start_min{};
    double service_end_min{};
    double wait_time_min{};
    double service_time_min{};
};

struct TracePoint {
    double time_min{};
    std::size_t queue_length{};
    std::size_t busy_counters{};
};

struct SimulationMetrics {
    std::size_t customers_arrived{};
    std::size_t customers_served{};
    double finish_time_min{};
    double mean_wait_min{};
    double median_wait_min{};
    double p95_wait_min{};
    double max_wait_min{};
    double total_wait_min{};
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

struct SimulationResult {
    bool completed{true};
    std::string incomplete_reason;
    std::size_t counter_count{};
    std::size_t events_processed{};
    SimulationMetrics metrics;
    std::vector<ServiceRecord> service_records;  // filled only when trace is recorded
    std::vector<TracePoint> queue_trace;          // filled only when trace is recorded
};

struct CandidateSummary {
    std::size_t counter_count{};
    std::size_t replications{};
    double mean_objective_cost{};
    double stddev_objective_cost{};
    double mean_staffing_cost{};
    double mean_waiting_cost{};
    double mean_sla_penalty{};
    double mean_wait_min{};
    double mean_p95_wait_min{};
    double mean_max_wait_min{};
    double mean_utilization_percent{};
    double mean_queue_length{};
    double mean_max_queue_length{};
    double mean_service_level_percent{};
    double mean_finish_time_min{};
    double mean_customers_served{};
};

struct OptimizationResult {
    std::string scenario_name;
    std::size_t replications{};
    bool parallel{false};
    std::size_t threads_used{1};
    double runtime_ms{};
    std::vector<CandidateSummary> results_by_counter_count;
    std::size_t best_counter_count{};
    double best_objective_cost{};
    std::optional<CandidateSummary> baseline;
    std::optional<double> objective_savings;
    std::optional<double> objective_savings_percent;
    std::optional<double> wait_change_percent;
    SimulationResult recommended_simulation;
    std::optional<SimulationResult> baseline_simulation;
};

}  // namespace flowsim
