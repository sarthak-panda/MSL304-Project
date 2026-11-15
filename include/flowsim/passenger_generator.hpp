#pragma once

#include <cstdint>
#include <vector>

#include "flowsim/types.hpp"

namespace flowsim {

struct ArrivalModel {
    double arrival_rate_per_hour{};
    double arrival_window_minutes{};
    double mean_service_time_minutes{};
    ServiceDistribution service_distribution{ServiceDistribution::Exponential};
};

// Homogeneous Poisson arrivals on [0, window] with the given service distribution.
// Output is sorted by arrival time. Throws ValidationError if more than max_passengers would be created.
std::vector<Passenger> generate_passengers(const ArrivalModel& model, std::uint64_t seed,
                                           std::size_t max_passengers);

// Dataset for replication r of a scenario: synthetic uses seed + r, CSV returns the sorted fixed data.
std::vector<Passenger> scenario_dataset(const Scenario& scenario, std::size_t replication);

}  // namespace flowsim
