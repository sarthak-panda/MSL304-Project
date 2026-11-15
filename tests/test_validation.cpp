#include <sstream>

#include "flowsim/csv_loader.hpp"
#include "flowsim/json_io.hpp"
#include "test_helpers.hpp"

using nlohmann::json;

namespace {

json base_doc() {
    return json::parse(R"({
      "schema_version": 1,
      "scenario": {
        "name": "Test", "input_mode": "synthetic",
        "arrival_window_minutes": 120, "arrival_rate_per_hour": 30, "mean_service_time_minutes": 4,
        "service_time_distribution": "exponential",
        "min_counters": 1, "max_counters": 4, "current_counters": 2,
        "counter_cost_per_hour": 100, "waiting_cost_per_passenger_hour": 20,
        "sla_wait_minutes": 10, "sla_penalty_per_passenger": 5,
        "replications": 3, "seed": 7, "parallel": false, "max_threads": 2, "record_trace": false
      }})");
}

json csv_doc() {
    json d = base_doc();
    d["scenario"]["input_mode"] = "csv";
    d["scenario"]["passengers"] = json::array({{{"passenger_id", "P2"}, {"arrival_time_min", 3.0}, {"service_time_min", 2.0}},
                                               {{"passenger_id", "P1"}, {"arrival_time_min", 1.0}, {"service_time_min", 1.5}}});
    return d;
}

std::string field_of(const json& doc) {
    try {
        flowsim::parse_scenario(doc);
    } catch (const flowsim::ValidationError& e) {
        return e.field();
    }
    return "<accepted>";
}

}  // namespace

TEST_CASE("validation: a complete synthetic scenario parses with documented values") {
    const auto s = flowsim::parse_scenario(base_doc());
    CHECK_EQ(s.name, std::string("Test"));
    CHECK_EQ(s.min_counters, 1u);
    CHECK_EQ(s.max_counters, 4u);
    CHECK(s.current_counters.has_value());
    CHECK_EQ(s.replications, 3u);
    CHECK_EQ(s.seed, 7u);
    CHECK(!s.record_trace);
}

TEST_CASE("validation: documented defaults apply to optional fields") {
    json d = base_doc();
    for (const char* key : {"input_mode", "service_time_distribution", "current_counters", "replications", "seed",
                            "parallel", "max_threads", "record_trace"})
        d["scenario"].erase(key);
    const auto s = flowsim::parse_scenario(d);
    CHECK(s.input_mode == flowsim::InputMode::Synthetic);
    CHECK(s.service_distribution == flowsim::ServiceDistribution::Exponential);
    CHECK(!s.current_counters.has_value());
    CHECK_EQ(s.replications, 1u);
    CHECK_EQ(s.seed, 42u);
    CHECK(!s.parallel);
    CHECK_EQ(s.max_threads, 4u);
    CHECK(s.record_trace);
}

TEST_CASE("validation: malformed values are rejected with the offending field") {
    auto with = [](const char* key, json value) {
        json d = base_doc();
        d["scenario"][key] = std::move(value);
        return d;
    };
    CHECK_EQ(field_of(with("arrival_rate_per_hour", 0)), std::string("scenario.arrival_rate_per_hour"));
    CHECK_EQ(field_of(with("arrival_rate_per_hour", -3)), std::string("scenario.arrival_rate_per_hour"));
    CHECK_EQ(field_of(with("mean_service_time_minutes", 0)), std::string("scenario.mean_service_time_minutes"));
    CHECK_EQ(field_of(with("arrival_window_minutes", -1)), std::string("scenario.arrival_window_minutes"));
    CHECK_EQ(field_of(with("arrival_window_minutes", 20000)), std::string("scenario.arrival_window_minutes"));
    CHECK_EQ(field_of(with("min_counters", 0)), std::string("scenario.min_counters"));
    CHECK_EQ(field_of(with("min_counters", 5)), std::string("scenario.max_counters"));
    CHECK_EQ(field_of(with("max_counters", 500)), std::string("scenario.max_counters"));
    CHECK_EQ(field_of(with("current_counters", 0)), std::string("scenario.current_counters"));
    CHECK_EQ(field_of(with("counter_cost_per_hour", -1)), std::string("scenario.counter_cost_per_hour"));
    CHECK_EQ(field_of(with("sla_penalty_per_passenger", "ten")), std::string("scenario.sla_penalty_per_passenger"));
    CHECK_EQ(field_of(with("replications", 0)), std::string("scenario.replications"));
    CHECK_EQ(field_of(with("replications", 2.5)), std::string("scenario.replications"));
    CHECK_EQ(field_of(with("seed", -1)), std::string("scenario.seed"));
    CHECK_EQ(field_of(with("max_threads", 0)), std::string("scenario.max_threads"));
    CHECK_EQ(field_of(with("service_time_distribution", "gamma")), std::string("scenario.service_time_distribution"));
    CHECK_EQ(field_of(with("input_mode", "excel")), std::string("scenario.input_mode"));
    CHECK_EQ(field_of(with("parallel", "yes")), std::string("scenario.parallel"));
    CHECK_EQ(field_of(with("name", "")), std::string("scenario.name"));
    CHECK_EQ(field_of(with("arrival_rate_per_hour", 1e6)), std::string("scenario.arrival_rate_per_hour"));
    CHECK_EQ(field_of(with("typo_field", 1)), std::string("scenario.typo_field"));
    json no_name = base_doc();
    no_name["scenario"].erase("name");
    CHECK_EQ(field_of(no_name), std::string("scenario.name"));
    json bad_version = base_doc();
    bad_version["schema_version"] = 2;
    CHECK_EQ(field_of(bad_version), std::string("schema_version"));
    CHECK_EQ(field_of(json::array()), std::string(""));
}

TEST_CASE("validation: csv-mode passengers are validated and replications forced to one") {
    const auto s = flowsim::parse_scenario(csv_doc());
    CHECK(s.input_mode == flowsim::InputMode::Csv);
    CHECK_EQ(s.passengers.size(), 2u);
    CHECK_EQ(s.replications, 1u);

    json dup = csv_doc();
    dup["scenario"]["passengers"][1]["passenger_id"] = "P2";
    CHECK_EQ(field_of(dup), std::string("scenario.passengers[1].passenger_id"));
    json neg = csv_doc();
    neg["scenario"]["passengers"][0]["arrival_time_min"] = -1.0;
    CHECK_EQ(field_of(neg), std::string("scenario.passengers[0].arrival_time_min"));
    json zero = csv_doc();
    zero["scenario"]["passengers"][0]["service_time_min"] = 0.0;
    CHECK_EQ(field_of(zero), std::string("scenario.passengers[0].service_time_min"));
    json empty_id = csv_doc();
    empty_id["scenario"]["passengers"][0]["passenger_id"] = "";
    CHECK_EQ(field_of(empty_id), std::string("scenario.passengers[0].passenger_id"));
    json missing = csv_doc();
    missing["scenario"].erase("passengers");
    CHECK_EQ(field_of(missing), std::string("scenario.passengers"));
    json synthetic_with_passengers = csv_doc();
    synthetic_with_passengers["scenario"]["input_mode"] = "synthetic";
    CHECK_EQ(field_of(synthetic_with_passengers), std::string("scenario.passengers"));
}

TEST_CASE("csv: parses out-of-order rows with any column order") {
    std::istringstream in("arrival_time_min,passenger_id,service_time_min\r\n2.5,B,1\r\n\r\n0.5, A ,2\r\n");
    const auto ps = flowsim::parse_passengers_csv(in);
    CHECK_EQ(ps.size(), 2u);
    CHECK_EQ(ps[1].id, std::string("A"));
    CHECK_EQ(ps[1].arrival_time_min, 0.5);
}

TEST_CASE("csv: malformed files are rejected") {
    auto rejects = [](const std::string& text) {
        std::istringstream in(text);
        try {
            flowsim::parse_passengers_csv(in);
        } catch (const flowsim::ValidationError&) {
            return true;
        }
        return false;
    };
    CHECK(rejects(""));
    CHECK(rejects("passenger_id,arrival_time_min\nA,1\n"));
    CHECK(rejects("passenger_id,arrival_time_min,service_time_min\nA,abc,1\n"));
    CHECK(rejects("passenger_id,arrival_time_min,service_time_min\nA,nan,1\n"));
    CHECK(rejects("passenger_id,arrival_time_min,service_time_min\nA,1,inf\n"));
    CHECK(rejects("passenger_id,arrival_time_min,service_time_min\nA,1\n"));
    CHECK(rejects("passenger_id,arrival_time_min,service_time_min\nA,1,2,3\n"));
    CHECK(rejects("passenger_id,arrival_time_min,service_time_min\nA,1,0\n"));
    CHECK(rejects("passenger_id,arrival_time_min,service_time_min\nA,-1,2\n"));
    CHECK(rejects("passenger_id,arrival_time_min,service_time_min\nA,1,2\nA,2,2\n"));
    CHECK(rejects("passenger_id,arrival_time_min,service_time_min\n,1,2\n"));
    CHECK(!rejects("passenger_id,arrival_time_min,service_time_min\n"));  // header only: zero customers
}

TEST_CASE("validation: csv-mode task limit ignores unused replications") {
    json d = csv_doc();
    d["scenario"]["max_counters"] = 100;
    d["scenario"]["replications"] = 1000;
    CHECK_EQ(field_of(d), std::string("<accepted>"));
    json synthetic = base_doc();
    synthetic["scenario"]["max_counters"] = 100;
    synthetic["scenario"]["replications"] = 1000;
    CHECK_EQ(field_of(synthetic), std::string("scenario.replications"));
}
