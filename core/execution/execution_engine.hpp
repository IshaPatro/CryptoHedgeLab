#pragma once

#include "../common/ring_buffer.hpp"
#include "../common/signal.hpp"
#include "../latency/latency.hpp"

#include <atomic>
#include <cstdio>
#include <thread>

namespace chl {

// ─── Execution Engine ──────────────────────────────────────────────────────
// Simulated order execution. Consumes Signals and logs the "trade".
//
// In a real system, this thread would route orders to an exchange gateway
// or a paper trading matching engine. For Module 2, it simply prints
// the execution and measures the full pipeline latency:
//
//   Feed Thread → Strategy Thread → Execution Thread
//
// Hot-path characteristics:
//   - One try_pop from signal_queue (atomic load)
//   - One clock read for execution timestamp
//   - printf for output (buffered, non-blocking)
//   - Zero heap allocation

inline void execution_loop(
    SPSCRingBuffer<Signal>& signal_queue,
    std::atomic<bool>&      running)
{
    std::printf("[Exec] Thread started — simulated execution active\n");

    Signal sig{};

    while (running.load(std::memory_order_relaxed)) {
        if (!signal_queue.try_pop(sig)) {
            std::this_thread::yield();
            continue;
        }

        // Stamp execution time
        TimePoint exec_ts = now();

        // ── Compute Pipeline Latencies ──
        double feed_to_strat = elapsed_us(sig.feed_ts, sig.strategy_ts);
        double strat_to_exec = elapsed_us(sig.strategy_ts, exec_ts);
        double total         = elapsed_us(sig.feed_ts, exec_ts);

        // ── Output ──
        std::printf("═══════════════════════════════════════════\n");
        std::printf("[Tick] #%llu  Price: %.2f\n",
                    static_cast<unsigned long long>(sig.seq), sig.price);
        std::printf("[Signal] %s\n", action_str(sig.action));
        std::printf("[Exec] Executed %s @ %.2f\n",
                    action_str(sig.action), sig.price);
        std::printf("[Latency]\n");
        std::printf("  Feed → Strategy:  %6.1f µs\n", feed_to_strat);
        std::printf("  Strategy → Exec:  %6.1f µs\n", strat_to_exec);
        std::printf("  Total Pipeline:   %6.1f µs\n", total);
        std::printf("═══════════════════════════════════════════\n\n");
    }

    std::printf("[Exec] Thread exiting\n");
}

} // namespace chl
