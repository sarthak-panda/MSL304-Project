#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "flowsim/types.hpp"

namespace flowsim {

inline constexpr int kSchemaVersion = 1;

// Parses and validates a scenario document. Throws ValidationError.
Scenario parse_scenario(const nlohmann::json& doc);

nlohmann::json scenario_to_json(const Scenario& scenario);
nlohmann::json metrics_to_json(const SimulationMetrics& m);
nlohmann::json simulation_to_json(const SimulationResult& result);
nlohmann::json candidate_to_json(const CandidateSummary& c);
nlohmann::json simulation_document(const Scenario& scenario, const SimulationResult& result);
nlohmann::json optimization_document(const Scenario& scenario, const OptimizationResult& result);
nlohmann::json error_document(const std::string& code, const std::string& message, const std::string& field);

}  // namespace flowsim
