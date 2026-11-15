#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

#include "flowsim/types.hpp"

namespace flowsim {

// Every resource guard used by the engine lives here.
namespace limits {
inline constexpr std::size_t kMaxPassengers = 200'000;
inline constexpr std::size_t kMaxCounters = 200;
inline constexpr std::size_t kMaxReplications = 1'000;
inline constexpr std::size_t kMaxThreads = 64;
inline constexpr std::size_t kMaxTasks = 50'000;  // replications x evaluated counter counts
inline constexpr double kMaxArrivalWindowMinutes = 10'080.0;  // one week
inline constexpr double kMaxMeanServiceMinutes = 1'440.0;
inline constexpr double kMaxArrivalTimeMinutes = 1.0e7;
inline constexpr double kMaxServiceTimeMinutes = 1.0e5;
// Lower bound keeps service_end strictly after service_start for every allowed timestamp.
inline constexpr double kMinServiceTimeMinutes = 1.0e-6;
inline constexpr double kMaxRatePerHour = 1.0e6;
inline constexpr double kMaxCostValue = 1.0e9;
inline constexpr std::uint64_t kMaxSeed = 9'007'199'254'740'991ULL;  // 2^53 - 1, exact in JSON doubles
inline constexpr std::size_t kMaxIdLength = 64;
// Each passenger produces exactly one arrival and one completion event.
inline constexpr std::size_t events_limit(std::size_t passengers) { return 2 * passengers + 16; }
inline constexpr double kTieTolerance = 1e-9;      // relative tolerance for objective ties
inline constexpr double kUtilizationSlack = 1e-6;  // allowed floating overshoot above 100%
}  // namespace limits

class ValidationError : public std::runtime_error {
public:
    ValidationError(std::string field, const std::string& message)
        : std::runtime_error(message), field_(std::move(field)) {}
    const std::string& field() const noexcept { return field_; }

private:
    std::string field_;
};

// Throws ValidationError on the first problem found.
void validate_scenario(const Scenario& scenario);
void validate_passengers(const std::vector<Passenger>& passengers, const std::string& field_prefix);

// Sorts by arrival time, breaking ties by passenger id, then by original order.
void sort_passengers(std::vector<Passenger>& passengers);

}  // namespace flowsim
