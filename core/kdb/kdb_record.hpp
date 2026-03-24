#pragma once

#include "../common/signal.hpp"
#include <cstdint>

namespace chl {

enum class KdbRecType : uint8_t {
    TRADE = 0,
    FILL,
    PNL,
    LATENCY
};

// POD struct for lock-free transmission to KdbWriter thread
struct alignas(64) KdbRecord {
    KdbRecType type;
    uint64_t   ts;       // timestamp in ns

    union {
        // TRADE
        struct {
            double price;
            double size;
        } trade;

        // FILL
        struct {
            Action side;
            double price;
            double qty;
        } fill;

        // PNL
        struct {
            double realized;
            double unrealized;
        } pnl;

        // LATENCY
        struct {
            double end_to_end; // µs
        } latency;

    } data;
};

} // namespace chl
