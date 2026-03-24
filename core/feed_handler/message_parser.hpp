#pragma once

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace chl {

// ─── Lightweight JSON Field Extractor ──────────────────────────────────────
// Zero-allocation parser for Binance WebSocket messages.
// Uses string_view::find() to locate keys and fast number conversion.
//
// This is NOT a general-purpose JSON parser. It exploits the known,
// stable schema of Binance messages to extract only the fields we need.

// ─── Trade Message ─────────────────────────────────────────────────────────
struct TradeMessage {
    double   price          = 0.0;
    double   quantity       = 0.0;
    int64_t  timestamp_ms   = 0;
    bool     is_buyer_maker = false;
    bool     valid          = false;

    void print() const noexcept {
        std::printf("[Trade]\n");
        std::printf("  Price: %.2f\n", price);
        std::printf("  Qty:   %.6f\n", quantity);
    }
};

// ─── Depth Message ─────────────────────────────────────────────────────────
struct PriceLevel {
    double price = 0.0;
    double qty   = 0.0;
};

struct DepthMessage {
    PriceLevel best_bid;
    PriceLevel best_ask;
    bool       valid = false;
};


// ─── Parser Utilities ──────────────────────────────────────────────────────

namespace detail {

// Extract a quoted string value for a given key.
// E.g., for key "p" in ..."p":"67000.12"... returns "67000.12"
inline std::string_view extract_quoted_value(std::string_view json,
                                              std::string_view key) noexcept {
    char pattern[64];
    int len = std::snprintf(pattern, sizeof(pattern), "\"%.*s\":\"",
                            static_cast<int>(key.size()), key.data());
    if (len <= 0 || len >= static_cast<int>(sizeof(pattern))) return {};

    std::string_view pat(pattern, static_cast<size_t>(len));
    auto pos = json.find(pat);
    if (pos == std::string_view::npos) return {};

    auto start = pos + pat.size();
    auto end = json.find('"', start);
    if (end == std::string_view::npos) return {};

    return json.substr(start, end - start);
}

// Extract an unquoted numeric value for a given key.
// E.g., for key "T" in ..."T":1234567890000,... returns "1234567890000"
inline std::string_view extract_numeric_value(std::string_view json,
                                               std::string_view key) noexcept {
    char pattern[64];
    int len = std::snprintf(pattern, sizeof(pattern), "\"%.*s\":",
                            static_cast<int>(key.size()), key.data());
    if (len <= 0 || len >= static_cast<int>(sizeof(pattern))) return {};

    std::string_view pat(pattern, static_cast<size_t>(len));
    auto pos = json.find(pat);
    if (pos == std::string_view::npos) return {};

    auto start = pos + pat.size();
    while (start < json.size() && json[start] == ' ') ++start;

    auto end = start;
    while (end < json.size() && json[end] != ',' && json[end] != '}' &&
           json[end] != ']' && json[end] != ' ') ++end;

    return json.substr(start, end - start);
}

inline bool extract_bool(std::string_view json, std::string_view key) noexcept {
    auto val = extract_numeric_value(json, key);
    return val == "true";
}

// Apple Clang does not support std::from_chars for floating-point types.
// Use strtod with a null-terminated temporary buffer on the stack instead.
inline double sv_to_double(std::string_view sv) noexcept {
    if (sv.empty()) return 0.0;
    // Use a small stack buffer to null-terminate
    char buf[64];
    auto n = sv.size() < sizeof(buf) - 1 ? sv.size() : sizeof(buf) - 1;
    __builtin_memcpy(buf, sv.data(), n);
    buf[n] = '\0';
    return std::strtod(buf, nullptr);
}

inline int64_t sv_to_int64(std::string_view sv) noexcept {
    if (sv.empty()) return 0;
    int64_t val = 0;
    auto result = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (result.ec != std::errc{}) return 0;
    return val;
}

// Extract the first price-level pair from a JSON array of arrays.
// Input: [["67000.00","0.5"],["66999.00","1.2"],...]
inline PriceLevel extract_first_level(std::string_view array_str) noexcept {
    auto first_bracket = array_str.find('[');
    if (first_bracket == std::string_view::npos) return {};

    auto inner_bracket = array_str.find('[', first_bracket + 1);
    if (inner_bracket == std::string_view::npos) return {};

    auto q1_start = array_str.find('"', inner_bracket + 1);
    if (q1_start == std::string_view::npos) return {};
    q1_start++;
    auto q1_end = array_str.find('"', q1_start);
    if (q1_end == std::string_view::npos) return {};

    auto q2_start = array_str.find('"', q1_end + 1);
    if (q2_start == std::string_view::npos) return {};
    q2_start++;
    auto q2_end = array_str.find('"', q2_start);
    if (q2_end == std::string_view::npos) return {};

    PriceLevel level;
    level.price = sv_to_double(array_str.substr(q1_start, q1_end - q1_start));
    level.qty   = sv_to_double(array_str.substr(q2_start, q2_end - q2_start));
    return level;
}

} // namespace detail


// ─── Stream Type Detection ─────────────────────────────────────────────────
enum class StreamType {
    TRADE,
    DEPTH,
    FUNDING,
    UNKNOWN
};

inline StreamType detect_stream_type(std::string_view msg) noexcept {
    if (msg.find("@trade") != std::string_view::npos) {
        return StreamType::TRADE;
    }
    if (msg.find("@depth") != std::string_view::npos) {
        return StreamType::DEPTH;
    }
    if (msg.find("@markPrice") != std::string_view::npos) {
        return StreamType::FUNDING;
    }
    // Raw stream messages (no wrapper):
    if (msg.find("\"e\":\"trade\"") != std::string_view::npos) {
        return StreamType::TRADE;
    }
    if (msg.find("\"lastUpdateId\"") != std::string_view::npos) {
        return StreamType::DEPTH;
    }
    return StreamType::UNKNOWN;
}

inline std::string_view extract_stream(std::string_view msg) noexcept {
    return detail::extract_quoted_value(msg, "stream");
}


// ─── Parse Functions ───────────────────────────────────────────────────────

inline TradeMessage parse_trade(std::string_view msg) noexcept {
    TradeMessage trade;

    auto data_key = msg.find("\"data\"");
    std::string_view data_section = msg;
    if (data_key != std::string_view::npos) {
        data_section = msg.substr(data_key);
    }

    auto price_sv = detail::extract_quoted_value(data_section, "p");
    auto qty_sv   = detail::extract_quoted_value(data_section, "q");
    auto ts_sv    = detail::extract_numeric_value(data_section, "T");

    trade.price          = detail::sv_to_double(price_sv);
    trade.quantity       = detail::sv_to_double(qty_sv);
    trade.timestamp_ms   = detail::sv_to_int64(ts_sv);
    trade.is_buyer_maker = detail::extract_bool(data_section, "m");
    trade.valid          = (trade.price > 0.0 && trade.quantity > 0.0);

    return trade;
}

inline DepthMessage parse_depth(std::string_view msg) noexcept {
    DepthMessage depth;

    auto data_key = msg.find("\"data\"");
    std::string_view data_section = msg;
    if (data_key != std::string_view::npos) {
        data_section = msg.substr(data_key);
    }

    // Find "bids" array
    auto bids_key = data_section.find("\"bids\"");
    if (bids_key != std::string_view::npos) {
        auto bids_start = data_section.find('[', bids_key);
        if (bids_start != std::string_view::npos) {
            int bracket_depth = 0;
            size_t bids_end = bids_start;
            for (size_t i = bids_start; i < data_section.size(); ++i) {
                if (data_section[i] == '[') bracket_depth++;
                else if (data_section[i] == ']') {
                    bracket_depth--;
                    if (bracket_depth == 0) { bids_end = i + 1; break; }
                }
            }
            auto bids_str = data_section.substr(bids_start, bids_end - bids_start);
            depth.best_bid = detail::extract_first_level(bids_str);
        }
    }

    // Find "asks" array
    auto asks_key = data_section.find("\"asks\"");
    if (asks_key != std::string_view::npos) {
        auto asks_start = data_section.find('[', asks_key);
        if (asks_start != std::string_view::npos) {
            int bracket_depth = 0;
            size_t asks_end = asks_start;
            for (size_t i = asks_start; i < data_section.size(); ++i) {
                if (data_section[i] == '[') bracket_depth++;
                else if (data_section[i] == ']') {
                    bracket_depth--;
                    if (bracket_depth == 0) { asks_end = i + 1; break; }
                }
            }
            auto asks_str = data_section.substr(asks_start, asks_end - asks_start);
            depth.best_ask = detail::extract_first_level(asks_str);
        }
    }

    depth.valid = (depth.best_bid.price > 0.0 && depth.best_ask.price > 0.0);
    return depth;
}

inline float parse_funding_rate(std::string_view msg) noexcept {
    auto data_key = msg.find("\"data\"");
    std::string_view data_section = msg;
    if (data_key != std::string_view::npos) {
        data_section = msg.substr(data_key);
    }
    auto r_sv = detail::extract_quoted_value(data_section, "r");
    return static_cast<float>(detail::sv_to_double(r_sv));
}

} // namespace chl
