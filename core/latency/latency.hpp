#pragma once

#include <chrono>
#include <cstdio>

namespace chl {

// ─── High-Resolution Timestamp Utility ─────────────────────────────────────
// Uses steady_clock (monotonic) to avoid NTP drift artifacts.
// On Apple Silicon, steady_clock::now() compiles to a single MRS instruction
// reading the hardware counter — ~20-25 ns overhead per call.

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

inline TimePoint now() noexcept {
    return Clock::now();
}

inline double elapsed_us(TimePoint start, TimePoint end) noexcept {
    return std::chrono::duration<double, std::micro>(end - start).count();
}

// ─── Per-Tick Latency Tracker ──────────────────────────────────────────────
// Three timestamps per message, all stack-allocated (24 bytes total).
// Stamped inline at each pipeline stage for minimal overhead.

struct LatencyTracker {
    TimePoint receive_ts;   // Stamped immediately when async read completes
    TimePoint parse_ts;     // Stamped after field extraction
    TimePoint book_ts;      // Stamped after order book update

    void stamp_receive() noexcept { receive_ts = now(); }
    void stamp_parse()   noexcept { parse_ts   = now(); }
    void stamp_book()    noexcept { book_ts    = now(); }

    void print() const noexcept {
        double recv_to_parse = elapsed_us(receive_ts, parse_ts);
        double parse_to_book = elapsed_us(parse_ts, book_ts);
        double total         = elapsed_us(receive_ts, book_ts);

        std::printf("[Latency]\n");
        std::printf("  Receive → Parse: %.1f µs\n", recv_to_parse);
        std::printf("  Parse → Book:    %.1f µs\n", parse_to_book);
        std::printf("  Total:           %.1f µs\n", total);
    }
};

} // namespace chl
