#include "flowsim/validation.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>

namespace flowsim {
namespace {

void require(bool condition, const std::string& field, const std::string& message) {
    if (!condition) throw ValidationError(field, message);
}

void check_cost(double value, const std::string& field) {
    require(std::isfinite(value), field, field + " must be a finite number");
    require(value >= 0.0, field, field + " must be non-negative");
    require(value <= limits::kMaxCostValue, field, field + " exceeds the maximum of 1e9");
}

}  // namespace

void validate_passengers(const std::vector<Passenger>& passengers, const std::string& prefix) {
    require(passengers.size() <= limits::kMaxPassengers, prefix,
            "at most " + std::to_string(limits::kMaxPassengers) + " passengers are supported");
    std::unordered_set<std::string> seen;
    seen.reserve(passengers.size());
    for (std::size_t i = 0; i < passengers.size(); ++i) {
        const Passenger& p = passengers[i];
        const std::string at = prefix + "[" + std::to_string(i) + "]";
        require(!p.id.empty(), at + ".passenger_id", "passenger_id must be non-empty (" + at + ")");
        require(p.id.size() <= limits::kMaxIdLength, at + ".passenger_id",
                "passenger_id is longer than 64 characters (" + at + ")");
        require(seen.insert(p.id).second, at + ".passenger_id", "duplicate passenger_id '" + p.id + "'");
        require(std::isfinite(p.arrival_time_min) && p.arrival_time_min >= 0.0, at + ".arrival_time_min",
                "arrival_time_min must be finite and non-negative for passenger '" + p.id + "'");
        require(p.arrival_time_min <= limits::kMaxArrivalTimeMinutes, at + ".arrival_time_min",
                "arrival_time_min exceeds the maximum of 1e7 minutes for passenger '" + p.id + "'");
        require(std::isfinite(p.service_time_min) && p.service_time_min > 0.0, at + ".service_time_min",
                "service_time_min must be finite and greater than zero for passenger '" + p.id + "'");
        require(p.service_time_min >= limits::kMinServiceTimeMinutes &&
                    p.service_time_min <= limits::kMaxServiceTimeMinutes,
                at + ".service_time_min",
                "service_time_min must be between 1e-6 and 1e5 minutes for passenger '" + p.id + "'");
    }
}

void validate_scenario(const Scenario& s) {
    require(!s.name.empty(), "scenario.name", "scenario name must be non-empty");
    require(s.name.size() <= 200, "scenario.name", "scenario name must be at most 200 characters");

    if (s.input_mode == InputMode::Synthetic) {
        require(std::isfinite(s.arrival_rate_per_hour) && s.arrival_rate_per_hour > 0.0,
                "scenario.arrival_rate_per_hour", "arrival_rate_per_hour must be greater than zero");
        require(s.arrival_rate_per_hour <= limits::kMaxRatePerHour, "scenario.arrival_rate_per_hour",
                "arrival_rate_per_hour exceeds the maximum of 1e6");
        require(std::isfinite(s.arrival_window_minutes) && s.arrival_window_minutes >= 0.0,
                "scenario.arrival_window_minutes", "arrival_window_minutes must be finite and non-negative");
        require(s.arrival_window_minutes <= limits::kMaxArrivalWindowMinutes, "scenario.arrival_window_minutes",
                "arrival_window_minutes must be at most 10080 (one week)");
        require(std::isfinite(s.mean_service_time_minutes) &&
                    s.mean_service_time_minutes >= limits::kMinServiceTimeMinutes,
                "scenario.mean_service_time_minutes", "mean_service_time_minutes must be greater than zero");
        require(s.mean_service_time_minutes <= limits::kMaxMeanServiceMinutes,
                "scenario.mean_service_time_minutes", "mean_service_time_minutes must be at most 1440");
        const double expected = s.arrival_rate_per_hour * s.arrival_window_minutes / 60.0;
        require(expected <= static_cast<double>(limits::kMaxPassengers), "scenario.arrival_rate_per_hour",
                "expected passenger count (rate x window) exceeds the limit of " +
                    std::to_string(limits::kMaxPassengers));
    } else {
        validate_passengers(s.passengers, "scenario.passengers");
    }

    require(s.min_counters >= 1, "scenario.min_counters", "min_counters must be at least 1");
    require(s.min_counters <= s.max_counters, "scenario.max_counters",
            "max_counters must be greater than or equal to min_counters");
    require(s.max_counters <= limits::kMaxCounters, "scenario.max_counters",
            "max_counters must be at most " + std::to_string(limits::kMaxCounters));
    if (s.current_counters) {
        require(*s.current_counters >= 1, "scenario.current_counters", "current_counters must be at least 1");
        require(*s.current_counters <= limits::kMaxCounters, "scenario.current_counters",
                "current_counters must be at most " + std::to_string(limits::kMaxCounters));
    }

    check_cost(s.costs.counter_cost_per_hour, "scenario.counter_cost_per_hour");
    check_cost(s.costs.waiting_cost_per_passenger_hour, "scenario.waiting_cost_per_passenger_hour");
    check_cost(s.costs.sla_wait_minutes, "scenario.sla_wait_minutes");
    check_cost(s.costs.sla_penalty_per_passenger, "scenario.sla_penalty_per_passenger");

    require(s.replications >= 1 && s.replications <= limits::kMaxReplications, "scenario.replications",
            "replications must be between 1 and " + std::to_string(limits::kMaxReplications));
    require(s.seed <= limits::kMaxSeed, "scenario.seed", "seed must be between 0 and 2^53 - 1");
    require(s.max_threads >= 1 && s.max_threads <= limits::kMaxThreads, "scenario.max_threads",
            "max_threads must be between 1 and " + std::to_string(limits::kMaxThreads));

    const std::size_t counters = s.max_counters - s.min_counters + 1;
    const std::size_t runs_per_counter = s.input_mode == InputMode::Csv ? 1 : s.replications;
    require(runs_per_counter * counters <= limits::kMaxTasks, "scenario.replications",
            "replications x counter range exceeds the limit of " + std::to_string(limits::kMaxTasks) + " runs");
}

void sort_passengers(std::vector<Passenger>& passengers) {
    std::stable_sort(passengers.begin(), passengers.end(), [](const Passenger& a, const Passenger& b) {
        if (a.arrival_time_min != b.arrival_time_min) return a.arrival_time_min < b.arrival_time_min;
        return a.id < b.id;
    });
}

}  // namespace flowsim
