cmake_minimum_required(VERSION 3.16)
project(FlowSim VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FLOWSIM_BUILD_TESTS "Build FlowSim unit tests" ON)
option(FLOWSIM_BUILD_BENCH "Build FlowSim benchmark executable" ON)
set(FLOWSIM_SANITIZE "" CACHE STRING "Sanitizer list, e.g. address,undefined or thread")

find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found, fetching v3.11.3")
  include(FetchContent)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
  FetchContent_MakeAvailable(nlohmann_json)
endif()
find_package(Threads REQUIRED)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(FLOWSIM_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=${FLOWSIM_SANITIZE} -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${FLOWSIM_SANITIZE})
endif()

add_library(flowsim_core
  src/validation.cpp
  src/passenger_generator.cpp
  src/csv_loader.cpp
  src/simulator.cpp
  src/metrics.cpp
  src/optimizer.cpp
  src/json_io.cpp
)
target_include_directories(flowsim_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(flowsim_core PUBLIC nlohmann_json::nlohmann_json Threads::Threads)
target_compile_definitions(flowsim_core PUBLIC
  FLOWSIM_VERSION="${PROJECT_VERSION}"
  FLOWSIM_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

add_executable(flowsim_cli src/main.cpp)
target_link_libraries(flowsim_cli PRIVATE flowsim_core)

if(FLOWSIM_BUILD_BENCH)
  add_executable(flowsim_bench bench/benchmark.cpp)
  target_link_libraries(flowsim_bench PRIVATE flowsim_core)
endif()

if(FLOWSIM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
