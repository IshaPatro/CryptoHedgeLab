#pragma once

#include "../common/signal.hpp"

#include <atomic>
#include <array>
#include <mutex>
#include <string>

namespace chl {

// ─── UI State Snapshot ─────────────────────────────────────────────────────
// A completely lock-free struct where the Execution thread deposits 
// real-time metrics, and the UI WebSocket thread reads them.
// By using std::atomic, we ensure the UI thread never blocks the hot path.

struct UITrade {
    Action side;
    double price;
    double qty;
    uint64_t seq;
};

struct UIState {
    // ─── Market ───
    std::atomic<double> price{0.0};
    std::atomic<double> best_bid{0.0};
    std::atomic<double> best_ask{0.0};

    // ─── Position ───
    std::atomic<double> pos_qty{0.0};
    std::atomic<double> pos_avg_price{0.0};

    // ─── PnL ─────
    std::atomic<double> pnl_realized{0.0};
    std::atomic<double> pnl_unrealized{0.0};

    // ─── Latency (in microseconds) ───
    std::atomic<double> lat_feed_strat{0.0};
    std::atomic<double> lat_strat_exec{0.0};
    std::atomic<double> lat_total{0.0};

    // ─── Recent Trades (Circular Buffer) ───
    static constexpr size_t MAX_UI_TRADES = 20;
    std::array<UITrade, MAX_UI_TRADES> trades{};
    std::atomic<size_t> trade_idx{0}; // write index

    // The execution thread calls this to record a fill
    void add_trade(Action side, double price, double qty, uint64_t seq) noexcept {
        size_t idx = trade_idx.load(std::memory_order_relaxed) % MAX_UI_TRADES;
        trades[idx] = {side, price, qty, seq};
        trade_idx.store(idx + 1, std::memory_order_release);
    }
};

} // namespace chl
