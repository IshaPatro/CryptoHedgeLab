#pragma once

#include "strategy_engine.hpp"
#include "comparison_engine.hpp"
#include "../kdb/replay_engine.hpp"
#include "../common/ring_buffer.hpp"
#include "../common/tick.hpp"

#include <cstdio>
#include <cstdint>

namespace chl {

// ─── BacktestRunner ──────────────────────────────────────────────────────
// Wires the ReplayEngine (kdb+ historical data) into the StrategyEngine
// (N strategies). Tick delivery happens synchronously in the caller's thread
// — no SPSC queues between replay and strategy evaluation, allowing maximum
// throughput (target: >1M ticks/sec).
//
// Usage:
//   BacktestRunner runner;
//   runner.add_strategy(def1);
//   runner.add_strategy(def2);
//   runner.run(start_ns, end_ns, speed_multiplier);
//   runner.print_results();

class BacktestRunner {
public:
    explicit BacktestRunner(RankBy rank_by = RankBy::TOTAL_PNL)
        : strat_engine_()
        , cmp_engine_(rank_by)
    {}

    void add_strategy(const StrategyDef& def) {
        strat_engine_.add_strategy(def);
    }

    // ─── Run backtest — drives ticks directly through StrategyEngine ─────
    // Uses an inline tick callback instead of the SPSC queue to maximise
    // throughput: each historical tick is immediately evaluated by all N
    // strategies without queuing overhead.
    void run(uint64_t start_ns, uint64_t end_ns, double speed_multiplier) {
        if (strat_engine_.strategy_count() == 0) {
            std::printf("[Backtest] No strategies loaded — aborting.\n");
            return;
        }

        std::printf("[Backtest] Starting %zu strateg%s, speed=%.1fx\n",
            strat_engine_.strategy_count(),
            strat_engine_.strategy_count() == 1 ? "y" : "ies",
            speed_multiplier <= 0.0 ? 0.0 : speed_multiplier);

        // ReplayEngine normally writes to a SPSCRingBuffer; here we give it
        // a private queue and drain it immediately after each fetch.
        SPSCRingBuffer<Tick> local_queue;
        ReplayEngine replay(local_queue);

        // Run replay, hooking the strategy engine in-line
        replay.run_with_callback(start_ns, end_ns, speed_multiplier,
            [this](const Tick& tick) {
                strat_engine_.on_tick(tick);
            });

        tick_count_ = replay.tick_count();
        std::printf("[Backtest] Complete — %llu ticks processed.\n",
                    static_cast<unsigned long long>(tick_count_));
    }

    // ─── Print results ────────────────────────────────────────────────────
    void print_results() const {
        auto results = strat_engine_.collect_results();
        cmp_engine_.print_ranked(results);
    }

    uint64_t tick_count() const { return tick_count_; }

private:
    StrategyEngine  strat_engine_;
    ComparisonEngine cmp_engine_;
    uint64_t tick_count_{0};
};

} // namespace chl
