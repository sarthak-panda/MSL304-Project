// Measures optimizer runtime for growing passenger counts, sequential versus parallel.
// usage: flowsim_bench [--sizes 1000,10000,100000] [--replications 4] [--threads N]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "flowsim/optimizer.hpp"
#include "flowsim/passenger_generator.hpp"

namespace {

struct Timing {
    double ms{};
    std::size_t threads{};
    double checksum{};
};

Timing time_grid(const std::vector<std::vector<flowsim::Passenger>>& datasets, const std::vector<std::size_t>& counters,
                 bool parallel, std::size_t threads) {
    flowsim::EvaluationPlan plan;
    plan.datasets = &datasets;
    plan.counter_values = counters;
    plan.costs = flowsim::CostParams{250.0, 35.0, 10.0, 20.0};
    plan.parallel = parallel;
    plan.max_threads = threads;
    const auto t0 = std::chrono::steady_clock::now();
    const auto grid = flowsim::evaluate_grid(plan);
    const auto t1 = std::chrono::steady_clock::now();
    double checksum = 0.0;
    for (const auto& rep : grid.metrics)
        for (const auto& m : rep) checksum += m.total_objective_cost;
    return Timing{std::chrono::duration<double, std::milli>(t1 - t0).count(), grid.threads_used, checksum};
}

std::vector<std::size_t> parse_list(const std::string& s) {
    std::vector<std::size_t> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(std::stoul(item));
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::size_t> sizes{1000, 10000, 100000};
    std::size_t replications = 4;
    std::size_t threads = std::max(1u, std::thread::hardware_concurrency());
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        if (key == "--sizes") sizes = parse_list(argv[i + 1]);
        else if (key == "--replications") replications = std::stoul(argv[i + 1]);
        else if (key == "--threads") threads = std::stoul(argv[i + 1]);
        else {
            std::cerr << "unknown argument " << key << '\n';
            return 2;
        }
    }

    // Fixed workload shape: 100 customers/hour, 6 minute exponential service (offered load 10 Erlangs),
    // window chosen so the expected count matches the requested size; counters 10..17.
    std::vector<std::size_t> counters;
    for (std::size_t c = 10; c <= 17; ++c) counters.push_back(c);
    std::printf("hardware_concurrency=%u replications=%zu counters=10..17 (%zu candidates)\n",
                std::thread::hardware_concurrency(), replications, counters.size());
    std::printf("%10s %10s %8s %12s %12s %8s %16s %s\n", "customers", "runs", "threads", "seq_ms", "par_ms",
                "speedup", "par_cust_ev/s", "match");

    for (std::size_t n : sizes) {
        const flowsim::ArrivalModel model{100.0, static_cast<double>(n) * 60.0 / 100.0, 6.0,
                                          flowsim::ServiceDistribution::Exponential};
        std::vector<std::vector<flowsim::Passenger>> datasets;
        std::size_t total_customers = 0;
        for (std::size_t r = 0; r < replications; ++r) {
            datasets.push_back(flowsim::generate_passengers(model, 1000 + r, 10 * n + 1000));
            total_customers += datasets.back().size();
        }
        const Timing seq = time_grid(datasets, counters, false, 1);
        const Timing par = time_grid(datasets, counters, true, threads);
        const double events = 2.0 * static_cast<double>(total_customers) * static_cast<double>(counters.size());
        std::printf("%10zu %10zu %8zu %12.2f %12.2f %8.2f %16.0f %s\n", total_customers / replications,
                    replications * counters.size(), par.threads, seq.ms, par.ms, seq.ms / par.ms,
                    events / (par.ms / 1000.0), seq.checksum == par.checksum ? "yes" : "NO");
    }
    return 0;
}
