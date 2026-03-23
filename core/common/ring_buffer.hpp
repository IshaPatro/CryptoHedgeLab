#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <new>       // std::hardware_destructive_interference_size
#include <type_traits>

namespace chl {

// ─── SPSC Lock-Free Ring Buffer ────────────────────────────────────────────
//
// Single-Producer Single-Consumer queue. No mutex, no CAS.
//
// Memory layout:
//   - head_ and tail_ are each on their own cache line (64-byte padded).
//     This prevents false sharing: the producer writes head_ while the
//     consumer writes tail_. Without padding, both atomics would sit on
//     the same 64-byte cache line, causing cross-core invalidation on
//     every push/pop — even though they're logically independent.
//
//   - Capacity must be a power of 2. Modulo is computed as (idx & mask_),
//     which compiles to a single AND instruction instead of a DIV.
//
// Memory ordering:
//   - Producer: store(head_, release) after writing data.
//     "release" guarantees that the data write is visible to any thread
//     that subsequently observes the updated head index.
//   - Consumer: load(head_, acquire) before reading data.
//     "acquire" guarantees that the consumer sees all writes made by the
//     producer before the index was advanced.
//   - This is the minimal ordering that is correct for SPSC. Using
//     seq_cst would add unnecessary memory fences on ARM64.
//
// Capacity:
//   - Default 8192 slots. At ~64 bytes per Tick, that's 512 KB — fits
//     comfortably in L2 cache on Apple Silicon (16 MB shared L2).
//   - The buffer will never dynamically grow. If the consumer falls behind
//     and the queue is full, try_push returns false and the tick is dropped.
//     This is intentional: in HFT, stale data is worse than no data.

// Cache line size — use 64 bytes as a safe default across all platforms
static constexpr std::size_t CACHE_LINE_SIZE = 64;

template <typename T, std::size_t N = 8192>
class SPSCRingBuffer {
    static_assert((N & (N - 1)) == 0, "Capacity must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>,
                  "T must be trivially copyable (POD-like) for lock-free safety");

public:
    SPSCRingBuffer() = default;

    // Non-copyable, non-movable (contains atomics)
    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;

    // ─── Producer (single thread only) ─────────────────────────────────
    // Returns true if the element was enqueued, false if the queue is full.
    bool try_push(const T& item) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = head + 1;

        // Check if full: if next slot equals tail, queue is full
        if (next - tail_.load(std::memory_order_acquire) >= N) {
            return false;  // Queue full — drop the tick
        }

        buffer_[head & mask_] = item;

        // Release: ensure the data write above is visible before head advances
        head_.store(next, std::memory_order_release);
        return true;
    }

    // ─── Consumer (single thread only) ─────────────────────────────────
    // Returns true if an element was dequeued, false if the queue is empty.
    bool try_pop(T& item) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);

        // Check if empty: if tail equals head, nothing to read
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // Queue empty
        }

        item = buffer_[tail & mask_];

        // Release: ensure the data read above completes before tail advances
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // ─── Diagnostics (not for hot path) ────────────────────────────────
    std::size_t size() const noexcept {
        auto h = head_.load(std::memory_order_acquire);
        auto t = tail_.load(std::memory_order_acquire);
        return h - t;
    }

    bool empty() const noexcept { return size() == 0; }
    bool full()  const noexcept { return size() >= N; }

    static constexpr std::size_t capacity() noexcept { return N; }

private:
    static constexpr std::size_t mask_ = N - 1;

    // ─── Data ──────────────────────────────────────────────────────────
    // The buffer itself — allocated once, never resized.
    std::array<T, N> buffer_{};

    // ─── Cache-Line Padded Indices ─────────────────────────────────────
    // Each atomic index is placed on its own cache line to prevent
    // false sharing between the producer thread and consumer thread.

    // Producer writes head_, consumer reads head_
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> head_{0};
    char pad_head_[CACHE_LINE_SIZE - sizeof(std::atomic<std::size_t>)];

    // Consumer writes tail_, producer reads tail_
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> tail_{0};
    char pad_tail_[CACHE_LINE_SIZE - sizeof(std::atomic<std::size_t>)];
};

} // namespace chl
