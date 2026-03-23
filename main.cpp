// ─────────────────────────────────────────────────────────────────────────────
// CryptoHedgeLab — Module 2: Lock-Free Multi-Threaded Pipeline
// ─────────────────────────────────────────────────────────────────────────────
// 3-thread pipeline:
//   Feed Thread   → [SPSC Queue] → Strategy Thread → [SPSC Queue] → Exec Thread
//
// Usage: ./cryptohedgelab_feed
// Exit:  Ctrl+C (SIGINT)
// ─────────────────────────────────────────────────────────────────────────────

#include "core/feed_handler/binance_ws.hpp"
#include "core/feed_handler/message_parser.hpp"
#include "core/order_book/order_book.hpp"
#include "core/latency/latency.hpp"
#include "core/common/ring_buffer.hpp"
#include "core/common/tick.hpp"
#include "core/common/signal.hpp"
#include "core/strategy/strategy_engine.hpp"
#include "core/execution/execution_engine.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <thread>

int main() {
    std::printf("╔══════════════════════════════════════════════════════════╗\n");
    std::printf("║        CryptoHedgeLab — Pipeline Core v2.0              ║\n");
    std::printf("║  BTCUSDT | 3-Thread | Lock-Free | Latency Instrumented  ║\n");
    std::printf("╚══════════════════════════════════════════════════════════╝\n\n");

    // ─── Shared State ─────────────────────────────────────────────────────
    // All allocated here in main's stack frame — process lifetime.
    // No heap allocation after this point in the hot path.

    std::atomic<bool> running{true};

    // SPSC queues connecting the pipeline stages
    // 8192 slots × ~64 bytes per Tick = 512 KB per queue (fits in L2)
    chl::SPSCRingBuffer<chl::Tick>   tick_queue;
    chl::SPSCRingBuffer<chl::Signal> signal_queue;

    // Order book — updated by feed thread, read for tick enrichment
    chl::OrderBook book;
    uint64_t tick_seq = 0;

    // ═══════════════════════════════════════════════════════════════════════
    // Thread 2: Strategy Thread
    // ═══════════════════════════════════════════════════════════════════════
    std::thread strategy_thread([&]() {
        chl::strategy_loop(tick_queue, signal_queue, running);
    });

    // ═══════════════════════════════════════════════════════════════════════
    // Thread 3: Execution Thread
    // ═══════════════════════════════════════════════════════════════════════
    std::thread exec_thread([&]() {
        chl::execution_loop(signal_queue, running);
    });

    // ═══════════════════════════════════════════════════════════════════════
    // Thread 1: Feed Thread (runs on main thread via io_context)
    // ═══════════════════════════════════════════════════════════════════════
    boost::asio::io_context ioc;

    // Callback from WebSocket — this is the hot path on the feed thread.
    // Parses the message, updates the order book, and pushes a Tick
    // into the lock-free queue for the strategy thread.
    auto on_message = [&book, &tick_queue, &tick_seq](std::string_view msg) {

        auto stream_type = chl::detect_stream_type(msg);

        if (stream_type == chl::StreamType::TRADE) {
            auto trade = chl::parse_trade(msg);
            if (!trade.valid) return;

            // Build tick and push into pipeline
            chl::Tick tick{};
            tick.price       = trade.price;
            tick.quantity     = trade.quantity;
            tick.best_bid    = book.best_bid_price;
            tick.best_ask    = book.best_ask_price;
            tick.exchange_ts = trade.timestamp_ms;
            tick.feed_ts     = chl::now();  // Pipeline latency starts here
            tick.seq         = ++tick_seq;

            tick_queue.try_push(tick);

        } else if (stream_type == chl::StreamType::DEPTH) {
            auto depth = chl::parse_depth(msg);
            if (!depth.valid) return;

            // Update local order book (feed thread only — no lock needed)
            book.update(depth.best_bid.price, depth.best_bid.qty,
                        depth.best_ask.price, depth.best_ask.qty);

            // Also push a depth tick so strategy sees book updates
            chl::Tick tick{};
            tick.price       = 0.0;  // No trade price on depth updates
            tick.quantity     = 0.0;
            tick.best_bid    = depth.best_bid.price;
            tick.best_ask    = depth.best_ask.price;
            tick.exchange_ts = 0;
            tick.feed_ts     = chl::now();
            tick.seq         = ++tick_seq;

            tick_queue.try_push(tick);
        }
    };

    // ─── WebSocket Client ─────────────────────────────────────────────────
    chl::BinanceWebSocket ws(ioc, on_message);
    ws.connect();

    // ─── Graceful Shutdown on SIGINT (Ctrl+C) ──────────────────────────────
    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
        std::printf("\n[Main] Shutting down pipeline ...\n");

        // 1. Signal all threads to stop
        running.store(false, std::memory_order_release);

        // 2. Close WebSocket and stop io_context (stops feed thread)
        ws.close();
        ioc.stop();
    });

    // ─── Run Feed Event Loop ──────────────────────────────────────────────
    std::printf("[Main] Pipeline starting ...\n");
    std::printf("[Main]   Feed thread:     main (io_context)\n");
    std::printf("[Main]   Strategy thread: spawned\n");
    std::printf("[Main]   Execution thread: spawned\n");
    std::printf("[Main]   Tick queue:      %zu slots\n", tick_queue.capacity());
    std::printf("[Main]   Signal queue:    %zu slots\n\n", signal_queue.capacity());

    ioc.run();  // Blocks until io_context is stopped

    // ─── Join Worker Threads ──────────────────────────────────────────────
    std::printf("[Main] Waiting for worker threads ...\n");
    if (strategy_thread.joinable()) strategy_thread.join();
    if (exec_thread.joinable())     exec_thread.join();

    std::printf("[Main] Exited cleanly. Total ticks: %llu\n",
                static_cast<unsigned long long>(tick_seq));
    return 0;
}
