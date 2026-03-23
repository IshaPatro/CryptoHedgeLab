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

public:
    explicit WSSession(tcp::socket socket) 
        : ws_(std::move(socket)) {}

    void start() {
        // Accept the websocket handshake
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.async_accept(
            beast::bind_front_handler(&WSSession::on_accept, shared_from_this())
        );
    }

    void send_async(const std::string& msg) {
        // Simple async push (assumes no overlaps for this demo, or just drop if busy)
        // In a rigorous server, you'd queue messages if async_write is already pending.
        // For local UI 20Hz updates, this is mostly fine.
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
        
        // We only care about sending. If the client closes, we rely on write failing.
        do_read(); // Keep reading to detect close
    }

    void do_read() {
        auto buffer = std::make_shared<beast::flat_buffer>();
        ws_.async_read(
            *buffer,
            [self = shared_from_this(), buffer](beast::error_code ec, std::size_t) {
                if (!ec) self->do_read();
            });
    }

    void on_write(beast::error_code /*ec*/, std::size_t /*bytes_transferred*/) {
        // If write fails (e.g. client disconnects), the read loop will also fail
        // and tear down the session eventually.
    }
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

public:
    explicit UIBroadcaster(UIState& ui_state, uint16_t port = 8080)
        : ui_state_(ui_state),
          acceptor_(ioc_, tcp::endpoint(tcp::v4(), port))
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
                    auto session = std::make_shared<WSSession>(std::move(socket));
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
        if (sessions_.empty()) return; // Nothing to do

        // Clean up dead sessions
        sessions_.erase(
            std::remove_if(sessions_.begin(), sessions_.end(),
                [](const std::weak_ptr<WSSession>& wp) { return wp.expired(); }),
            sessions_.end());

        if (sessions_.empty()) return;

        // Take snapshot (atomic relaxed loads)
        double p = ui_state_.price.load(std::memory_order_relaxed);
        double b = ui_state_.best_bid.load(std::memory_order_relaxed);
        double a = ui_state_.best_ask.load(std::memory_order_relaxed);
        
        double qty = ui_state_.pos_qty.load(std::memory_order_relaxed);
        double avg_p = ui_state_.pos_avg_price.load(std::memory_order_relaxed);
        
        double pnl_r = ui_state_.pnl_realized.load(std::memory_order_relaxed);
        double pnl_u = ui_state_.pnl_unrealized.load(std::memory_order_relaxed);
        
        double l_fs = ui_state_.lat_feed_strat.load(std::memory_order_relaxed);
        double l_se = ui_state_.lat_strat_exec.load(std::memory_order_relaxed);
        double l_tot = ui_state_.lat_total.load(std::memory_order_relaxed);

        // Trades
        std::string trades_json = "[";
        size_t write_idx = ui_state_.trade_idx.load(std::memory_order_acquire);
        
        // Get the last min(N, MAX) trades in reverse chronological
        size_t count = std::min(write_idx, UIState::MAX_UI_TRADES);
        for (size_t i = 0; i < count; ++i) {
            size_t actual_idx = (write_idx - 1 - i) % UIState::MAX_UI_TRADES;
            const auto& t = ui_state_.trades[actual_idx];
            
            char trade_buf[128];
            std::snprintf(trade_buf, sizeof(trade_buf),
                R"({"side":"%s", "price":%.2f, "qty":%.6f, "seq":%llu})",
                action_str(t.side), t.price, t.qty, 
                static_cast<unsigned long long>(t.seq));
                
            trades_json += trade_buf;
            if (i < count - 1) trades_json += ",";
        }
        trades_json += "]";

        // Format Big JSON
        // Using snprintf to avoid heavy dependencies like jsoncpp or rapidjson
        char buf[1024];
        std::snprintf(buf, sizeof(buf),
            "{"
            "\"price\":%.2f,"
            "\"bid\":%.2f,"
            "\"ask\":%.2f,"
            "\"position\":{\"qty\":%.6f,\"avg_price\":%.2f},"
            "\"pnl\":{\"realized\":%.4f,\"unrealized\":%.4f},"
            "\"latency\":{\"feed_to_strategy\":%.1f,\"strategy_to_execution\":%.1f,\"end_to_end\":%.1f},"
            "\"trades\":%s"
            "}",
            p, b, a, 
            qty, avg_p, 
            pnl_r, pnl_u, 
            l_fs, l_se, l_tot,
            trades_json.c_str()
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
