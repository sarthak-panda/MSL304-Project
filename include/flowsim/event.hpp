#pragma once

#include <cstddef>
#include <cstdint>

namespace flowsim {

// Declaration order matters: completions sort before arrivals at equal times.
enum class EventType : std::uint8_t { ServiceCompletion = 0, Arrival = 1 };

struct Event {
    double time_min{};
    EventType type{};
    std::uint64_t sequence{};
    std::size_t passenger_index{};
    std::size_t counter_index{};  // 0-based, meaningful for completion events
};

// Strict weak ordering on (time, type, sequence). std::priority_queue is a max-heap, so this
// returns true when `a` should be processed AFTER `b`, which puts the earliest event on top.
struct EventLater {
    bool operator()(const Event& a, const Event& b) const noexcept {
        if (a.time_min != b.time_min) return a.time_min > b.time_min;
        if (a.type != b.type) return a.type > b.type;
        return a.sequence > b.sequence;
    }
};

}  // namespace flowsim
