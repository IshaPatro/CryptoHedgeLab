#pragma once

#include "../common/ring_buffer.hpp"
#include "../common/tick.hpp"

extern "C" {
#include "../../kdb/c/k.h"
}

#include <functional>
#include <cstdio>
#include <thread>
#include <chrono>

namespace chl {

class ReplayEngine {
    SPSCRingBuffer<Tick>& tick_queue_;
    int                   h_{-1};
    uint64_t              tick_count_{0};

public:
    explicit ReplayEngine(SPSCRingBuffer<Tick>& tick_queue)
        : tick_queue_(tick_queue) {}

    uint64_t tick_count() const { return tick_count_; }

    // ─── Standard mode: push ticks into the SPSC queue ──────────────────
    // Used by the main pipeline's backtest mode (Strategy/Exec threads consume)
    void run(uint64_t start_ns, uint64_t end_ns, double speed_multiplier) {
        run_impl(start_ns, end_ns, speed_multiplier,
                 [this](const Tick& tick) {
                     while (!tick_queue_.try_push(tick))
                         std::this_thread::yield();
                 });
    }

    // ─── Callback mode: deliver ticks directly to a functor ──────────────
    // Used by BacktestRunner for maximum throughput — bypasses SPSC queue
    // entirely. Strategy evaluation happens inline in the replay loop.
    void run_with_callback(uint64_t start_ns, uint64_t end_ns,
                           double speed_multiplier,
                           std::function<void(const Tick&)> callback) {
        run_impl(start_ns, end_ns, speed_multiplier, std::move(callback));
    }

private:
    void run_impl(uint64_t start_ns, uint64_t end_ns,
                  double speed_multiplier,
                  std::function<void(const Tick&)> deliver)
    {
        std::printf("[Replay] Connecting to kdb+ at localhost:5001...\n");
        h_ = khpu((S)"localhost", 5001, (S)"");
        if (h_ <= 0) {
            std::printf("[Replay] ERROR: Could not connect to kdb+.\n");
            return;
        }

        std::printf("[Replay] Connected. Querying trades from %llu to %llu...\n",
                    (unsigned long long)start_ns, (unsigned long long)end_ns);

        long long start_k = static_cast<long long>(start_ns) - 946684800000000000LL;
        long long end_k   = static_cast<long long>(end_ns)   - 946684800000000000LL;

        char query[256];
        std::snprintf(query, sizeof(query),
                      "select time, price, size from trade where time >= %lld, time <= %lld",
                      start_k, end_k);

        K res = k(h_, query, (K)0);
        if (!res) {
            std::printf("[Replay] Network error querying kdb+.\n");
            kclose(h_); return;
        }
        if (res->t != 98) {
            std::printf("[Replay] ERROR: Expected table response.\n");
            r0(res); kclose(h_); return;
        }

        K dict      = res->k;
        K cols      = kK(dict)[1];
        K time_col  = kK(cols)[0];
        K price_col = kK(cols)[1];
        K size_col  = kK(cols)[2];

        long n = time_col->n;
        std::printf("[Replay] Fetched %ld ticks. Beginning playback...\n", n);

        auto     wall_start    = std::chrono::steady_clock::now();
        uint64_t first_tick_ts = 0;
        uint64_t seq           = 0;

        for (long i = 0; i < n; ++i) {
            uint64_t tick_ts = static_cast<uint64_t>(kJ(time_col)[i] + 946684800000000000LL);
            double   price   = kF(price_col)[i];
            double   size    = kF(size_col)[i];

            if (i == 0) first_tick_ts = tick_ts;

            if (speed_multiplier > 0.0 && i > 0) {
                uint64_t elapsed_sim = tick_ts - first_tick_ts;
                uint64_t target_wall = static_cast<uint64_t>(elapsed_sim / speed_multiplier);
                while (true) {
                    auto now_time     = std::chrono::steady_clock::now();
                    auto elapsed_wall = std::chrono::duration_cast<std::chrono::nanoseconds>(now_time - wall_start).count();
                    if (static_cast<uint64_t>(elapsed_wall) >= target_wall) break;
                    if (target_wall - static_cast<uint64_t>(elapsed_wall) > 1000000)
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                    else
                        std::this_thread::yield();
                }
            }

            Tick tick{};
            tick.price       = price;
            tick.quantity    = size;
            tick.best_bid    = price - 0.01;
            tick.best_ask    = price + 0.01;
            tick.exchange_ts = static_cast<int64_t>(tick_ts / 1000000);
            tick.feed_ts     = chl::now();
            tick.seq         = ++seq;

            deliver(tick);
        }

        auto   wall_end  = std::chrono::steady_clock::now();
        double elapsed_s = std::chrono::duration<double>(wall_end - wall_start).count();

        tick_count_ = static_cast<uint64_t>(n);
        std::printf("[Replay] Finished. %ld ticks in %.3f s (%.0f ticks/sec).\n",
                    n, elapsed_s, elapsed_s > 0.0 ? n / elapsed_s : 0.0);

        r0(res);
        kclose(h_);
    }
};

} // namespace chl
