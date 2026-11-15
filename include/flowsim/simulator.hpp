#pragma once

#include <cstddef>
#include <vector>

#include "flowsim/types.hpp"

namespace flowsim {

struct SimulationOptions {
    bool record_trace{false};
    // Safety limit on processed events; 0 means limits::events_limit(passenger count).
    std::size_t max_events{0};
};

// Runs one FIFO multi-server simulation. `passengers` must be sorted by arrival time
// (see sort_passengers). The input is only read, so it can be shared across threads.
SimulationResult simulate(const std::vector<Passenger>& passengers, std::size_t counter_count,
                          const CostParams& costs, const SimulationOptions& options = {});

}  // namespace flowsim
