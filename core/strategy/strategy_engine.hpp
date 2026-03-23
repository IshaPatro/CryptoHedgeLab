#pragma once

#include "../common/ring_buffer.hpp"
#include "../common/tick.hpp"
#include "../common/signal.hpp"
#include "../latency/latency.hpp"

#include <atomic>
#include <cstdio>
#include <thread>

namespace chl {

// ─── Momentum Strategy Engine ──────────────────────────────────────────────
// Deterministic, zero-allocation strategy that compares the current price
// to the previous price:
//
//   price > prev_price → BUY
//   price < prev_price → SELL
//   price == prev_price → NONE
//
// Runs as a polling loop on its own thread. Consumes Ticks from the feed
// queue and pushes Signals into the execution queue.
//
// Hot-path characteristics:
//   - One try_pop from tick_queue (atomic load)
//   - One double comparison (1 cycle)
//   - One try_push into signal_queue (atomic store)
//   - Zero heap allocation
//   - No virtual dispatch
//   - No locks

inline void strategy_loop(
    SPSCRingBuffer<Tick>&   tick_queue,
    SPSCRingBuffer<Signal>& signal_queue,
    std::atomic<bool>&      running)
{
    std::printf("[Strategy] Thread started — momentum strategy active\n");

    double  prev_price = 0.0;
    Tick    tick{};

    while (running.load(std::memory_order_relaxed)) {
        // Non-blocking poll of the tick queue
        if (!tick_queue.try_pop(tick)) {
            // Queue empty — yield to avoid busy-spinning at 100% CPU.
            // In a production system with CPU pinning, you'd use
            // _mm_pause() / __builtin_arm_yield() instead.
            std::this_thread::yield();
            continue;
        }

        // Skip depth-only updates (price == 0.0 means no trade)
        if (tick.price <= 0.0) {
            continue;
        }

        // ── Strategy Logic ──
        Action action = Action::NONE;
        if (prev_price > 0.0) {
            if (tick.price > prev_price)       action = Action::BUY;
            else if (tick.price < prev_price)  action = Action::SELL;
        }
        prev_price = tick.price;

        // Only emit actionable signals
        if (action == Action::NONE) continue;

        // ── Build Signal ──
        Signal sig{};
        sig.action      = action;
        sig.price       = tick.price;
        sig.best_bid    = tick.best_bid;
        sig.best_ask    = tick.best_ask;
        sig.feed_ts     = tick.feed_ts;       // Carry through for latency
        sig.strategy_ts = now();              // Stamp strategy completion
        sig.seq         = tick.seq;

        // Push into execution queue (drop if full — stale signals are useless)
        signal_queue.try_push(sig);
    }

    std::printf("[Strategy] Thread exiting\n");
}

} // namespace chl
