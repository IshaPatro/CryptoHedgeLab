#pragma once

#include "../common/signal.hpp"  // Action, action_str
#include "../latency/latency.hpp"

#include <cstdint>
#include <cstdio>

namespace chl {

// ─── Fill ──────────────────────────────────────────────────────────────────
// Represents a simulated order fill. In paper trading, fills are generated
// instantly when a signal arrives — no matching engine delay.
//
// BUY  fills execute at best_ask (crossing the spread, lifting the offer)
// SELL fills execute at best_bid (crossing the spread, hitting the bid)
//
// POD struct. 48 bytes. No heap.

struct Fill {
    Action    side;          // BUY or SELL
    double    price;         // Execution price (ask for BUY, bid for SELL)
    double    quantity;      // Filled quantity
    TimePoint fill_ts;       // Stamped when fill is generated
    uint64_t  seq;           // Sequence from originating signal

    void print() const noexcept {
        std::printf("[Fill] %s %.6f @ %.2f\n",
                    action_str(side), quantity, price);
    }
};

} // namespace chl
