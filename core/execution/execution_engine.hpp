#pragma once

#include "../common/ring_buffer.hpp"
#include "../common/signal.hpp"
#include "../kdb/kdb_record.hpp"
#include "../latency/latency.hpp"
#include "../ui/ui_state.hpp"
#include "fill.hpp"
#include "hedge_engine.hpp"
#include "pnl_tracker.hpp"
#include "position.hpp"

#include <atomic>
#include <cstdio>
#include <thread>
#include <unordered_map>
#include <string>

namespace chl {

static constexpr double TRADE_QTY = 0.001; // BTC per signal

inline void execution_loop(SPSCRingBuffer<Signal> &signal_queue,
                           UIState &ui_state,
                           SPSCRingBuffer<KdbRecord> &kdb_queue,
                           HedgeEngine &hedge, std::atomic<bool> &running) {
    std::printf("[Exec] Thread started — paper trading engine active\n");
    std::printf("[Exec] Trade size: %.6f BTC per signal\n\n", TRADE_QTY);

    std::unordered_map<std::string, Position> positions;
    std::unordered_map<std::string, PnLTracker> pnl_trackers;
    Signal sig{};
    uint64_t fill_count = 0;

    auto get_strategy_id = [](const std::string& name) -> int {
        if (name == "momentum")           return 0;
        if (name == "funding_arbitrage")  return 1;
        if (name == "pairs_trading")      return 2;
        if (name == "dual_momentum")      return 3;
        if (name == "margin_short")       return 4;
        if (name == "vol_straddle")       return 5;
        if (name == "perp_swap_hedge")    return 6;
        if (name == "inverse_perp_hedge") return 7;
        if (name == "synthetic_put")      return 8;
        return -1;
    };

    while (running.load(std::memory_order_relaxed)) {
        if (!signal_queue.try_pop(sig)) {
            std::this_thread::yield();
            continue;
        }

        std::string s_name = sig.strategy_name;
        auto& position = positions[s_name];
        auto& pnl = pnl_trackers[s_name];

        int s_id = get_strategy_id(s_name);

        if (sig.action == Action::RESET_STATE) {
            position = Position{};
            pnl = PnLTracker{};
            hedge.reset();
            continue;
        }

        // ── Stage 1: Generate Fill ─────────────────────────────────────
        Fill fill{};
        fill.side = sig.action;
        fill.quantity = TRADE_QTY;
        fill.seq = sig.seq;
        fill.fill_ts = now();

        if (sig.action == Action::BUY) {
            fill.price = sig.best_ask;
        } else {
            fill.price = sig.best_bid;
        }

        if (fill.price <= 0.0)
            continue;
        ++fill_count;

        // ── Stage 2: Apply Fill to Position ────────────────────────────
        double signed_qty = (sig.action == Action::BUY) ? TRADE_QTY : -TRADE_QTY;
        double realized = position.apply_fill(signed_qty, fill.price);
        pnl.add_realized(realized);

        // ── Stage 3: Compute Unrealized PnL & Hedging ──────────────────
        double mid_price = (sig.best_bid + sig.best_ask) / 2.0;
        double unrealized =
            PnLTracker::unrealized(position.qty, position.avg_price, mid_price);

        hedge.rebalance(position, fill.price);
        hedge.update_metrics(pnl, position, mid_price);

        // ── Stage 4: Compute Latencies ─────────────────────────────────
        double feed_to_strat = elapsed_us(sig.feed_ts, sig.strategy_ts);
        double strat_to_exec = elapsed_us(sig.strategy_ts, fill.fill_ts);
        double total = elapsed_us(sig.feed_ts, now());

        // ── Stage 5: Push Snapshot to UI Broadcaster ───────────────────
        ui_state.price.store(sig.price, std::memory_order_relaxed);
        ui_state.best_bid.store(sig.best_bid, std::memory_order_relaxed);
        ui_state.best_ask.store(sig.best_ask, std::memory_order_relaxed);
        ui_state.lat_feed_strat.store(feed_to_strat, std::memory_order_relaxed);
        ui_state.lat_strat_exec.store(strat_to_exec, std::memory_order_relaxed);
        ui_state.lat_total.store(total, std::memory_order_relaxed);

        if (s_id >= 0 && s_id < (int)UIState::MAX_STRATEGIES) {
            auto& m = ui_state.strategy_metrics[s_id];
            m.pos_qty.store(position.qty, std::memory_order_relaxed);
            m.pos_avg_price.store(position.avg_price, std::memory_order_relaxed);
            m.pnl_realized.store(pnl.realized, std::memory_order_relaxed);
            m.pnl_unrealized.store(unrealized, std::memory_order_relaxed);
        }

        ui_state.add_trade(s_id, fill.side, fill.price, fill.quantity, fill.seq);

        // ── Stage 6: Push to kdb+ queue ────────────────────────────────
        KdbRecord krec{};
        krec.ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(fill.fill_ts.time_since_epoch()).count());
        krec.type = KdbRecType::FILL;
        krec.data.fill.side = fill.side;
        krec.data.fill.price = fill.price;
        krec.data.fill.qty = fill.quantity;
        kdb_queue.try_push(krec);

        // ── Output ─────────────────────────────────────────────────────
        std::printf("═══════════════════════════════════════════════════\n");
        std::printf("[Signal] [%s] %s @ %.2f  (Tick #%llu)\n", s_name.c_str(), action_str(sig.action),
                    sig.price, static_cast<unsigned long long>(sig.seq));
        fill.print();
        position.print();
        pnl.print(position.qty, position.avg_price, mid_price);
        std::printf("═══════════════════════════════════════════════════\n\n");
    }

    std::printf("[Exec] Thread exiting. Total Fills: %llu\n", static_cast<unsigned long long>(fill_count));
}

} // namespace chl
