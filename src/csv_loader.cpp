#include "flowsim/csv_loader.hpp"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "flowsim/validation.hpp"

namespace flowsim {
namespace {

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream ss(line);
    while (std::getline(ss, field, ',')) fields.push_back(trim(field));
    if (!line.empty() && line.back() == ',') fields.emplace_back();
    return fields;
}

double parse_number(const std::string& text, const std::string& column, std::size_t line_no) {
    const std::string where = "line " + std::to_string(line_no) + ", column " + column;
    if (text.empty()) throw ValidationError("csv." + column, "missing value at " + where);
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || errno == ERANGE || !std::isfinite(value)) {
        throw ValidationError("csv." + column, "'" + text + "' is not a finite number at " + where);
    }
    return value;
}

}  // namespace

std::vector<Passenger> parse_passengers_csv(std::istream& in) {
    static const std::array<std::string, 3> kColumns{"passenger_id", "arrival_time_min", "service_time_min"};
    std::string line;
    std::size_t line_no = 0;
    std::vector<std::string> header;
    while (std::getline(in, line)) {
        ++line_no;
        if (line_no == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);  // UTF-8 BOM
        if (!trim(line).empty()) {
            header = split(line);
            break;
        }
    }
    if (header.empty()) throw ValidationError("csv", "CSV file is empty; expected a header row");

    std::array<std::size_t, 3> index{};
    for (std::size_t c = 0; c < kColumns.size(); ++c) {
        std::size_t found = header.size();
        for (std::size_t h = 0; h < header.size(); ++h) {
            if (header[h] == kColumns[c]) {
                if (found != header.size()) throw ValidationError("csv", "duplicate column '" + kColumns[c] + "'");
                found = h;
            }
        }
        if (found == header.size()) throw ValidationError("csv", "missing required column '" + kColumns[c] + "'");
        index[c] = found;
    }

    std::vector<Passenger> passengers;
    while (std::getline(in, line)) {
        ++line_no;
        if (trim(line).empty()) continue;
        const auto fields = split(line);
        if (fields.size() != header.size()) {
            throw ValidationError("csv", "line " + std::to_string(line_no) + " has " + std::to_string(fields.size()) +
                                             " fields, expected " + std::to_string(header.size()));
        }
        if (passengers.size() >= limits::kMaxPassengers) {
            throw ValidationError("csv", "CSV has more than " + std::to_string(limits::kMaxPassengers) + " rows");
        }
        Passenger p;
        p.id = fields[index[0]];
        if (p.id.empty()) throw ValidationError("csv.passenger_id", "empty passenger_id at line " + std::to_string(line_no));
        p.arrival_time_min = parse_number(fields[index[1]], kColumns[1], line_no);
        p.service_time_min = parse_number(fields[index[2]], kColumns[2], line_no);
        passengers.push_back(std::move(p));
    }
    validate_passengers(passengers, "csv");
    return passengers;
}

std::vector<Passenger> load_passengers_csv(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ValidationError("csv", "cannot open CSV file");
    return parse_passengers_csv(in);
}

}  // namespace flowsim
