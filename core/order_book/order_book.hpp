#pragma once

#include <cstdio>
#include <cmath>

namespace chl {

// ─── Best Bid/Ask Order Book ───────────────────────────────────────────────
// POD struct — 32 bytes, fits in a single cache line.
// No heap allocation, no containers. Update is 4 stores.
//
// This is intentionally minimal: only top-of-book for Module 1.
// Future modules will extend to a fixed-size multi-level book
// (std::array<PriceLevel, N>) for order book imbalance signals.

struct OrderBook {
    double best_bid_price = 0.0;
    double best_bid_qty   = 0.0;
    double best_ask_price = 0.0;
    double best_ask_qty   = 0.0;

    // Update from parsed depth snapshot (top-of-book only)
    void update(double bid_price, double bid_qty,
                double ask_price, double ask_qty) noexcept {
        best_bid_price = bid_price;
        best_bid_qty   = bid_qty;
        best_ask_price = ask_price;
        best_ask_qty   = ask_qty;
    }

    bool is_valid() const noexcept {
        return best_bid_price > 0.0 && best_ask_price > 0.0;
    }

    double spread() const noexcept {
        return best_ask_price - best_bid_price;
    }

    double mid_price() const noexcept {
        return (best_bid_price + best_ask_price) / 2.0;
    }

    void print() const noexcept {
        std::printf("[Book]\n");
        std::printf("  Best Bid: %.2f (%.6f)\n", best_bid_price, best_bid_qty);
        std::printf("  Best Ask: %.2f (%.6f)\n", best_ask_price, best_ask_qty);
        if (is_valid()) {
            std::printf("  Spread:   %.2f\n", spread());
        }
    }
};

} // namespace chl
