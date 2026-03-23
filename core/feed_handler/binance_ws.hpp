#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <functional>
#include <string>

namespace chl {

namespace beast     = boost::beast;
namespace websocket = beast::websocket;
namespace net       = boost::asio;
namespace ssl       = net::ssl;
using tcp           = net::ip::tcp;

// ─── SSL Stream Type ───────────────────────────────────────────────────────
// Boost.Beast's ssl_stream wrapper may not exist in all Boost versions.
// Use ssl::stream<beast::tcp_stream> directly, which is the canonical form.
using ssl_stream_t = ssl::stream<beast::tcp_stream>;
using ws_stream_t  = websocket::stream<ssl_stream_t>;

// ─── Binance WebSocket Client ──────────────────────────────────────────────
// Async WebSocket-over-TLS client using Boost.Beast.
// Connects to Binance combined streams endpoint for both trade and depth data.
//
// Hot-path memory characteristics:
//   - flat_buffer is pre-allocated (64 KB reserved at construction)
//   - No per-read heap allocation
//   - Callbacks receive string_view into the buffer — zero copy
//
// Threading:
//   - Not thread-safe. Designed for single-threaded io_context::run().
//   - All async operations complete on the io_context's executor.

class BinanceWebSocket {
public:
    // Callback signature: receives the raw message as a string_view
    using MessageCallback = std::function<void(std::string_view)>;

    BinanceWebSocket(net::io_context& ioc, MessageCallback on_message);
    ~BinanceWebSocket();

    // Non-copyable, non-movable (owns async state)
    BinanceWebSocket(const BinanceWebSocket&) = delete;
    BinanceWebSocket& operator=(const BinanceWebSocket&) = delete;

    // Initiate async connection to Binance
    void connect();

    // Request graceful shutdown
    void close();

private:
    void on_resolve(beast::error_code ec, tcp::resolver::results_type results);
    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep);
    void on_ssl_handshake(beast::error_code ec);
    void on_ws_handshake(beast::error_code ec);
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_close(beast::error_code ec);

    // ─── Configuration ─────────────────────────────────────────────────
    static constexpr const char* HOST   = "data-stream.binance.vision";
    static constexpr const char* PORT   = "9443";
    // Use raw stream endpoint; subscribe to combined streams dynamically
    static constexpr const char* TARGET = "/ws/btcusdt@trade";
    static constexpr std::size_t BUFFER_SIZE = 65536;  // 64 KB pre-allocated

    // ─── Members ───────────────────────────────────────────────────────
    tcp::resolver      resolver_;
    ssl::context       ssl_ctx_;
    ws_stream_t        ws_;
    beast::flat_buffer buffer_;
    MessageCallback    on_message_;
    bool               connected_ = false;
};

} // namespace chl
