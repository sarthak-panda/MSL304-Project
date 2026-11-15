add_executable(flowsim_tests
  test_main.cpp
  test_simulator.cpp
  test_metrics.cpp
  test_optimizer.cpp
  test_validation.cpp
)
target_link_libraries(flowsim_tests PRIVATE flowsim_core)
add_test(NAME flowsim_unit_tests COMMAND flowsim_tests)

add_test(NAME flowsim_cli_version COMMAND flowsim_cli version)
add_test(NAME flowsim_cli_simulate_example
  COMMAND flowsim_cli simulate
          --input ${PROJECT_SOURCE_DIR}/config/example_scenario.json
          --output ${CMAKE_CURRENT_BINARY_DIR}/example_simulation.json
          --counters 4)
add_test(NAME flowsim_cli_optimize_example
  COMMAND flowsim_cli optimize
          --input ${PROJECT_SOURCE_DIR}/config/example_scenario.json
          --output ${CMAKE_CURRENT_BINARY_DIR}/example_optimization.json)
add_test(NAME flowsim_cli_rejects_unknown_command COMMAND flowsim_cli frobnicate)
set_tests_properties(flowsim_cli_rejects_unknown_command PROPERTIES WILL_FAIL TRUE)
