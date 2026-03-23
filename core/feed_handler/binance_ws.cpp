#include "binance_ws.hpp"
#include <cstdio>

namespace chl {

BinanceWebSocket::BinanceWebSocket(net::io_context& ioc, MessageCallback on_message)
    : resolver_(ioc)
    , ssl_ctx_(ssl::context::tlsv12_client)
    , ws_(ioc, ssl_ctx_)
    , on_message_(std::move(on_message))
{
    // Pre-allocate read buffer to avoid hot-path malloc
    buffer_.reserve(BUFFER_SIZE);

    // Use default SSL verification paths
    ssl_ctx_.set_default_verify_paths();
}

BinanceWebSocket::~BinanceWebSocket() {
    if (connected_) {
        beast::error_code ec;
        ws_.close(websocket::close_code::normal, ec);
    }
}

void BinanceWebSocket::connect() {
    std::printf("[Feed] Resolving %s:%s ...\n", HOST, PORT);
    resolver_.async_resolve(
        HOST, PORT,
        [this](beast::error_code ec, tcp::resolver::results_type results) {
            on_resolve(ec, results);
        }
    );
}

void BinanceWebSocket::close() {
    if (!connected_ || !ws_.is_open()) return;
    ws_.async_close(
        websocket::close_code::normal,
        [this](beast::error_code ec) { on_close(ec); }
    );
}

// ─── Async Chain ───────────────────────────────────────────────────────────

void BinanceWebSocket::on_resolve(beast::error_code ec,
                                   tcp::resolver::results_type results) {
    if (ec) {
        std::fprintf(stderr, "[Feed] Resolve failed: %s\n", ec.message().c_str());
        return;
    }
    std::printf("[Feed] Resolved. Connecting ...\n");

    beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(10));

    beast::get_lowest_layer(ws_).async_connect(
        results,
        [this](beast::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
            on_connect(ec, ep);
        }
    );
}

void BinanceWebSocket::on_connect(beast::error_code ec,
                                   tcp::resolver::results_type::endpoint_type /*ep*/) {
    if (ec) {
        std::fprintf(stderr, "[Feed] Connect failed: %s\n", ec.message().c_str());
        return;
    }
    std::printf("[Feed] TCP connected. TLS handshake ...\n");

    beast::get_lowest_layer(ws_).expires_never();

    // Set SNI hostname (required by Binance)
    if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), HOST)) {
        ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                               net::error::get_ssl_category());
        std::fprintf(stderr, "[Feed] SNI failed: %s\n", ec.message().c_str());
        return;
    }

    ws_.next_layer().async_handshake(
        ssl::stream_base::client,
        [this](beast::error_code ec) { on_ssl_handshake(ec); }
    );
}

void BinanceWebSocket::on_ssl_handshake(beast::error_code ec) {
    if (ec) {
        std::fprintf(stderr, "[Feed] TLS handshake failed: %s\n", ec.message().c_str());
        return;
    }
    std::printf("[Feed] TLS established. WebSocket upgrade ...\n");

    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req) {
            req.set(boost::beast::http::field::user_agent, "CryptoHedgeLab/1.0");
        }
    ));

    // Binance requires Host header without port for WSS (443/9443)
    ws_.async_handshake(HOST, TARGET,
        [this](beast::error_code ec) { on_ws_handshake(ec); }
    );
}

void BinanceWebSocket::on_ws_handshake(beast::error_code ec) {
    if (ec) {
        std::fprintf(stderr, "[Feed] WebSocket handshake failed: %s\n", ec.message().c_str());
        return;
    }

    connected_ = true;
    std::printf("[Feed] ✓ Connected to Binance WebSocket\n");

    // Subscribe to additional depth stream via Binance JSON API
    std::string subscribe_msg = R"({
        "method": "SUBSCRIBE",
        "params": ["btcusdt@depth5@100ms"],
        "id": 1
    })";

    ws_.async_write(
        net::buffer(subscribe_msg),
        [this](beast::error_code ec, std::size_t /*bytes*/) {
            if (ec) {
                std::fprintf(stderr, "[Feed] Subscribe write failed: %s\n", ec.message().c_str());
                return;
            }
            std::printf("[Feed] Subscribed: btcusdt@trade + btcusdt@depth5@100ms\n");
            std::printf("[Feed] Listening for messages ...\n\n");
            do_read();
        }
    );
}

void BinanceWebSocket::do_read() {
    buffer_.consume(buffer_.size());

    ws_.async_read(
        buffer_,
        [this](beast::error_code ec, std::size_t bytes_transferred) {
            on_read(ec, bytes_transferred);
        }
    );
}

void BinanceWebSocket::on_read(beast::error_code ec, std::size_t /*bytes_transferred*/) {
    if (ec) {
        if (ec == websocket::error::closed) {
            std::printf("[Feed] Connection closed cleanly.\n");
        } else {
            std::fprintf(stderr, "[Feed] Read error: %s\n", ec.message().c_str());
        }
        connected_ = false;
        return;
    }

    // Create a string_view into the buffer — zero copy
    auto data = buffer_.data();
    std::string_view msg(
        static_cast<const char*>(data.data()),
        data.size()
    );

    // Dispatch to callback
    if (on_message_) {
        on_message_(msg);
    }

    // Continue reading
    do_read();
}

void BinanceWebSocket::on_close(beast::error_code ec) {
    connected_ = false;
    if (ec) {
        std::fprintf(stderr, "[Feed] Close error: %s\n", ec.message().c_str());
    } else {
        std::printf("[Feed] Connection closed.\n");
    }
}

} // namespace chl
