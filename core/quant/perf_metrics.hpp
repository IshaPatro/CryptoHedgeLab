#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <string>

namespace chl {

// ─── Incremental Performance Metrics ─────────────────────────────────────
// Updated on every tick and fill. No batch computation at the end.
// All stats are computed via running accumulators to keep the hot path O(1).
//
//  Sharpe:    (mean_pnl_delta / stddev_pnl_delta) * sqrt(N)
//  MaxDD:     peak_pnl - current_pnl (running)
//  WinRate:   winning_fills / total_fills

struct PerfMetrics {
    std::string name;

    // ─── PnL tracking ────────────────────────────────────────────────────
    double total_pnl        = 0.0;
    double realized_pnl     = 0.0;
    double unrealized_pnl   = 0.0;
    double hedge_pnl        = 0.0;   // realized hedge contribution

    // ─── Sharpe / Volatility (Welford-style running stats) ──────────────
    uint64_t tick_count     = 0;
    double   prev_pnl       = 0.0;
    double   mean_delta     = 0.0;   // running mean of tick-to-tick PnL changes
    double   M2             = 0.0;   // running sum of squared deviations (Welford)

    // ─── Drawdown ────────────────────────────────────────────────────────
    double peak_pnl         = 0.0;
    double max_drawdown     = 0.0;   // positive value = maximum loss from peak

    // ─── Fill stats ──────────────────────────────────────────────────────
    uint64_t total_fills    = 0;
    uint64_t winning_fills  = 0;     // fill that increased realized_pnl

    // ─── Update on every tick (pass current total PnL) ──────────────────
    void on_tick(double current_pnl) {
        total_pnl = current_pnl;

        // Drawdown
        if (current_pnl > peak_pnl) peak_pnl = current_pnl;
        double dd = peak_pnl - current_pnl;
        if (dd > max_drawdown) max_drawdown = dd;

        // Welford's online mean/variance for Sharpe
        if (tick_count > 0) {
            double delta = current_pnl - prev_pnl;
            tick_count++;
            double delta2 = delta - mean_delta;
            mean_delta += delta2 / static_cast<double>(tick_count);
            M2 += delta2 * (delta - mean_delta);
        } else {
            tick_count = 1;
        }
        prev_pnl = current_pnl;
    }

    // ─── Update on fill (pass realized PnL delta from this fill) ────────
    void on_fill(double realized_delta) {
        total_fills++;
        if (realized_delta > 0.0) winning_fills++;
        realized_pnl += realized_delta;
    }

    // ─── Derived metrics ─────────────────────────────────────────────────
    double win_rate() const {
        if (total_fills == 0) return 0.0;
        return static_cast<double>(winning_fills) / static_cast<double>(total_fills);
    }

    double volatility() const {
        if (tick_count < 2) return 0.0;
        return std::sqrt(M2 / static_cast<double>(tick_count - 1));
    }

    double sharpe() const {
        double vol = volatility();
        if (vol < 1e-12) return 0.0;
        return (mean_delta / vol) * std::sqrt(static_cast<double>(tick_count));
    }

    // ─── Console print ───────────────────────────────────────────────────
    void print_row() const {
        std::printf("  %-18s │ %+8.4f │ %6.2f │ %8.4f │ %6.1f%% │ %6.4f\n",
            name.c_str(),
            total_pnl,
            sharpe(),
            max_drawdown,
            win_rate() * 100.0,
            volatility()
        );
    }
};

} // namespace chl
