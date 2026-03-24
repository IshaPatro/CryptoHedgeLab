#pragma once

#include "perf_metrics.hpp"
#include "../kdb/kdb_record.hpp"

#include <vector>
#include <algorithm>
#include <cstdio>
#include <ctime>

namespace chl {

// ─── ComparisonEngine ────────────────────────────────────────────────────
// Takes the collected PerfMetrics from a StrategyEngine run and:
//   1. Ranks strategies by a chosen metric
//   2. Prints a formatted comparison table to stdout
//   3. Serializes results for kdb+ storage

enum class RankBy {
    TOTAL_PNL,
    SHARPE,
    MIN_DRAWDOWN,
    WIN_RATE,
};

class ComparisonEngine {
public:
    explicit ComparisonEngine(RankBy rank_by = RankBy::TOTAL_PNL)
        : rank_by_(rank_by) {}

    // Rank and print results. Returns sorted results (best first).
    std::vector<PerfMetrics> rank(std::vector<PerfMetrics> results) const {
        std::sort(results.begin(), results.end(),
                  [this](const PerfMetrics& a, const PerfMetrics& b) {
                      switch (rank_by_) {
                          case RankBy::TOTAL_PNL:    return a.total_pnl > b.total_pnl;
                          case RankBy::SHARPE:       return a.sharpe()  > b.sharpe();
                          case RankBy::MIN_DRAWDOWN: return a.max_drawdown < b.max_drawdown;
                          case RankBy::WIN_RATE:     return a.win_rate() > b.win_rate();
                          default:                   return a.total_pnl > b.total_pnl;
                      }
                  });
        return results;
    }

    void print(const std::vector<PerfMetrics>& results) const {
        std::printf("\n");
        std::printf("╔══════════════════════════════════════════════════════════════════════════╗\n");
        std::printf("║                    Quant Lab — Strategy Comparison                      ║\n");
        std::printf("╚══════════════════════════════════════════════════════════════════════════╝\n");
        std::printf("  %-4s │ %-18s │ %9s │ %6s │ %8s │ %7s │ %6s\n",
                    "Rank", "Strategy", "PnL (USD)", "Sharpe", "Max DD", "WinRate", "Vol");
        std::printf("  ──────────────────────────────────────────────────────────────────────────\n");
        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            std::printf("  [%2zu] │ %-18s │ %+9.4f │ %6.2f │ %8.4f │ %6.1f%% │ %6.4f\n",
                i + 1,
                r.name.c_str(),
                r.total_pnl,
                r.sharpe(),
                r.max_drawdown,
                r.win_rate() * 100.0,
                r.volatility()
            );
        }
        std::printf("\n");
    }

    void print_ranked(std::vector<PerfMetrics> results) const {
        print(rank(std::move(results)));
    }

private:
    RankBy rank_by_;
};

} // namespace chl
