#pragma once

#include "../common/ring_buffer.hpp"
#include "../common/tick.hpp"
#include "../common/signal.hpp"
#include "../latency/latency.hpp"
#include "../quant/strategy_def.hpp"
#include "../ui/ui_state.hpp"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>
#include <string>
#include <cmath>

namespace chl {

// ─── Live Strategy State ─────────────────────────────────────────────────────
struct LiveStrategyState {
    const StrategyDef& def;
    double ema = 0.0;
    double ema_short = 0.0;
    double ema_long = 0.0;
    double prev_price = 0.0;
    int    counter = 0;
    bool   flat = true;

    // We track per-strategy position locally in strategy loop to decide entry/exit
    double pos_qty = 0.0;
    double pos_avg_price = 0.0;

    // RSI tracking (Wilder's smoothed)
    double rsi_avg_gain = 0.0;
    double rsi_avg_loss = 0.0;
    double rsi = 50.0;
    int    rsi_warmup = 0;

    // ATR / volatility tracking
    double atr = 0.0;
    double atr_ma = 0.0;  // 200-period MA of ATR
    double vol_spike = 1.0;
    double prev_high = 0.0;
    double prev_low = 0.0;

    // Bollinger Band upper (20-period, 2.5σ)
    double bb_prices[20] = {};
    int    bb_idx = 0;
    int    bb_count = 0;
    double bb_upper = 0.0;

    // Funding rate proxy (smoothed)
    double fr_hr = 0.0;
    double fr_ma = 0.0;

    // EMA50 for trend detection
    double ema50 = 0.0;

    // Synthetic put delta
    double put_delta = 0.0;

    explicit LiveStrategyState(const StrategyDef& d) : def(d) {}
};

inline void strategy_loop(
    SPSCRingBuffer<Tick>&   tick_queue,
    SPSCRingBuffer<Signal>& signal_queue,
    std::atomic<bool>&      running,
    const std::vector<StrategyDef>& strats,
    std::atomic<int>&       active_idx,
    UIState*                ui_state)
{
    std::vector<LiveStrategyState> states;
    for (const auto& s : strats) {
        states.emplace_back(s);
    }

    std::printf("[Strategy] Simultaneous Execution Engine started (%zu strategies)\n", states.size());

    double btc_price_ = 0.0;
    double eth_price_ = 0.0;
    double funding_rate_ = 0.0;

    auto check_cond = [&](ConditionType cond, LiveStrategyState& s) {
        const auto& def = s.def;
        double price = btc_price_;
        switch (cond) {
            case ConditionType::PRICE_UP:        return s.prev_price > 0.0 && price > s.prev_price;
            case ConditionType::PRICE_DOWN:      return s.prev_price > 0.0 && price < s.prev_price;
            case ConditionType::PRICE_ABOVE_EMA: return s.ema > 0.0 && price > s.ema;
            case ConditionType::PRICE_BELOW_EMA: return s.ema > 0.0 && price < s.ema;
            
            case ConditionType::FUNDING_ARB_ENTRY: 
                if (funding_rate_ > def.threshold) s.counter++;
                else s.counter = 0;
                return s.counter >= def.counter_max;
            case ConditionType::FUNDING_ARB_EXIT:
                return funding_rate_ < 0.0;
                
            case ConditionType::PAIRS_Z_SCORE_ENTRY: {
                if (eth_price_ <= 0.0) return false;
                double ratio = btc_price_ / eth_price_;
                double z_score = (ratio - 15.0) / 0.5;
                return std::abs(z_score) > def.threshold;
            }
            case ConditionType::PAIRS_Z_SCORE_EXIT: {
                if (eth_price_ <= 0.0) return false;
                double ratio = btc_price_ / eth_price_;
                return std::abs(ratio - 15.0) < 0.1;
            }
            
            case ConditionType::DUAL_MOMENTUM_ENTRY: {
                double s_mom = price - s.ema_short;
                double l_trend = price - s.ema_long;
                return s_mom < 0.0 && l_trend > 0.0;
            }
            case ConditionType::DUAL_MOMENTUM_EXIT: {
                return (price - s.ema_short) > 0.0;
            }

            case ConditionType::MARGIN_SHORT_ENTRY: {
                double mom = price - s.prev_price;
                return price < s.ema && mom < 0.0;
            }
            case ConditionType::MARGIN_SHORT_EXIT: {
                double mid = price;
                double unrealized = s.pos_qty != 0.0 ? (s.pos_qty > 0 ? (mid - s.pos_avg_price)*s.pos_qty : (s.pos_avg_price - mid)*-s.pos_qty) : 0;
                return unrealized > def.threshold;
            }
            
            case ConditionType::VOL_STRADDLE_ENTRY:
                s.counter++;
                return s.counter % 1000 == 0;
            case ConditionType::VOL_STRADDLE_EXIT: {
                double mid = price;
                double unrealized = s.pos_qty != 0.0 ? (s.pos_qty > 0 ? (mid - s.pos_avg_price)*s.pos_qty : (s.pos_avg_price - mid)*-s.pos_qty) : 0;
                return std::abs(unrealized) > def.threshold; 
            }

            // ── Perp Swap Hedge: delta-neutral funding rate harvest ──
            case ConditionType::PERP_SWAP_ENTRY:
                // Enter when smoothed funding rate > 15%/yr annualised AND uptrend
                return (s.fr_ma > (0.15 / 8760.0)) && (price > s.ema50) && (s.ema50 > 0.0);
            case ConditionType::PERP_SWAP_EXIT:
                // Exit when funding dries up or trend breaks
                return (s.fr_ma < (0.05 / 8760.0)) || (price < s.ema50 * 0.97);

            // ── Inverse Perp Hedge: overbought delta reduction ──
            case ConditionType::INVERSE_PERP_ENTRY:
                // Enter hedge when RSI > 70 + price near BB upper + bull regime
                return (s.rsi > 70.0) && (s.bb_upper > 0.0) && (price > s.bb_upper * 0.995) && (price > s.ema);
            case ConditionType::INVERSE_PERP_EXIT:
                // Close hedge when RSI cools or price drops below EMA50
                return (s.rsi < 50.0) || (price < s.ema50);

            // ── Synthetic Put: dynamic delta hedge ──
            case ConditionType::SYNTHETIC_PUT_ENTRY:
                // Heavy protection engaged: put_delta > 0.10
                return s.put_delta > 0.10;
            case ConditionType::SYNTHETIC_PUT_EXIT:
                // Protection removed: put_delta normalised
                return s.put_delta < 0.05;
            
            default: return false;
        }
        return false;
    };

    Tick tick{};

    while (running.load(std::memory_order_relaxed)) {
        if (!tick_queue.try_pop(tick)) {
            std::this_thread::yield();
            continue;
        }

        if (tick.price > 0.0) {
            if (tick.symbol_id == 0) btc_price_ = tick.price;
            else if (tick.symbol_id == 1) eth_price_ = tick.price;
        }
        if (tick.funding_rate != 0.0f) {
            funding_rate_ = tick.funding_rate;
        }

        if (btc_price_ <= 0.0) continue;

        int current_active_idx = active_idx.load(std::memory_order_relaxed);

        for (size_t i = 0; i < states.size(); ++i) {
            // Simultaneous mode: current_active_idx == -1
            if (current_active_idx != -1 && (int)i != current_active_idx) {
                continue;
            }

            auto& s = states[i];
            const auto& def = s.def;
            double price = btc_price_;

            // EMA updates
            double ema_alpha = 2.0 / (def.ema_period + 1.0);
            if (s.ema <= 0.0) s.ema = price;
            else              s.ema = ema_alpha * price + (1.0 - ema_alpha) * s.ema;
            
            double alpha_short = 2.0 / (9.0 + 1.0);
            double alpha_long  = 2.0 / (50.0 + 1.0);
            if (s.ema_short <= 0.0) s.ema_short = price;
            else s.ema_short = alpha_short * price + (1.0 - alpha_short) * s.ema_short;
            if (s.ema_long <= 0.0) s.ema_long = price;
            else s.ema_long = alpha_long * price + (1.0 - alpha_long) * s.ema_long;

            // EMA50 for trend detection
            double alpha50 = 2.0 / (50.0 + 1.0);
            if (s.ema50 <= 0.0) s.ema50 = price;
            else s.ema50 = alpha50 * price + (1.0 - alpha50) * s.ema50;

            // RSI update (Wilder's smoothed, 14-period)
            if (s.prev_price > 0.0) {
                double delta = price - s.prev_price;
                double gain = delta > 0.0 ? delta : 0.0;
                double loss = delta < 0.0 ? -delta : 0.0;
                if (s.rsi_warmup < 14) {
                    s.rsi_avg_gain += gain;
                    s.rsi_avg_loss += loss;
                    s.rsi_warmup++;
                    if (s.rsi_warmup == 14) {
                        s.rsi_avg_gain /= 14.0;
                        s.rsi_avg_loss /= 14.0;
                    }
                } else {
                    s.rsi_avg_gain = (s.rsi_avg_gain * 13.0 + gain) / 14.0;
                    s.rsi_avg_loss = (s.rsi_avg_loss * 13.0 + loss) / 14.0;
                }
                if (s.rsi_avg_loss > 0.0)
                    s.rsi = 100.0 - (100.0 / (1.0 + s.rsi_avg_gain / s.rsi_avg_loss));
                else
                    s.rsi = 100.0;
            }

            // ATR update (14-period EWM approximation)
            double high_est = std::max(price, s.prev_price > 0 ? s.prev_price : price);
            double low_est  = std::min(price, s.prev_price > 0 ? s.prev_price : price);
            double tr = high_est - low_est;
            double atr_alpha = 2.0 / (14.0 + 1.0);
            if (s.atr <= 0.0) s.atr = tr;
            else s.atr = atr_alpha * tr + (1.0 - atr_alpha) * s.atr;
            // ATR moving average (200-period)
            double atr_ma_alpha = 2.0 / (200.0 + 1.0);
            if (s.atr_ma <= 0.0) s.atr_ma = s.atr;
            else s.atr_ma = atr_ma_alpha * s.atr + (1.0 - atr_ma_alpha) * s.atr_ma;
            s.vol_spike = (s.atr_ma > 0.0) ? s.atr / s.atr_ma : 1.0;

            // Bollinger Band upper (20-period, 2.5σ)
            s.bb_prices[s.bb_idx % 20] = price;
            s.bb_idx++;
            if (s.bb_count < 20) s.bb_count++;
            if (s.bb_count >= 20) {
                double sum = 0.0, sum2 = 0.0;
                for (int j = 0; j < 20; ++j) { sum += s.bb_prices[j]; sum2 += s.bb_prices[j] * s.bb_prices[j]; }
                double mean = sum / 20.0;
                double stddev = std::sqrt(sum2 / 20.0 - mean * mean);
                s.bb_upper = mean + 2.5 * stddev;
            }

            // Funding rate proxy from RSI (matches notebook model)
            // ann_fr = 0.50 * max((RSI/50)² - 1, 0)
            double rsi_ratio = s.rsi / 50.0;
            double ann_fr = 0.50 * std::max(rsi_ratio * rsi_ratio - 1.0, 0.0);
            s.fr_hr = ann_fr / 8760.0;
            double fr_alpha = 2.0 / (72.0 + 1.0); // 3-day (72h) smoothing
            s.fr_ma = fr_alpha * s.fr_hr + (1.0 - fr_alpha) * s.fr_ma;

            // Synthetic put delta: clip((RSI-50)/50, 0, 1) * clip(vol_spike-1, 0, 2) * 0.5
            double rsi_factor = std::max(std::min((s.rsi - 50.0) / 50.0, 1.0), 0.0);
            double vol_factor = std::max(std::min(s.vol_spike - 1.0, 2.0), 0.0);
            s.put_delta = rsi_factor * vol_factor * 0.5;

            Action action = Action::NONE;
            if (s.flat) {
                if (check_cond(def.entry_cond, s)) action = Action::BUY;
            } else {
                if (check_cond(def.exit_cond, s))  action = Action::SELL;
            }

            if (action != Action::NONE) {
                s.flat = !s.flat;
                if (action == Action::BUY) {
                    s.pos_qty = def.size_btc;
                    s.pos_avg_price = price;
                } else {
                    s.pos_qty = 0.0;
                    s.pos_avg_price = 0.0;
                }
                
                Signal sig{};
                sig.action = action;
                sig.price  = price;
                sig.best_bid = tick.best_bid;
                sig.best_ask = tick.best_ask;
                sig.feed_ts  = tick.feed_ts;
                sig.strategy_ts = now();
                sig.seq = tick.seq;
                std::snprintf(sig.strategy_name, sizeof(sig.strategy_name), "%s", def.name.c_str());

                signal_queue.try_push(sig);
            }
            s.prev_price = price;
        }

        if (ui_state) {
            ui_state->price.store(btc_price_, std::memory_order_relaxed);
            ui_state->best_bid.store(tick.best_bid, std::memory_order_relaxed);
            ui_state->best_ask.store(tick.best_ask, std::memory_order_relaxed);
            ui_state->lat_feed_strat.store(elapsed_us(tick.feed_ts, now()), std::memory_order_relaxed);
        }
    }

    std::printf("[Strategy] Thread exiting\n");
}

} // namespace chl
