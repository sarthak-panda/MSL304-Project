#include "flowsim/passenger_generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

#include "flowsim/validation.hpp"

namespace flowsim {
namespace {

// Inverse-CDF exponential sample. Implemented by hand (instead of std::exponential_distribution)
// so a given seed produces the same values with every standard library.
double exponential_sample(std::mt19937_64& rng, double mean) {
    const double u = static_cast<double>(rng() >> 11) * 0x1.0p-53;  // uniform in [0, 1)
    return -mean * std::log1p(-u);
}

std::string make_id(std::size_t n) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "P%06zu", n);
    return buf;
}

}  // namespace

std::vector<Passenger> generate_passengers(const ArrivalModel& model, std::uint64_t seed,
                                           std::size_t max_passengers) {
    std::mt19937_64 rng(seed);
    const double mean_interarrival = 60.0 / model.arrival_rate_per_hour;
    std::vector<Passenger> out;
    out.reserve(static_cast<std::size_t>(
        std::min(model.arrival_rate_per_hour * model.arrival_window_minutes / 60.0 * 1.1 + 16.0,
                 static_cast<double>(max_passengers))));

    double t = 0.0;
    while (true) {
        t += exponential_sample(rng, mean_interarrival);
        if (!(t <= model.arrival_window_minutes)) break;
        if (out.size() >= max_passengers) {
            throw ValidationError("scenario.arrival_rate_per_hour",
                                  "generated passengers exceed the limit of " + std::to_string(max_passengers));
        }
        double service = model.mean_service_time_minutes;
        if (model.service_distribution == ServiceDistribution::Exponential) {
            service = std::max(exponential_sample(rng, model.mean_service_time_minutes),
                               limits::kMinServiceTimeMinutes);
        }
        out.push_back(Passenger{make_id(out.size() + 1), t, service});
    }
    return out;  // already sorted: arrival times are strictly increasing sums of positive gaps
}

std::vector<Passenger> scenario_dataset(const Scenario& scenario, std::size_t replication) {
    if (scenario.input_mode == InputMode::Csv) {
        std::vector<Passenger> data = scenario.passengers;
        sort_passengers(data);
        return data;
    }
    const ArrivalModel model{scenario.arrival_rate_per_hour, scenario.arrival_window_minutes,
                             scenario.mean_service_time_minutes, scenario.service_distribution};
    return generate_passengers(model, scenario.seed + replication, limits::kMaxPassengers);
}

}  // namespace flowsim
