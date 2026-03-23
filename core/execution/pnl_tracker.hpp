#pragma once

#include <cstdio>
#include <cmath>

namespace chl {

// ─── PnL Tracker ───────────────────────────────────────────────────────────
// Tracks cumulative realized PnL and computes unrealized PnL on demand.
//
// Realized PnL:   Accumulated profit/loss from closed trades.
//                 Updated every time a position is reduced or flipped.
//
// Unrealized PnL: Mark-to-market profit/loss on the open position.
//                 (current_price - avg_entry_price) * position_qty
//                 For shorts: (avg_entry_price - current_price) * |qty|
//
// POD struct. 8 bytes. No heap.

struct PnLTracker {
    double realized = 0.0;   // Cumulative realized PnL (USD)

    // Add realized PnL from a fill
    void add_realized(double pnl) noexcept {
        realized += pnl;
    }

    // Compute unrealized PnL given current position and market price
    static double unrealized(double position_qty, double avg_price,
                             double current_price) noexcept {
        if (std::abs(position_qty) < 1e-12) return 0.0;

        // Long:  profit when market goes up
        // Short: profit when market goes down
        return position_qty * (current_price - avg_price);
    }

    void print(double position_qty, double avg_price,
               double current_price) const noexcept {
        double unreal = unrealized(position_qty, avg_price, current_price);
        double total  = realized + unreal;

        std::printf("[PnL]\n");
        std::printf("  Realized:   %+.4f USD\n", realized);
        std::printf("  Unrealized: %+.4f USD\n", unreal);
        std::printf("  Total:      %+.4f USD\n", total);
    }
};

} // namespace chl
