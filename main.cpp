// ─────────────────────────────────────────────────────────────────────────────
// CryptoHedgeLab — Module 6: Quant Strategy Lab
// ─────────────────────────────────────────────────────────────────────────────
// CLI:
//   --mode live                              Live WebSocket feed from Binance
//   --mode replay --start N --end N          Replay historical data from kdb+
//   --speed N                                Replay playback speed multiplier
//   --hedge on/off                           Enable/Disable auto-hedging

#include "core/feed_handler/binance_ws.hpp"
#include "core/feed_handler/message_parser.hpp"
#include "core/order_book/order_book.hpp"
#include "core/latency/latency.hpp"
#include "core/common/ring_buffer.hpp"
#include "core/common/tick.hpp"
#include "core/common/signal.hpp"
#include "core/strategy/strategy_engine.hpp"
#include "core/execution/execution_engine.hpp"
#include "core/execution/hedge_engine.hpp"
#include "core/ui/ui_state.hpp"
#include "core/ui/ws_server.hpp"
#include "core/kdb/kdb_writer.hpp"
#include "core/kdb/replay_engine.hpp"
#include "core/quant/strategy_def.hpp"
#include "core/quant/strategy_parser.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <string>
#include <vector>
#include <cstdlib>

// ─── CLI Config ──────────────────────────────────────────────────────────────
struct Config {
    std::string mode        = "live";
    uint64_t    start       = 0;
    uint64_t    end         = std::numeric_limits<uint64_t>::max();
    double      speed       = 0.0;  // 0 = max throughput
    bool        hedge       = false;
};

Config parse_cli(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i+1 < argc) {
            cfg.mode = argv[++i];
        }
        else if (arg == "--start" && i+1 < argc){ cfg.start = std::stoull(argv[++i]); }
        else if (arg == "--end"   && i+1 < argc){ cfg.end   = std::stoull(argv[++i]); }
        else if (arg == "--speed" && i+1 < argc){ cfg.speed = std::stod(argv[++i]);   }
        else if (arg == "--hedge" && i+1 < argc){
            std::string h = argv[++i];
            cfg.hedge = (h == "on" || h == "true" || h == "1");
        }
    }
    return cfg;
}

// ─── Live Pipeline entrypoint ────────────────────────────────────────────────
static int run_live_pipeline(const Config& cfg) {
    std::printf("╔══════════════════════════════════════════════════════════╗\n");
    std::printf("║        CryptoHedgeLab — Core Pipeline v6.0              ║\n");
    std::printf("║  kdb+ | Replay | Hedging | Lock-Free | WebSocket UI     ║\n");
    std::printf("╚══════════════════════════════════════════════════════════╝\n\n");

    // Load all predefined Strategies for hot-swapping
    std::vector<chl::StrategyDef> strats;
    std::vector<std::string> paths = {
        "../strategies/momentum.json",
        "../strategies/funding_arb.json",
        "../strategies/pairs_trading.json",
        "../strategies/dual_momentum.json",
        "../strategies/margin_short.json",
        "../strategies/vol_straddle.json",
        "../strategies/perp_swap_hedge.json",
        "../strategies/inverse_perp_hedge.json",
        "../strategies/synthetic_put.json"
    };
    
    for (const auto& path : paths) {
        try {
            strats.push_back(chl::StrategyParser::parse_file(path));
            std::printf("[Main] Loaded strategy: %s (%s)\n", strats.back().name.c_str(), path.c_str());
        } catch (...) {
            std::fprintf(stderr, "[Main] WARNING: Failed to load %s\n", path.c_str());
        }
    }
    
    if (strats.empty()) {
        std::fprintf(stderr, "[Main] ERROR: No strategies loaded.\n");
        return 1;
    }
    
    std::atomic<int> active_idx{-1};  // -1 = simultaneous execution of all strategies

    std::atomic<bool> running{true};

    // Heap allocate all large ring buffers to avoid 8MB stack limit
    auto tick_queue   = std::make_unique<chl::SPSCRingBuffer<chl::Tick>>();
    auto signal_queue = std::make_unique<chl::SPSCRingBuffer<chl::Signal>>();
    auto kdb_feed_q   = std::make_unique<chl::SPSCRingBuffer<chl::KdbRecord>>();
    auto kdb_exec_q   = std::make_unique<chl::SPSCRingBuffer<chl::KdbRecord>>();

    auto book_btc = std::make_unique<chl::OrderBook>();
    auto book_eth = std::make_unique<chl::OrderBook>();
    uint32_t tick_seq = 0;

    auto ui_state = std::make_unique<chl::UIState>();
    
    auto on_cmd = [&strats, &active_idx](const std::string& cmd_json) {
        auto cmd = chl::detail::extract_string(cmd_json, "cmd");
        if (cmd == "run_live") {
            auto s_name = chl::detail::extract_string(cmd_json, "strategy");
            if (s_name == "all") {
                std::printf("[UI Command] Switching to ALL strategies simultaneous execution\n");
                active_idx.store(-1, std::memory_order_relaxed);
            } else {
                std::printf("[UI Command] Switching live strategy to: %s\n", s_name.c_str());
                for (size_t i = 0; i < strats.size(); ++i) {
                    if (strats[i].name == s_name) {
                        active_idx.store((int)i, std::memory_order_relaxed);
                        break;
                    }
                }
            }
        }
    };
    
    chl::UIBroadcaster ui_server(*ui_state, 8080, on_cmd);
    ui_server.start();

    // Hedge initialized to first strategy
    chl::HedgeEngine hedge(strats[0].hedge_enabled, strats[0].hedge_ratio);

    chl::KdbWriter kdb_writer(*kdb_feed_q, *kdb_exec_q, running);
    std::thread kdb_thread([&]() { kdb_writer.run(); });

    std::thread strategy_thread([&]() {
        chl::strategy_loop(*tick_queue, *signal_queue, running, strats, active_idx, ui_state.get());
    });

    std::thread exec_thread([&]() {
        chl::execution_loop(*signal_queue, *ui_state, *kdb_exec_q, hedge, running);
    });

    boost::asio::io_context ioc;

    auto on_message = [&book_btc, &book_eth, &tick_queue, &kdb_feed_q, &tick_seq](std::string_view msg) {
        auto stream_type = chl::detect_stream_type(msg);
        auto stream_name = chl::extract_stream(msg);
        
        uint32_t symbol_id = 0; // default BTC
        chl::OrderBook* active_book = book_btc.get();
        if (stream_name.find("eth") != std::string_view::npos) {
            symbol_id = 1;
            active_book = book_eth.get();
        }

        if (stream_type == chl::StreamType::TRADE) {
            auto trade = chl::parse_trade(msg);
            if (!trade.valid) return;

            chl::Tick tick{};
            tick.price        = trade.price;
            tick.quantity     = trade.quantity;
            tick.best_bid     = active_book->best_bid_price;
            tick.best_ask     = active_book->best_ask_price;
            tick.exchange_ts  = trade.timestamp_ms;
            tick.feed_ts      = chl::now();
            tick.seq          = ++tick_seq;
            tick.symbol_id    = symbol_id;
            tick.funding_rate = 0.0f;
            tick_queue->try_push(tick);

            // Forward to kdb+
            chl::KdbRecord krec{};
            krec.type = chl::KdbRecType::TRADE;
            krec.ts   = static_cast<uint64_t>(tick.exchange_ts) * 1000000ULL;
            krec.data.trade.price = tick.price;
            krec.data.trade.size  = tick.quantity;
            
            // TODO: kdb stream symbol mapping requires updating schema/writer for multi-asset
            // For now, write everything downstream as usual (we'll fix kdb writer if needed)
            kdb_feed_q->try_push(krec);

        } else if (stream_type == chl::StreamType::DEPTH) {
            auto depth = chl::parse_depth(msg);
            if (!depth.valid) return;
            active_book->update(depth.best_bid.price, depth.best_bid.qty,
                                depth.best_ask.price, depth.best_ask.qty);
                                
        } else if (stream_type == chl::StreamType::FUNDING) {
            float rate = chl::parse_funding_rate(msg);
            chl::Tick tick{};
            tick.price        = 0.0;
            tick.quantity     = 0.0;
            tick.best_bid     = active_book->best_bid_price;
            tick.best_ask     = active_book->best_ask_price;
            tick.feed_ts      = chl::now();
            tick.seq          = ++tick_seq;
            tick.symbol_id    = symbol_id;
            tick.funding_rate = rate;
            
            tick_queue->try_push(tick);
        }
    };

    chl::BinanceWebSocket ws(ioc, on_message);
    chl::ReplayEngine     replay(*tick_queue);

    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
        std::printf("\n[Main] Shutting down...\n");
        running.store(false, std::memory_order_release);
        ui_server.stop();
        if (cfg.mode == "live") ws.close();
        ioc.stop();
    });

    std::printf("[Main] Pipeline starting in MODE: %s\n", cfg.mode.c_str());

    std::thread replay_thread;
    if (cfg.mode == "live") {
        ws.connect();
        ioc.run();
    } else if (cfg.mode == "replay") {
        std::printf("[Main] Starting Historical Replay Engine...\n");
        replay_thread = std::thread([&]() {
            replay.run(cfg.start, cfg.end, cfg.speed);
            running.store(false, std::memory_order_release);
            ioc.stop();
        });
        ioc.run();
    } else {
        std::fprintf(stderr, "[Main] ERROR: Unknown mode '%s'. Use 'live' or 'replay'.\n", cfg.mode.c_str());
        return 1;
    }

    std::printf("[Main] Waiting for worker threads...\n");
    if (replay_thread.joinable())   replay_thread.join();
    if (strategy_thread.joinable()) strategy_thread.join();
    if (exec_thread.joinable())     exec_thread.join();
    if (kdb_thread.joinable())      kdb_thread.join();

    std::printf("[Main] Exited cleanly.\n");
    return 0;
}

// ─── Entry Point ──────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    Config cfg = parse_cli(argc, argv);
    return run_live_pipeline(cfg);
}
