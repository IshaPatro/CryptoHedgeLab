#pragma once

#include "../latency/latency.hpp"

#include <cstdint>
#include <cstdio>

namespace chl {

// ─── Signal Action ─────────────────────────────────────────────────────────
enum class Action : uint8_t {
    NONE = 0,
    BUY  = 1,
    SELL = 2
};

inline const char* action_str(Action a) noexcept {
    switch (a) {
        case Action::BUY:  return "BUY";
        case Action::SELL: return "SELL";
        default:           return "NONE";
    }
}

// ─── Signal ────────────────────────────────────────────────────────────────
// Emitted by the Strategy Thread, consumed by the Execution Thread.
// Carries the strategy decision plus timestamps for latency measurement.
//
// POD struct. No heap, no virtuals.

struct Signal {
    Action    action;         // BUY / SELL / NONE
    double    price;          // Price at which the signal was generated
    double    best_bid;       // Book state at signal time
    double    best_ask;
    TimePoint feed_ts;        // Carried through from the originating Tick
    TimePoint strategy_ts;    // Stamped by strategy thread when signal is produced
    uint64_t  seq;            // Sequence number from originating tick

    void print() const noexcept {
        std::printf("[Signal] %s @ %.2f\n", action_str(action), price);
    }
};

} // namespace chl
