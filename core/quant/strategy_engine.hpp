#pragma once

#include "strategy_def.hpp"
#include "strategy_instance.hpp"
#include "comparison_engine.hpp"
#include "../common/tick.hpp"

#include <vector>
#include <cstdio>

namespace chl {

// ─── StrategyEngine ───────────────────────────────────────────────────────
// Owns N StrategyInstances and routes every incoming Tick to all of them
// in a single cache-friendly loop. This is the core of the Quant Lab:
//
//   Tick ──► StrategyEngine::on_tick() ──► [Instance 0, 1, 2, ...]
//                                                   │
//                                         (no SPSC queues between strategies)
//
// Design rationale: instead of spinning up N separate strategy threads each
// with their own queue (which would require N×2 SPSC queues and N threads),
// we process all strategies in a sequential loop on one thread. This keeps
// all StrategyInstance data warm in L1/L2 cache. At N ≤ 32 strategies and
// ~1M ticks/sec, this easily fits within a single CPU core's throughput.

class StrategyEngine {
public:
    explicit StrategyEngine() = default;

    // Add a strategy. Call before start (not thread-safe after start).
    void add_strategy(const StrategyDef& def) {
        instances_.emplace_back(def);
    }

    std::size_t strategy_count() const { return instances_.size(); }

    // ─── Hot path: called for every tick ─────────────────────────────────
    void on_tick(const Tick& tick) {
        for (auto& inst : instances_)
            inst.on_tick(tick);
    }

    // ─── Results (call after run completes) ──────────────────────────────
    std::vector<PerfMetrics> collect_results() const {
        std::vector<PerfMetrics> out;
        out.reserve(instances_.size());
        for (const auto& inst : instances_)
            out.push_back(inst.result());
        return out;
    }

    void print_summary() const {
        std::printf("\n");
        std::printf("╔══════════════════════════════════════════════════════════════════════╗\n");
        std::printf("║                   Strategy Comparison Results                       ║\n");
        std::printf("╚══════════════════════════════════════════════════════════════════════╝\n");
        std::printf("  %-18s │ %8s │ %6s │ %8s │ %7s │ %6s\n",
                    "Strategy", "PnL", "Sharpe", "Max DD", "WinRate", "Vol");
        std::printf("  ─────────────────────────────────────────────────────────────────────\n");
        auto results = collect_results();
        // Sort by total PnL descending
        std::sort(results.begin(), results.end(),
                  [](const PerfMetrics& a, const PerfMetrics& b){
                      return a.total_pnl > b.total_pnl;
                  });
        for (const auto& r : results)
            r.print_row();
        std::printf("\n");
    }

private:
    std::vector<StrategyInstance> instances_;
};

} // namespace chl
