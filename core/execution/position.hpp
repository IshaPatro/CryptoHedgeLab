#pragma once

#include <cstdio>
#include <cmath>

namespace chl {

// ─── Position ──────────────────────────────────────────────────────────────
// Tracks the current net position and average entry price.
//
// Convention:
//   qty > 0  → long position
//   qty < 0  → short position
//   qty == 0 → flat (no position)
//
// Average price is weighted: when adding to a position in the same direction,
// the avg_price is adjusted. When reducing/flipping, realized PnL is computed
// and returned to the caller.
//
// POD struct. 16 bytes. No heap, no virtuals.

struct Position {
    double qty       = 0.0;   // Net quantity (positive=long, negative=short)
    double avg_price = 0.0;   // Volume-weighted average entry price

    bool is_flat()  const noexcept { return qty == 0.0; }
    bool is_long()  const noexcept { return qty > 0.0; }
    bool is_short() const noexcept { return qty < 0.0; }

    // ── Apply a fill and return realized PnL (if any) ──────────────────
    //
    // Case 1: Opening or adding to position (same direction)
    //   → Update avg_price as weighted average. No realized PnL.
    //
    // Case 2: Reducing position (opposite direction, |fill_qty| ≤ |position|)
    //   → Realize PnL on the closed portion. Avg_price stays the same.
    //
    // Case 3: Flipping position (opposite direction, |fill_qty| > |position|)
    //   → Realize PnL on the old position. Open new position at fill_price.
    //
    double apply_fill(double fill_qty, double fill_price) noexcept {
        // fill_qty is signed: positive for BUY, negative for SELL
        double realized_pnl = 0.0;

        if (is_flat()) {
            // Opening new position
            qty       = fill_qty;
            avg_price = fill_price;

        } else if ((qty > 0.0 && fill_qty > 0.0) ||
                   (qty < 0.0 && fill_qty < 0.0)) {
            // Adding to existing position (same direction)
            // Weighted average: new_avg = (old_qty*old_avg + fill_qty*fill_price) / total
            double total = qty + fill_qty;
            avg_price = (qty * avg_price + fill_qty * fill_price) / total;
            qty = total;

        } else {
            // Opposite direction — reducing or flipping
            double close_qty = std::min(std::abs(fill_qty), std::abs(qty));

            // Realized PnL on closed portion
            // Long closed by sell: pnl = close_qty * (fill_price - avg_price)
            // Short closed by buy: pnl = close_qty * (avg_price - fill_price)
            if (qty > 0.0) {
                realized_pnl = close_qty * (fill_price - avg_price);
            } else {
                realized_pnl = close_qty * (avg_price - fill_price);
            }

            double remaining = std::abs(fill_qty) - close_qty;
            if (remaining < 1e-12) {
                // Position reduced or fully closed
                if (std::abs(qty) - close_qty < 1e-12) {
                    // Fully closed
                    qty = 0.0;
                    avg_price = 0.0;
                } else {
                    // Partially closed — keep same avg_price, reduce qty
                    if (qty > 0.0)
                        qty -= close_qty;
                    else
                        qty += close_qty;
                }
            } else {
                // Flipped — old position fully closed, open new in opposite direction
                qty = (fill_qty > 0.0) ? remaining : -remaining;
                avg_price = fill_price;
            }
        }

        return realized_pnl;
    }

    void print() const noexcept {
        std::printf("[Position]\n");
        if (is_flat()) {
            std::printf("  Flat (no position)\n");
        } else {
            std::printf("  Qty:       %+.6f %s\n", qty, is_long() ? "(LONG)" : "(SHORT)");
            std::printf("  Avg Price: %.2f\n", avg_price);
        }
    }
};

} // namespace chl
