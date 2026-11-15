#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "flowsim/types.hpp"

namespace flowsim {

// Required header columns (any order): passenger_id,arrival_time_min,service_time_min.
// Simple comma-separated format without quoted fields. Throws ValidationError on malformed data.
std::vector<Passenger> parse_passengers_csv(std::istream& in);
std::vector<Passenger> load_passengers_csv(const std::string& path);

}  // namespace flowsim
