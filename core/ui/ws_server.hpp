#pragma once

#include "ui_state.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <iostream>

namespace chl {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

// ─── WebSocket Session ─────────────────────────────────────────────────────
// Represents a single connected UI client.

class WSSession : public std::enable_shared_from_this<WSSession> {
    websocket::stream<tcp::socket> ws_;
    std::string send_buffer_;
    std::function<void(const std::string&)> on_cmd_;

public:
    explicit WSSession(tcp::socket socket, std::function<void(const std::string&)> on_cmd) 
        : ws_(std::move(socket)), on_cmd_(on_cmd) {}

    void start() {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.async_accept(
            beast::bind_front_handler(&WSSession::on_accept, shared_from_this())
        );
    }

    void send_async(const std::string& msg) {
        send_buffer_ = msg;
        ws_.async_write(
            asio::buffer(send_buffer_),
            beast::bind_front_handler(&WSSession::on_write, shared_from_this())
        );
    }

private:
    void on_accept(beast::error_code ec) {
        if (ec) std::cerr << "[UI] Accept error: " << ec.message() << "\n";
        else std::cout << "[UI] Client connected to dashboard.\n";
        do_read();
    }

    void do_read() {
        auto buffer = std::make_shared<beast::flat_buffer>();
        ws_.async_read(
            *buffer,
            [self = shared_from_this(), buffer](beast::error_code ec, std::size_t) {
                if (!ec) {
                    if (self->on_cmd_) {
                        self->on_cmd_(beast::buffers_to_string(buffer->data()));
                    }
                    self->do_read();
                }
            });
    }

    void on_write(beast::error_code /*ec*/, std::size_t /*bytes_transferred*/) {}
};

// ─── WebSocket Broadcaster ─────────────────────────────────────────────────
// Runs inside its own thread, accepting clients and blasting UIState JSON.

class UIBroadcaster {
    UIState& ui_state_;
    asio::io_context ioc_{1};
    tcp::acceptor acceptor_{ioc_};
    asio::steady_timer broadcast_timer_{ioc_};
    
    std::mutex sessions_mutex_;
    std::vector<std::weak_ptr<WSSession>> sessions_;

    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    std::function<void(const std::string&)> on_cmd_;

public:
    explicit UIBroadcaster(UIState& ui_state, uint16_t port = 8080, std::function<void(const std::string&)> on_cmd = nullptr)
        : ui_state_(ui_state),
          acceptor_(ioc_, tcp::endpoint(tcp::v4(), port)),
          on_cmd_(on_cmd)
    {
    }

    ~UIBroadcaster() {
        stop();
    }

    void start() {
        if (running_) return;
        running_ = true;

        std::cout << "[UI] Broadcaster listening on ws://localhost:" 
                  << acceptor_.local_endpoint().port() << "\n";

        do_accept();
        do_broadcast(); // Start the 50ms tick

        worker_thread_ = std::thread([this]() {
            ioc_.run();
        });
    }

    void stop() {
        if (!running_) return;
        running_ = false;
        
        asio::post(ioc_, [this]() {
            broadcast_timer_.cancel();
            acceptor_.close();
        });

        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](beast::error_code ec, tcp::socket socket) {
                if (!ec) {
                    auto session = std::make_shared<WSSession>(std::move(socket), on_cmd_);
                    {
                        std::lock_guard<std::mutex> lock(sessions_mutex_);
                        sessions_.push_back(session);
                    }
                    session->start();
                }
                if (running_) do_accept();
            }
        );
    }

    void do_broadcast() {
        broadcast_timer_.expires_after(std::chrono::milliseconds(50));
        broadcast_timer_.async_wait(
            [this](beast::error_code ec) {
                if (!ec && running_) {
                    broadcast_snapshot();
                    do_broadcast();
                }
            }
        );
    }

    void broadcast_snapshot() {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        if (sessions_.empty()) return;

        sessions_.erase(
            std::remove_if(sessions_.begin(), sessions_.end(),
                [](const std::weak_ptr<WSSession>& wp) { return wp.expired(); }),
            sessions_.end());

        if (sessions_.empty()) return;

        // 1. Market Data
        double p = ui_state_.price.load(std::memory_order_relaxed);
        double b = ui_state_.best_bid.load(std::memory_order_relaxed);
        double a = ui_state_.best_ask.load(std::memory_order_relaxed);
        
        // 2. Latencies
        double l_fs = ui_state_.lat_feed_strat.load(std::memory_order_relaxed);
        double l_se = ui_state_.lat_strat_exec.load(std::memory_order_relaxed);
        double l_tot = ui_state_.lat_total.load(std::memory_order_relaxed);

        // 3. Per-strategy metrics
        const char* names[] = {
            "momentum", "funding_arbitrage", "pairs_trading", "dual_momentum",
            "margin_short", "vol_straddle", "perp_swap_hedge", 
            "inverse_perp_hedge", "trend_vol_filter"
        };
        std::string strats_json = "[";
        for (size_t i = 0; i < UIState::MAX_STRATEGIES; ++i) {
            auto& m = ui_state_.strategy_metrics[i];
            double qty = m.pos_qty.load(std::memory_order_relaxed);
            double avg = m.pos_avg_price.load(std::memory_order_relaxed);
            double pr  = m.pnl_realized.load(std::memory_order_relaxed);
            double pu  = m.pnl_unrealized.load(std::memory_order_relaxed);
            
            char s_buf[256];
            std::snprintf(s_buf, sizeof(s_buf),
                R"({"name":"%s","qty":%.6f,"avg":%.2f,"pnl_r":%.4f,"pnl_u":%.4f})",
                names[i], qty, avg, pr, pu);
            strats_json += s_buf;
            if (i < UIState::MAX_STRATEGIES - 1) strats_json += ",";
        }
        strats_json += "]";

        // 4. Recent Trades
        std::string trades_json = "[";
        size_t write_idx = ui_state_.trade_idx.load(std::memory_order_acquire);
        size_t count = std::min(write_idx, UIState::MAX_UI_TRADES);
        for (size_t i = 0; i < count; ++i) {
            size_t actual_idx = (write_idx - 1 - i) % UIState::MAX_UI_TRADES;
            const auto& t = ui_state_.trades[actual_idx];
            char trade_buf[160];
            std::snprintf(trade_buf, sizeof(trade_buf),
                R"({"side":"%s","price":%.2f,"qty":%.6f,"seq":%llu,"strategy_name":"%s"})",
                action_str(t.side), t.price, t.qty, 
                static_cast<unsigned long long>(t.seq),
                (t.strategy_id < UIState::MAX_STRATEGIES) ? names[t.strategy_id] : "unknown");
            trades_json += trade_buf;
            if (i < count - 1) trades_json += ",";
        }
        trades_json += "]";

        // 5. Consolidated JSON
        char buf[4096];
        std::snprintf(buf, sizeof(buf),
            R"({"price":%.2f,"bid":%.2f,"ask":%.2f,"latency":{"fs":%.1f,"se":%.1f,"tot":%.1f},"strategies":%s,"trades":%s})",
            p, b, a, l_fs, l_se, l_tot, strats_json.c_str(), trades_json.c_str()
        );

        std::string msg(buf);
        for (auto& weak_session : sessions_) {
            if (auto session = weak_session.lock()) {
                session->send_async(msg);
            }
        }
    }
};

} // namespace chl
