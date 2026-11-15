#include "flowsim/json_io.hpp"

#include <cmath>
#include <set>

#include "flowsim/validation.hpp"

namespace flowsim {
namespace {

using nlohmann::json;

const std::set<std::string> kScenarioKeys{
    "name", "input_mode", "arrival_window_minutes", "arrival_rate_per_hour", "mean_service_time_minutes",
    "service_time_distribution", "passengers", "min_counters", "max_counters", "current_counters",
    "counter_cost_per_hour", "waiting_cost_per_passenger_hour", "sla_wait_minutes", "sla_penalty_per_passenger",
    "replications", "seed", "parallel", "max_threads", "record_trace"};

std::string path(const std::string& key) { return "scenario." + key; }

const json* find(const json& obj, const std::string& key, bool required) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        if (required) throw ValidationError(path(key), "missing required field '" + path(key) + "'");
        return nullptr;
    }
    return &*it;
}

std::optional<double> get_number(const json& obj, const std::string& key, bool required) {
    const json* v = find(obj, key, required);
    if (!v) return std::nullopt;
    if (!v->is_number()) throw ValidationError(path(key), path(key) + " must be a number");
    const double d = v->get<double>();
    if (!std::isfinite(d)) throw ValidationError(path(key), path(key) + " must be finite");
    return d;
}

std::optional<std::uint64_t> get_uint(const json& obj, const std::string& key, bool required) {
    const json* v = find(obj, key, required);
    if (!v) return std::nullopt;
    if (!v->is_number_integer()) throw ValidationError(path(key), path(key) + " must be an integer");
    if (v->is_number_unsigned()) return v->get<std::uint64_t>();
    const auto i = v->get<std::int64_t>();
    if (i < 0) throw ValidationError(path(key), path(key) + " must be non-negative");
    return static_cast<std::uint64_t>(i);
}

std::size_t to_size(std::uint64_t value, const std::string& key) {
    if (value > 1'000'000'000ULL) throw ValidationError(path(key), path(key) + " is too large");
    return static_cast<std::size_t>(value);
}

std::optional<bool> get_bool(const json& obj, const std::string& key) {
    const json* v = find(obj, key, false);
    if (!v) return std::nullopt;
    if (!v->is_boolean()) throw ValidationError(path(key), path(key) + " must be true or false");
    return v->get<bool>();
}

std::optional<std::string> get_string(const json& obj, const std::string& key, bool required) {
    const json* v = find(obj, key, required);
    if (!v) return std::nullopt;
    if (!v->is_string()) throw ValidationError(path(key), path(key) + " must be a string");
    return v->get<std::string>();
}

std::vector<Passenger> parse_passengers(const json& arr) {
    if (!arr.is_array()) throw ValidationError("scenario.passengers", "scenario.passengers must be an array");
    if (arr.size() > limits::kMaxPassengers) {
        throw ValidationError("scenario.passengers",
                              "at most " + std::to_string(limits::kMaxPassengers) + " passengers are supported");
    }
    std::vector<Passenger> out;
    out.reserve(arr.size());
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const json& p = arr[i];
        const std::string at = "scenario.passengers[" + std::to_string(i) + "]";
        if (!p.is_object()) throw ValidationError(at, at + " must be an object");
        const auto id = p.find("passenger_id");
        const auto arrival = p.find("arrival_time_min");
        const auto service = p.find("service_time_min");
        if (id == p.end() || !id->is_string()) throw ValidationError(at + ".passenger_id", at + ".passenger_id must be a string");
        if (arrival == p.end() || !arrival->is_number())
            throw ValidationError(at + ".arrival_time_min", at + ".arrival_time_min must be a number");
        if (service == p.end() || !service->is_number())
            throw ValidationError(at + ".service_time_min", at + ".service_time_min must be a number");
        out.push_back(Passenger{id->get<std::string>(), arrival->get<double>(), service->get<double>()});
    }
    return out;
}

json opt_number(const std::optional<double>& v) { return v ? json(*v) : json(nullptr); }

}  // namespace

Scenario parse_scenario(const json& doc) {
    if (!doc.is_object()) throw ValidationError("", "input must be a JSON object");
    const auto version = doc.find("schema_version");
    if (version == doc.end() || !version->is_number_integer() || version->get<int>() != kSchemaVersion) {
        throw ValidationError("schema_version", "schema_version must be 1");
    }
    const auto sc_it = doc.find("scenario");
    if (sc_it == doc.end() || !sc_it->is_object()) throw ValidationError("scenario", "missing 'scenario' object");
    const json& sc = *sc_it;
    for (const auto& item : sc.items()) {
        if (!kScenarioKeys.count(item.key())) {
            throw ValidationError(path(item.key()), "unknown field '" + path(item.key()) + "'");
        }
    }

    Scenario s;
    s.name = *get_string(sc, "name", true);
    const std::string mode = get_string(sc, "input_mode", false).value_or("synthetic");
    if (mode == "synthetic") {
        s.input_mode = InputMode::Synthetic;
    } else if (mode == "csv") {
        s.input_mode = InputMode::Csv;
    } else {
        throw ValidationError(path("input_mode"), "input_mode must be 'synthetic' or 'csv'");
    }
    const bool synthetic = s.input_mode == InputMode::Synthetic;

    s.arrival_window_minutes = get_number(sc, "arrival_window_minutes", synthetic).value_or(0.0);
    s.arrival_rate_per_hour = get_number(sc, "arrival_rate_per_hour", synthetic).value_or(0.0);
    s.mean_service_time_minutes = get_number(sc, "mean_service_time_minutes", synthetic).value_or(0.0);
    const std::string dist = get_string(sc, "service_time_distribution", false).value_or("exponential");
    if (dist == "exponential") {
        s.service_distribution = ServiceDistribution::Exponential;
    } else if (dist == "deterministic") {
        s.service_distribution = ServiceDistribution::Deterministic;
    } else {
        throw ValidationError(path("service_time_distribution"),
                              "service_time_distribution must be 'exponential' or 'deterministic'");
    }

    if (synthetic) {
        if (find(sc, "passengers", false)) {
            throw ValidationError(path("passengers"), "passengers is only allowed when input_mode is 'csv'");
        }
    } else {
        s.passengers = parse_passengers(*find(sc, "passengers", true));
    }

    s.min_counters = to_size(*get_uint(sc, "min_counters", true), "min_counters");
    s.max_counters = to_size(*get_uint(sc, "max_counters", true), "max_counters");
    if (auto cur = get_uint(sc, "current_counters", false)) s.current_counters = to_size(*cur, "current_counters");

    s.costs.counter_cost_per_hour = *get_number(sc, "counter_cost_per_hour", true);
    s.costs.waiting_cost_per_passenger_hour = *get_number(sc, "waiting_cost_per_passenger_hour", true);
    s.costs.sla_wait_minutes = *get_number(sc, "sla_wait_minutes", true);
    s.costs.sla_penalty_per_passenger = *get_number(sc, "sla_penalty_per_passenger", true);

    s.replications = to_size(get_uint(sc, "replications", false).value_or(1), "replications");
    s.seed = get_uint(sc, "seed", false).value_or(42);
    s.parallel = get_bool(sc, "parallel").value_or(false);
    s.max_threads = to_size(get_uint(sc, "max_threads", false).value_or(4), "max_threads");
    s.record_trace = get_bool(sc, "record_trace").value_or(true);

    validate_scenario(s);
    if (!synthetic) s.replications = 1;  // a fixed dataset has no stochastic replications
    return s;
}

json scenario_to_json(const Scenario& s) {
    const bool synthetic = s.input_mode == InputMode::Synthetic;
    json j{{"name", s.name},
           {"input_mode", synthetic ? "synthetic" : "csv"},
           {"min_counters", s.min_counters},
           {"max_counters", s.max_counters},
           {"current_counters", s.current_counters ? json(*s.current_counters) : json(nullptr)},
           {"counter_cost_per_hour", s.costs.counter_cost_per_hour},
           {"waiting_cost_per_passenger_hour", s.costs.waiting_cost_per_passenger_hour},
           {"sla_wait_minutes", s.costs.sla_wait_minutes},
           {"sla_penalty_per_passenger", s.costs.sla_penalty_per_passenger},
           {"replications", s.replications},
           {"parallel", s.parallel},
           {"max_threads", s.max_threads},
           {"record_trace", s.record_trace}};
    if (synthetic) {
        j["arrival_window_minutes"] = s.arrival_window_minutes;
        j["arrival_rate_per_hour"] = s.arrival_rate_per_hour;
        j["mean_service_time_minutes"] = s.mean_service_time_minutes;
        j["service_time_distribution"] =
            s.service_distribution == ServiceDistribution::Exponential ? "exponential" : "deterministic";
        j["seed"] = s.seed;
    } else {
        j["passenger_count"] = s.passengers.size();
    }
    return j;
}

json metrics_to_json(const SimulationMetrics& m) {
    return json{{"customers_arrived", m.customers_arrived},
                {"customers_served", m.customers_served},
                {"finish_time_min", m.finish_time_min},
                {"mean_wait_min", m.mean_wait_min},
                {"median_wait_min", m.median_wait_min},
                {"p95_wait_min", m.p95_wait_min},
                {"max_wait_min", m.max_wait_min},
                {"total_wait_min", m.total_wait_min},
                {"mean_queue_length", m.mean_queue_length},
                {"max_queue_length", m.max_queue_length},
                {"utilization_percent", m.utilization_percent},
                {"throughput_per_hour", m.throughput_per_hour},
                {"service_level_percent", m.service_level_percent},
                {"sla_breach_count", m.sla_breach_count},
                {"staffing_cost", m.staffing_cost},
                {"waiting_cost", m.waiting_cost},
                {"sla_penalty", m.sla_penalty},
                {"total_objective_cost", m.total_objective_cost}};
}

json simulation_to_json(const SimulationResult& r) {
    json records = json::array();
    for (const auto& rec : r.service_records) {
        records.push_back({{"passenger_id", rec.passenger_id},
                           {"counter_id", rec.counter_id},
                           {"arrival_time_min", rec.arrival_time_min},
                           {"service_start_min", rec.service_start_min},
                           {"service_end_min", rec.service_end_min},
                           {"wait_time_min", rec.wait_time_min},
                           {"service_time_min", rec.service_time_min}});
    }
    json trace = json::array();
    for (const auto& t : r.queue_trace) {
        trace.push_back({{"time_min", t.time_min}, {"queue_length", t.queue_length}, {"busy_counters", t.busy_counters}});
    }
    return json{{"counter_count", r.counter_count},
                {"events_processed", r.events_processed},
                {"metrics", metrics_to_json(r.metrics)},
                {"service_records", std::move(records)},
                {"queue_trace", std::move(trace)}};
}

json candidate_to_json(const CandidateSummary& c) {
    return json{{"counter_count", c.counter_count},
                {"replications", c.replications},
                {"mean_objective_cost", c.mean_objective_cost},
                {"stddev_objective_cost", c.stddev_objective_cost},
                {"mean_staffing_cost", c.mean_staffing_cost},
                {"mean_waiting_cost", c.mean_waiting_cost},
                {"mean_sla_penalty", c.mean_sla_penalty},
                {"mean_wait_min", c.mean_wait_min},
                {"mean_p95_wait_min", c.mean_p95_wait_min},
                {"mean_max_wait_min", c.mean_max_wait_min},
                {"mean_utilization_percent", c.mean_utilization_percent},
                {"mean_queue_length", c.mean_queue_length},
                {"mean_max_queue_length", c.mean_max_queue_length},
                {"mean_service_level_percent", c.mean_service_level_percent},
                {"mean_finish_time_min", c.mean_finish_time_min},
                {"mean_customers_served", c.mean_customers_served}};
}

json simulation_document(const Scenario& scenario, const SimulationResult& result) {
    json doc = simulation_to_json(result);
    doc["schema_version"] = kSchemaVersion;
    doc["ok"] = true;
    doc["run_type"] = "simulation";
    doc["scenario_name"] = scenario.name;
    doc["scenario"] = scenario_to_json(scenario);
    return doc;
}

json optimization_document(const Scenario& scenario, const OptimizationResult& r) {
    json candidates = json::array();
    for (const auto& c : r.results_by_counter_count) candidates.push_back(candidate_to_json(c));
    return json{{"schema_version", kSchemaVersion},
                {"ok", true},
                {"run_type", "optimization"},
                {"scenario_name", r.scenario_name},
                {"scenario", scenario_to_json(scenario)},
                {"run_info",
                 {{"replications", r.replications},
                  {"parallel", r.parallel},
                  {"threads_used", r.threads_used},
                  {"runtime_ms", r.runtime_ms},
                  {"representative_replication", 0}}},
                {"best_counter_count", r.best_counter_count},
                {"best_objective_cost", r.best_objective_cost},
                {"baseline_counter_count", r.baseline ? json(r.baseline->counter_count) : json(nullptr)},
                {"baseline_objective_cost", r.baseline ? json(r.baseline->mean_objective_cost) : json(nullptr)},
                {"baseline", r.baseline ? candidate_to_json(*r.baseline) : json(nullptr)},
                {"objective_savings", opt_number(r.objective_savings)},
                {"objective_savings_percent", opt_number(r.objective_savings_percent)},
                {"wait_change_percent", opt_number(r.wait_change_percent)},
                {"results_by_counter_count", std::move(candidates)},
                {"recommended_simulation", simulation_to_json(r.recommended_simulation)},
                {"baseline_simulation",
                 r.baseline_simulation ? simulation_to_json(*r.baseline_simulation) : json(nullptr)}};
}

json error_document(const std::string& code, const std::string& message, const std::string& field) {
    json err{{"code", code}, {"message", message}};
    err["field"] = field.empty() ? json(nullptr) : json(field);
    return json{{"schema_version", kSchemaVersion}, {"ok", false}, {"error", std::move(err)}};
}

}  // namespace flowsim
