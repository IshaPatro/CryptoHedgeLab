// ─────────────────────────────────────────────────────────────────────────────
// CryptoHedgeLab — Module 1: Feed + Order Book + Latency Core
// ─────────────────────────────────────────────────────────────────────────────
// Entry point. Wires together the WebSocket client, message parser, order book,
// and latency tracker into a single-threaded event loop.
//
// Usage: ./cryptohedgelab_feed
// Exit:  Ctrl+C (SIGINT)
// ─────────────────────────────────────────────────────────────────────────────

#include "core/feed_handler/binance_ws.hpp"
#include "core/feed_handler/message_parser.hpp"
#include "core/order_book/order_book.hpp"
#include "core/latency/latency.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <cstdio>
#include <cstdint>

int main() {
    std::printf("╔══════════════════════════════════════════════════════╗\n");
    std::printf("║         CryptoHedgeLab — Feed Core v1.0             ║\n");
    std::printf("║   BTCUSDT | Trade + Depth | Latency Instrumented    ║\n");
    std::printf("╚══════════════════════════════════════════════════════╝\n\n");

    // ─── Core Objects (stack-allocated, process lifetime) ──────────────
    boost::asio::io_context ioc;
    chl::OrderBook book;
    uint64_t tick_count = 0;

    // ─── Message Handler ──────────────────────────────────────────────
    // This lambda is the hot path. All objects it touches are stack-allocated
    // or pre-allocated. No heap allocation occurs here.
    auto on_message = [&book, &tick_count](std::string_view msg) {
        chl::LatencyTracker lat;

        // ── Stage 1: Receive timestamp (stamped as early as possible) ──
        lat.stamp_receive();

        // ── Detect stream type ──
        auto stream_type = chl::detect_stream_type(msg);

        if (stream_type == chl::StreamType::TRADE) {
            // ── Stage 2: Parse ──
            auto trade = chl::parse_trade(msg);
            lat.stamp_parse();

            if (!trade.valid) return;

            // ── Stage 3: Book update (trade doesn't update book, just stamp) ──
            lat.stamp_book();

            // ── Output ──
            ++tick_count;
            std::printf("─── Tick #%llu ─────────────────────────────────\n", 
                        static_cast<unsigned long long>(tick_count));
            trade.print();
            if (book.is_valid()) {
                book.print();
            }
            lat.print();
            std::printf("\n");

        } else if (stream_type == chl::StreamType::DEPTH) {
            // ── Stage 2: Parse ──
            auto depth = chl::parse_depth(msg);
            lat.stamp_parse();

            if (!depth.valid) return;

            // ── Stage 3: Book update ──
            book.update(depth.best_bid.price, depth.best_bid.qty,
                        depth.best_ask.price, depth.best_ask.qty);
            lat.stamp_book();

            // ── Output ──
            ++tick_count;
            std::printf("─── Tick #%llu (Depth) ─────────────────────────\n",
                        static_cast<unsigned long long>(tick_count));
            book.print();
            lat.print();
            std::printf("\n");
        }
        // StreamType::UNKNOWN messages are silently dropped
    };

    // ─── WebSocket Client ─────────────────────────────────────────────
    chl::BinanceWebSocket ws(ioc, on_message);
    ws.connect();

    // ─── Graceful Shutdown on SIGINT (Ctrl+C) ─────────────────────────
    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&ws, &ioc](auto, auto) {
        std::printf("\n[Main] Shutting down ...\n");
        ws.close();
        ioc.stop();
    });

    // ─── Run Event Loop ───────────────────────────────────────────────
    std::printf("[Main] Starting event loop ...\n\n");
    ioc.run();

    std::printf("[Main] Exited cleanly. Total ticks processed: %llu\n",
                static_cast<unsigned long long>(tick_count));
    return 0;
}
