#pragma once

#include "../common/ring_buffer.hpp"
#include "../common/signal.hpp"
#include "../latency/latency.hpp"
#include "../ui/ui_state.hpp"
#include "fill.hpp"
#include "position.hpp"
#include "pnl_tracker.hpp"

#include <atomic>
#include <cstdio>
#include <thread>

namespace chl {

static constexpr double TRADE_QTY = 0.001;  // BTC per signal

inline void execution_loop(
    SPSCRingBuffer<Signal>& signal_queue,
    UIState&                ui_state,
    std::atomic<bool>&      running)
{
    std::printf("[Exec] Thread started — paper trading engine active\n");
    std::printf("[Exec] Trade size: %.6f BTC per signal\n\n", TRADE_QTY);

    Position   position{};
    PnLTracker pnl{};
    Signal     sig{};
    uint64_t   fill_count = 0;

    while (running.load(std::memory_order_relaxed)) {
        if (!signal_queue.try_pop(sig)) {
            std::this_thread::yield();
            continue;
        }

        // ── Stage 1: Generate Fill ─────────────────────────────────────
        Fill fill{};
        fill.side     = sig.action;
        fill.quantity = TRADE_QTY;
        fill.seq      = sig.seq;
        fill.fill_ts  = now();

        if (sig.action == Action::BUY) {
            fill.price = sig.best_ask;
        } else {
            fill.price = sig.best_bid;
        }

        if (fill.price <= 0.0) continue;
        ++fill_count;

        // ── Stage 2: Apply Fill to Position ────────────────────────────
        double signed_qty = (sig.action == Action::BUY) ? TRADE_QTY : -TRADE_QTY;
        double realized   = position.apply_fill(signed_qty, fill.price);
        pnl.add_realized(realized);

        // ── Stage 3: Compute Unrealized PnL ────────────────────────────
        double mid_price = (sig.best_bid + sig.best_ask) / 2.0;
        double unrealized = PnLTracker::unrealized(position.qty, position.avg_price, mid_price);

        // ── Stage 4: Compute Latencies ─────────────────────────────────
        double feed_to_strat = elapsed_us(sig.feed_ts, sig.strategy_ts);
        double strat_to_exec = elapsed_us(sig.strategy_ts, fill.fill_ts);
        double exec_to_fill  = elapsed_us(fill.fill_ts, now());
        double total         = elapsed_us(sig.feed_ts, now());

        // ── Stage 5: Push Snapshot to UI Broadcaster ───────────────────
        // Lock-free atomic stores (relaxed semantics since the UI just polls occasionally)
        ui_state.price.store(sig.price, std::memory_order_relaxed);
        ui_state.best_bid.store(sig.best_bid, std::memory_order_relaxed);
        ui_state.best_ask.store(sig.best_ask, std::memory_order_relaxed);

        ui_state.pos_qty.store(position.qty, std::memory_order_relaxed);
        ui_state.pos_avg_price.store(position.avg_price, std::memory_order_relaxed);

        ui_state.pnl_realized.store(pnl.realized, std::memory_order_relaxed);
        ui_state.pnl_unrealized.store(unrealized, std::memory_order_relaxed);

        ui_state.lat_feed_strat.store(feed_to_strat, std::memory_order_relaxed);
        ui_state.lat_strat_exec.store(strat_to_exec, std::memory_order_relaxed);
        ui_state.lat_total.store(total, std::memory_order_relaxed);

        ui_state.add_trade(fill.side, fill.price, fill.quantity, fill.seq);

        // ── Output ─────────────────────────────────────────────────────
        std::printf("═══════════════════════════════════════════════════\n");
        std::printf("[Signal] %s @ %.2f  (Tick #%llu)\n",
                    action_str(sig.action), sig.price,
                    static_cast<unsigned long long>(sig.seq));
        fill.print();
        position.print();
        pnl.print(position.qty, position.avg_price, mid_price);
        std::printf("[Latency]\n");
        std::printf("  Feed → Strategy:  %6.1f µs\n", feed_to_strat);
        std::printf("  Strategy → Exec:  %6.1f µs\n", strat_to_exec);
        std::printf("  Exec → Fill:      %6.1f µs\n", exec_to_fill);
        std::printf("  Total Pipeline:   %6.1f µs\n", total);
        std::printf("═══════════════════════════════════════════════════\n\n");
    }

    // ── Final Summary ──────────────────────────────────────────────────
    std::printf("┌─────────────────────────────────────────────────┐\n");
    std::printf("│           PAPER TRADING SESSION SUMMARY         │\n");
    std::printf("├─────────────────────────────────────────────────┤\n");
    std::printf("│  Total Fills:    %6llu                         │\n",
                static_cast<unsigned long long>(fill_count));
    std::printf("│  Final Position: %+.6f BTC                │\n", position.qty);
    if (!position.is_flat()) {
        std::printf("│  Avg Entry:      %.2f                    │\n", position.avg_price);
    }
    std::printf("│  Realized PnL:   %+.4f USD                 │\n", pnl.realized);
    double final_unreal = PnLTracker::unrealized(position.qty, position.avg_price,
                                                  (position.avg_price > 0.0) ? position.avg_price : 0.0);
    std::printf("│  Unrealized PnL: %+.4f USD                 │\n", final_unreal);
    std::printf("│  Total PnL:      %+.4f USD                 │\n", pnl.realized + final_unreal);
    std::printf("└─────────────────────────────────────────────────┘\n");
    std::printf("[Exec] Thread exiting\n");
}

} // namespace chl
