#pragma once

#include "position.hpp"
#include "pnl_tracker.hpp"

#include <cmath>
#include <algorithm>
#include <cstdio>

namespace chl {

class HedgeEngine {
    double     hedge_ratio_;
    bool       enabled_;
    
    Position   futures_pos_{};
    PnLTracker futures_pnl_{};

    // Performance Metrics
    double peak_unhedged_pnl_{0.0};
    double peak_hedged_pnl_{0.0};
    
    double max_dd_unhedged_{0.0};
    double max_dd_hedged_{0.0};

    // For volatility / Sharpe (simplified running variance of tick-to-tick PnL changes)
    double prev_unhedged_pnl_{0.0};
    double prev_hedged_pnl_{0.0};
    double sum_unhedged_pnl_diff_{0.0};
    double sum_hedged_pnl_diff_{0.0};
    double sum_sq_unhedged_pnl_diff_{0.0};
    double sum_sq_hedged_pnl_diff_{0.0};
    uint64_t tick_count_{0};

public:
    explicit HedgeEngine(bool enabled = false, double ratio = 1.0)
        : hedge_ratio_(ratio), enabled_(enabled) {}

    void reset() {
        futures_pos_ = Position{};
        futures_pnl_ = PnLTracker{};
        peak_unhedged_pnl_ = 0.0;
        peak_hedged_pnl_   = 0.0;
        max_dd_unhedged_   = 0.0;
        max_dd_hedged_     = 0.0;
        prev_unhedged_pnl_ = 0.0;
        prev_hedged_pnl_   = 0.0;
        sum_unhedged_pnl_diff_ = 0.0;
        sum_hedged_pnl_diff_   = 0.0;
        sum_sq_unhedged_pnl_diff_ = 0.0;
        sum_sq_hedged_pnl_diff_   = 0.0;
        tick_count_ = 0;
    }

    // Called every time the base position fills
    void rebalance(const Position& base_pos, double current_price) {
        if (!enabled_) return;

        double target_qty = -base_pos.qty * hedge_ratio_;
        double qty_delta = target_qty - futures_pos_.qty;

        if (std::abs(qty_delta) > 1e-9) {
            // Apply fill to reach target qty
            double realized = futures_pos_.apply_fill(qty_delta, current_price);
            futures_pnl_.add_realized(realized);
        }
    }

    // Called every tick to update metrics
    void update_metrics(const PnLTracker& base_pnl, const Position& base_pos, double current_price) {
        double unhedged_unrealized = PnLTracker::unrealized(base_pos.qty, base_pos.avg_price, current_price);
        double unhedged_total = base_pnl.realized + unhedged_unrealized;

        double hedged_total = unhedged_total;

        if (enabled_) {
            double futures_unrealized = PnLTracker::unrealized(futures_pos_.qty, futures_pos_.avg_price, current_price);
            hedged_total += (futures_pnl_.realized + futures_unrealized);
        }

        // Drawdown tracking
        if (unhedged_total > peak_unhedged_pnl_) peak_unhedged_pnl_ = unhedged_total;
        double dd_u = peak_unhedged_pnl_ - unhedged_total;
        if (dd_u > max_dd_unhedged_) max_dd_unhedged_ = dd_u;

        if (hedged_total > peak_hedged_pnl_) peak_hedged_pnl_ = hedged_total;
        double dd_h = peak_hedged_pnl_ - hedged_total;
        if (dd_h > max_dd_hedged_) max_dd_hedged_ = dd_h;

        // Volatility tracking (Welford's approximate or simple sum of squares)
        if (tick_count_ > 0) {
            double diff_u = unhedged_total - prev_unhedged_pnl_;
            double diff_h = hedged_total - prev_hedged_pnl_;

            sum_unhedged_pnl_diff_ += diff_u;
            sum_hedged_pnl_diff_ += diff_h;

            sum_sq_unhedged_pnl_diff_ += diff_u * diff_u;
            sum_sq_hedged_pnl_diff_ += diff_h * diff_h;
        }

        prev_unhedged_pnl_ = unhedged_total;
        prev_hedged_pnl_ = hedged_total;
        tick_count_++;
    }

    double unhedged_pnl() const { return prev_unhedged_pnl_; }
    double hedged_pnl() const { return prev_hedged_pnl_; }

    double unhedged_dd() const { return max_dd_unhedged_; }
    double hedged_dd() const { return max_dd_hedged_; }

    // Simplified Sharpe ratio (mean / stddev of returns/deltas * sqrt(N))
    double unhedged_sharpe() const {
        if (tick_count_ < 2) return 0.0;
        double mean = sum_unhedged_pnl_diff_ / tick_count_;
        double variance = (sum_sq_unhedged_pnl_diff_ / tick_count_) - (mean * mean);
        if (variance <= 0) return 0.0;
        return (mean / std::sqrt(variance)) * std::sqrt(tick_count_); // annualized to tick count
    }

    double hedged_sharpe() const {
        if (!enabled_ || tick_count_ < 2) return 0.0;
        double mean = sum_hedged_pnl_diff_ / tick_count_;
        double variance = (sum_sq_hedged_pnl_diff_ / tick_count_) - (mean * mean);
        if (variance <= 0) return 0.0;
        return (mean / std::sqrt(variance)) * std::sqrt(tick_count_);
    }

    void print() const {
        std::printf("[Hedging]\n");
        if (enabled_) {
            std::printf("  Mode:       ON (Ratio: %.2f)\n", hedge_ratio_);
            std::printf("  FuturesPos: %.6f @ %.2f\n", futures_pos_.qty, futures_pos_.avg_price);
        } else {
            std::printf("  Mode:       OFF\n");
        }
        std::printf("[Performance]\n");
        std::printf("  Unhedged PnL:   $ %8.4f  | Max DD: $ %8.4f | Sharpe: %.2f\n", unhedged_pnl(), unhedged_dd(), unhedged_sharpe());
        if (enabled_) {
            std::printf("  Hedged PnL:     $ %8.4f  | Max DD: $ %8.4f | Sharpe: %.2f\n", hedged_pnl(), hedged_dd(), hedged_sharpe());
        }
    }
};

} // namespace chl
