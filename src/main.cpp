#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "flowsim/csv_loader.hpp"
#include "flowsim/json_io.hpp"
#include "flowsim/optimizer.hpp"
#include "flowsim/passenger_generator.hpp"
#include "flowsim/simulator.hpp"
#include "flowsim/validation.hpp"

namespace {

using nlohmann::json;

enum ExitCode : int { kOk = 0, kInternal = 1, kUsage = 2, kInvalidInput = 3, kRunFailed = 4 };

constexpr const char* kUsageText =
    "usage:\n"
    "  flowsim_cli simulate --input scenario.json --output result.json --counters N [--passengers-csv file.csv]\n"
    "  flowsim_cli optimize --input scenario.json --output result.json [--passengers-csv file.csv]\n"
    "  flowsim_cli version\n";

struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct RunFailed : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::map<std::string, std::string> parse_options(int argc, char** argv, const std::vector<std::string>& allowed) {
    std::map<std::string, std::string> opts;
    for (int i = 2; i < argc; ++i) {
        const std::string key = argv[i];
        bool known = false;
        for (const auto& a : allowed) known = known || a == key;
        if (!known) throw UsageError("unknown argument '" + key + "'");
        if (i + 1 >= argc) throw UsageError("missing value for " + key);
        if (opts.count(key)) throw UsageError("duplicate argument " + key);
        opts[key] = argv[++i];
    }
    return opts;
}

std::string require_opt(const std::map<std::string, std::string>& opts, const std::string& key) {
    const auto it = opts.find(key);
    if (it == opts.end()) throw UsageError("missing required argument " + key);
    return it->second;
}

json read_input(const std::map<std::string, std::string>& opts) {
    const std::string input_path = require_opt(opts, "--input");
    std::ifstream in(input_path);
    if (!in) throw flowsim::ValidationError("input", "cannot open input file");
    json doc;
    try {
        in >> doc;
    } catch (const json::parse_error& e) {
        throw flowsim::ValidationError("input", std::string("input is not valid JSON: ") + e.what());
    }
    const auto csv = opts.find("--passengers-csv");
    if (csv != opts.end()) {
        const auto passengers = flowsim::load_passengers_csv(csv->second);
        json arr = json::array();
        for (const auto& p : passengers) {
            arr.push_back({{"passenger_id", p.id}, {"arrival_time_min", p.arrival_time_min},
                           {"service_time_min", p.service_time_min}});
        }
        if (doc.is_object() && doc.contains("scenario") && doc["scenario"].is_object()) {
            doc["scenario"]["input_mode"] = "csv";
            doc["scenario"]["passengers"] = std::move(arr);
        }
    }
    return doc;
}

void write_output(const std::string& path, const json& doc) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot open output file for writing");
    out << doc.dump(2) << '\n';
    if (!out) throw std::runtime_error("failed while writing output file");
}

std::size_t parse_counters(const std::string& text) {
    std::size_t pos = 0;
    unsigned long long value = 0;
    try {
        value = std::stoull(text, &pos);
    } catch (const std::exception&) {
        pos = 0;
    }
    if (pos != text.size() || text.empty() || text[0] == '-' || value < 1 || value > flowsim::limits::kMaxCounters) {
        throw flowsim::ValidationError("counters", "--counters must be an integer between 1 and " +
                                                       std::to_string(flowsim::limits::kMaxCounters));
    }
    return static_cast<std::size_t>(value);
}

json run_simulate(const std::map<std::string, std::string>& opts) {
    const std::size_t counters = parse_counters(require_opt(opts, "--counters"));
    const flowsim::Scenario scenario = flowsim::parse_scenario(read_input(opts));
    const auto passengers = flowsim::scenario_dataset(scenario, 0);
    const auto result = flowsim::simulate(passengers, counters, scenario.costs, {scenario.record_trace, 0});
    if (!result.completed) throw RunFailed(result.incomplete_reason);
    return flowsim::simulation_document(scenario, result);
}

json run_optimize(const std::map<std::string, std::string>& opts) {
    const flowsim::Scenario scenario = flowsim::parse_scenario(read_input(opts));
    return flowsim::optimization_document(scenario, flowsim::optimize(scenario));
}

int fail(const std::optional<std::string>& output, int code, const std::string& error_code, const std::string& message,
         const std::string& field = "") {
    std::cerr << "flowsim_cli: " << message << '\n';
    if (output) {
        try {
            write_output(*output, flowsim::error_document(error_code, message, field));
        } catch (const std::exception& e) {
            std::cerr << "flowsim_cli: could not write error JSON: " << e.what() << '\n';
        }
    }
    return code;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << kUsageText;
        return kUsage;
    }
    const std::string command = argv[1];
    if (command == "version" || command == "--version") {
        if (argc != 2) {
            std::cerr << "flowsim_cli: version takes no arguments\n";
            return kUsage;
        }
        std::cout << "flowsim_cli " << FLOWSIM_VERSION << " (C++" << (__cplusplus / 100 % 100) << ", "
#if defined(__clang__)
                  << "clang " << __clang_major__ << '.' << __clang_minor__
#elif defined(__GNUC__)
                  << "gcc " << __GNUC__ << '.' << __GNUC_MINOR__
#elif defined(_MSC_VER)
                  << "msvc " << _MSC_VER
#else
                  << "unknown compiler"
#endif
                  << ", " << FLOWSIM_BUILD_TYPE << ", schema " << flowsim::kSchemaVersion << ")\n";
        return kOk;
    }
    if (command == "help" || command == "--help" || command == "-h") {
        std::cout << kUsageText;
        return kOk;
    }

    std::optional<std::string> output;
    try {
        std::map<std::string, std::string> opts;
        if (command == "simulate") {
            opts = parse_options(argc, argv, {"--input", "--output", "--counters", "--passengers-csv"});
        } else if (command == "optimize") {
            opts = parse_options(argc, argv, {"--input", "--output", "--passengers-csv"});
        } else {
            throw UsageError("unknown command '" + command + "'");
        }
        output = require_opt(opts, "--output");
        const json doc = command == "simulate" ? run_simulate(opts) : run_optimize(opts);
        write_output(*output, doc);
        return kOk;
    } catch (const UsageError& e) {
        std::cerr << kUsageText;
        return fail(output, kUsage, "USAGE_ERROR", e.what());
    } catch (const flowsim::ValidationError& e) {
        return fail(output, kInvalidInput, "INVALID_INPUT", e.what(), e.field());
    } catch (const RunFailed& e) {
        return fail(output, kRunFailed, "SIMULATION_INCOMPLETE", e.what());
    } catch (const std::exception& e) {
        return fail(output, kInternal, "INTERNAL_ERROR", e.what());
    }
}
