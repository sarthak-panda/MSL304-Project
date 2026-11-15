#include "flowsim/simulator.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <queue>
#include <stdexcept>

#include "flowsim/event.hpp"
#include "flowsim/metrics.hpp"
#include "flowsim/validation.hpp"

namespace flowsim {

SimulationResult simulate(const std::vector<Passenger>& passengers, std::size_t counter_count,
                          const CostParams& costs, const SimulationOptions& options) {
    if (counter_count == 0) throw std::invalid_argument("counter_count must be at least 1");
    for (std::size_t i = 1; i < passengers.size(); ++i) {
        if (passengers[i].arrival_time_min < passengers[i - 1].arrival_time_min) {
            throw std::invalid_argument("passengers must be sorted by arrival time");
        }
    }

    const std::size_t n = passengers.size();
    const std::size_t max_events = options.max_events ? options.max_events : limits::events_limit(n);

    SimulationResult result;
    result.counter_count = counter_count;

    std::priority_queue<Event, std::vector<Event>, EventLater> events;
    std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> idle_counters;
    for (std::size_t c = 0; c < counter_count; ++c) idle_counters.push(c);
    std::deque<std::size_t> waiting;  // passenger indices in FIFO order
    std::vector<double> start_time(n, 0.0);
    std::vector<double> waits;
    waits.reserve(n);
    if (options.record_trace) {
        result.service_records.resize(n);
        result.queue_trace.reserve(2 * n + 1);
    }

    std::uint64_t sequence = 0;
    std::size_t busy = 0;
    std::size_t arrived = 0;
    std::size_t max_queue = 0;
    double busy_time = 0.0;
    double queue_area = 0.0;
    double last_time = 0.0;

    auto record_trace_point = [&](double t) {
        if (!options.record_trace) return;
        auto& trace = result.queue_trace;
        if (!trace.empty() && trace.back().time_min == t) {
            trace.back().queue_length = waiting.size();
            trace.back().busy_counters = busy;
        } else {
            trace.push_back(TracePoint{t, waiting.size(), busy});
        }
    };
    auto start_service = [&](std::size_t p, std::size_t counter, double t) {
        start_time[p] = t;
        ++busy;
        events.push(Event{t + passengers[p].service_time_min, EventType::ServiceCompletion, sequence++, p, counter});
    };

    record_trace_point(0.0);
    if (n > 0) events.push(Event{passengers[0].arrival_time_min, EventType::Arrival, sequence++, 0, 0});

    while (!events.empty()) {
        if (result.events_processed >= max_events) {
            result.completed = false;
            result.incomplete_reason = "event safety limit of " + std::to_string(max_events) +
                                       " reached before all customers were served";
            result.service_records.clear();
            result.queue_trace.clear();
            return result;
        }
        const Event e = events.top();
        events.pop();
        ++result.events_processed;

        queue_area += static_cast<double>(waiting.size()) * (e.time_min - last_time);
        last_time = e.time_min;

        if (e.type == EventType::Arrival) {
            ++arrived;
            const std::size_t next = e.passenger_index + 1;
            if (next < n) events.push(Event{passengers[next].arrival_time_min, EventType::Arrival, sequence++, next, 0});
            if (!idle_counters.empty()) {
                const std::size_t counter = idle_counters.top();
                idle_counters.pop();
                start_service(e.passenger_index, counter, e.time_min);
            } else {
                waiting.push_back(e.passenger_index);
            }
        } else {
            const std::size_t p = e.passenger_index;
            const Passenger& passenger = passengers[p];
            --busy;
            const double wait = start_time[p] - passenger.arrival_time_min;
            waits.push_back(wait);
            busy_time += passenger.service_time_min;
            if (options.record_trace) {
                result.service_records[p] = ServiceRecord{passenger.id, e.counter_index + 1, passenger.arrival_time_min,
                                                          start_time[p], e.time_min, wait, passenger.service_time_min};
            }
            if (!waiting.empty()) {
                const std::size_t front = waiting.front();
                waiting.pop_front();
                start_service(front, e.counter_index, e.time_min);
            } else {
                idle_counters.push(e.counter_index);
            }
        }
        max_queue = std::max(max_queue, waiting.size());
        record_trace_point(e.time_min);
    }

    MetricInputs inputs;
    inputs.counter_count = counter_count;
    inputs.customers_arrived = arrived;
    inputs.finish_time_min = last_time;
    inputs.total_busy_time_min = busy_time;
    inputs.queue_area = queue_area;
    inputs.max_queue_length = max_queue;
    inputs.waits = std::move(waits);
    result.metrics = compute_metrics(std::move(inputs), costs);
    return result;
}

}  // namespace flowsim
