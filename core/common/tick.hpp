#pragma once

#include "../latency/latency.hpp"

#include <cstdint>

namespace chl {

// ─── Tick ──────────────────────────────────────────────────────────────────
// Normalized market data event pushed from the Feed Thread into the
// Strategy Thread via the SPSC ring buffer.
//
// Layout: 64 bytes — fits exactly in one cache line.
// All fields are POD. No pointers, no strings, no heap.

struct Tick {
    double    price;         // Last trade price (0.0 if depth-only update)
    double    best_bid;      // Top-of-book bid price
    double    best_ask;      // Top-of-book ask price
    double    quantity;      // Trade quantity (0.0 if depth-only)
    int64_t   exchange_ts;   // Binance trade timestamp (ms since epoch)
    TimePoint feed_ts;       // Stamped by feed thread when tick is produced
    uint32_t  seq;           // Monotonic sequence number (32-bit to save space)
    uint32_t  symbol_id;     // 0=BTC_SPOT, 1=ETH_SPOT, 2=BTC_PERP
    float     funding_rate;  // Latest funding rate (0.0 if not a funding tick)

    void print() const noexcept {
        std::printf("[Tick] #%llu  Price: %.2f  Bid: %.2f  Ask: %.2f\n",
                    static_cast<unsigned long long>(seq),
                    price, best_bid, best_ask);
    }
};

} // namespace chl
