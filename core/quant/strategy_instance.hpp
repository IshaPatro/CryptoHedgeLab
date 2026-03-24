#pragma once

#include "strategy_def.hpp"
#include "perf_metrics.hpp"
#include "../common/tick.hpp"
#include "../common/signal.hpp"
#include "../execution/position.hpp"
#include "../execution/pnl_tracker.hpp"
#include "../execution/hedge_engine.hpp"

#include <cstdio>
#include <cmath>
#include <string>

namespace chl {

// ─── StrategyInstance ─────────────────────────────────────────────────────
// All mutable per-strategy state for one strategy running on one stream of
// ticks. Constructed once per backtest run or at live startup.
// The StrategyDef (config) is immutable and shared; the instance owns the
// running state.
//
// Hot-path characteristics per tick:
//   - 1 double comparison (EMA or price delta)
//   - 1 EMA update (multiply + add)
//   - Conditional: position apply_fill + PnL update
//   - Zero heap allocation after construction

class StrategyInstance {
public:
    const StrategyDef& def;

    StrategyInstance(const StrategyDef& d)
        : def(d)
        , ema_(0.0)
        , ema_alpha_(2.0 / (d.ema_period + 1.0))
        , ema_short_(0.0)
        , ema_long_(0.0)
        , prev_price_(0.0)
        , btc_price_(0.0)
        , eth_price_(0.0)
        , funding_rate_(0.0)
        , counter_(0)
        , flat_(true)
        , hedge_(d.hedge_enabled, d.hedge_ratio)
    {
        metrics.name = d.name;
    }

    // ─── on_tick: evaluate strategy logic, update state ─────────────────
    void on_tick(const Tick& tick) {
        if (tick.price > 0.0) {
            if (tick.symbol_id == 0) btc_price_ = tick.price;
            else if (tick.symbol_id == 1) eth_price_ = tick.price;
        }
        if (tick.funding_rate != 0.0f) {
            funding_rate_ = tick.funding_rate;
        }
        
        // Ensure we at least have a base price to evaluate simple rules
        if (btc_price_ <= 0.0) return;

        const double price = btc_price_; // baseline for simpler rules
        const double bid   = tick.best_bid;
        const double ask   = tick.best_ask;
        const double mid   = (bid > 0.0 && ask > 0.0) ? (bid + ask) * 0.5 : price;

        // ── Standard EMA update ────────────
        if (ema_ <= 0.0) ema_ = price;
        else             ema_ = ema_alpha_ * price + (1.0 - ema_alpha_) * ema_;
        
        // ── Dual EMA update (Short 9, Long 50) for momentum ────────────
        double alpha_short = 2.0 / (9.0 + 1.0);
        double alpha_long  = 2.0 / (50.0 + 1.0);
        if (ema_short_ <= 0.0) ema_short_ = price;
        else ema_short_ = alpha_short * price + (1.0 - alpha_short) * ema_short_;
        if (ema_long_ <= 0.0) ema_long_ = price;
        else ema_long_   = alpha_long * price + (1.0 - alpha_long) * ema_long_;

        // ── Evaluate entry/exit condition ──────────────────────────────────────
        Action action = Action::NONE;
        if (flat_) {
            if (check(def.entry_cond)) {
                action = Action::BUY;
            }
        } else {
            if (check(def.exit_cond)) {
                action = Action::SELL;
            }
        }

        // ── Execute fill (paper trade) ────────────────────────────────────
        // Note: For advanced strategies, BUY/SELL semantics map to ENTRY/EXIT of the defined strategy.
        if (action == Action::BUY && flat_) {
            double fill_price = ask > 0.0 ? ask : price;
            double realized = position_.apply_fill(+def.size_btc, fill_price);
            pnl_.add_realized(realized);
            metrics.on_fill(realized);
            flat_ = false;
            hedge_.rebalance(position_, fill_price);

        } else if (action == Action::SELL && !flat_) {
            double fill_price = bid > 0.0 ? bid : price;
            double realized = position_.apply_fill(-def.size_btc, fill_price);
            pnl_.add_realized(realized);
            metrics.on_fill(realized);
            flat_ = true;
            hedge_.rebalance(position_, fill_price);
        }

        // ── Mark-to-market PnL ────────────────────────────────────────────
        double unrealized = PnLTracker::unrealized(position_.qty, position_.avg_price, mid);
        double current_pnl = pnl_.realized + unrealized;

        hedge_.update_metrics(pnl_, position_, mid);
        metrics.hedge_pnl = hedge_.hedged_pnl();
        current_pnl = def.hedge_enabled ? metrics.hedge_pnl : current_pnl;

        metrics.unrealized_pnl = unrealized;
        metrics.on_tick(current_pnl);

        prev_price_ = price;
    }

    const PerfMetrics& result() const { return metrics; }

private:
    double   ema_;
    double   ema_alpha_;
    double   ema_short_;
    double   ema_long_;
    double   prev_price_;
    
    // Multi-Asset State
    double   btc_price_;
    double   eth_price_;
    double   funding_rate_;
    int      counter_;

    bool     flat_;
    Position   position_{};
    PnLTracker pnl_{};
    HedgeEngine hedge_;
    PerfMetrics metrics;

    // ─── Condition evaluator ──────────────────────────────────────────────
    inline bool check(ConditionType cond) {
        switch (cond) {
            case ConditionType::PRICE_UP:        return prev_price_ > 0.0 && btc_price_ > prev_price_;
            case ConditionType::PRICE_DOWN:      return prev_price_ > 0.0 && btc_price_ < prev_price_;
            case ConditionType::PRICE_ABOVE_EMA: return ema_ > 0.0 && btc_price_ > ema_;
            case ConditionType::PRICE_BELOW_EMA: return ema_ > 0.0 && btc_price_ < ema_;
            
            // Delta-Neutral Funding Arbitrage
            case ConditionType::FUNDING_ARB_ENTRY: 
                if (funding_rate_ > def.threshold) counter_++;
                else counter_ = 0;
                return counter_ >= def.counter_max;
            case ConditionType::FUNDING_ARB_EXIT:
                return funding_rate_ < 0.0;
                
            // Statistical Pairs Trading (BTC/ETH)
            case ConditionType::PAIRS_Z_SCORE_ENTRY: {
                if (eth_price_ <= 0.0) return false;
                double ratio = btc_price_ / eth_price_;
                // Simplified z-score concept: distance from mean ratio (mocked as constant 15.0 for demo config)
                double mean_ratio = 15.0; 
                double z_score = (ratio - mean_ratio) / 0.5; // mocked stddev
                return std::abs(z_score) > def.threshold;
            }
            case ConditionType::PAIRS_Z_SCORE_EXIT: {
                if (eth_price_ <= 0.0) return false;
                double ratio = btc_price_ / eth_price_;
                return std::abs(ratio - 15.0) < 0.1;
            }
            
            // Dual-Position Futures Hedge
            case ConditionType::DUAL_MOMENTUM_ENTRY: {
                double short_mom = btc_price_ - ema_short_;
                double long_trend = btc_price_ - ema_long_;
                return short_mom < 0.0 && long_trend > 0.0;
            }
            case ConditionType::DUAL_MOMENTUM_EXIT: {
                double short_mom = btc_price_ - ema_short_;
                return short_mom > 0.0; // reversal
            }
            
            // Auto-Borrow Margin Short
            case ConditionType::MARGIN_SHORT_ENTRY: {
                double mom = btc_price_ - prev_price_;
                return btc_price_ < ema_ && mom < 0.0;
            }
            case ConditionType::MARGIN_SHORT_EXIT: {
                double unrealized = PnLTracker::unrealized(position_.qty, position_.avg_price, btc_price_);
                return unrealized > def.threshold; // Target hit
            }
            
            // Options Volatility Straddle
            case ConditionType::VOL_STRADDLE_ENTRY:
                // fixed counter trigger to simulate vol event drop
                counter_++;
                return counter_ % 1000 == 0;
            case ConditionType::VOL_STRADDLE_EXIT: {
                double unrealized = PnLTracker::unrealized(position_.qty, position_.avg_price, btc_price_);
                return std::abs(unrealized) > def.threshold; 
            }
            
            default: return false;
        }
        return false;
    }
};

} // namespace chl
