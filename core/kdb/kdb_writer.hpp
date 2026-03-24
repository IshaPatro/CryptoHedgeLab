#pragma once

#include "kdb_record.hpp"
#include "kdb_record.hpp"
#include "../common/ring_buffer.hpp"

extern "C" {
#include "../../kdb/c/k.h"
}

#include <atomic>
#include <thread>
#include <cstdio>
#include <string>
#include <vector>

namespace chl {

// ─── Constants ─────────────────────────────────────────────────────────────
static const char* SYMBOL = "BTCUSDT";

class KdbWriter {
    SPSCRingBuffer<KdbRecord>& feed_q_;
    SPSCRingBuffer<KdbRecord>& exec_q_;
    std::atomic<bool>&         running_;

    int h_{-1}; // kdb IPC handle

    // Batching buffers to reduce syscalls and kdb IPC overhead
    std::vector<KdbRecord> batch_trade_;
    std::vector<KdbRecord> batch_fill_;
    std::vector<KdbRecord> batch_pnl_;
    std::vector<KdbRecord> batch_lat_;

    static constexpr size_t BATCH_SIZE = 1000;

public:
    KdbWriter(SPSCRingBuffer<KdbRecord>& feed_q, 
              SPSCRingBuffer<KdbRecord>& exec_q,
              std::atomic<bool>& running)
        : feed_q_(feed_q), exec_q_(exec_q), running_(running) 
    {
        batch_trade_.reserve(BATCH_SIZE);
        batch_fill_.reserve(BATCH_SIZE);
        batch_pnl_.reserve(BATCH_SIZE);
        batch_lat_.reserve(BATCH_SIZE);
    }

    void run() {
        std::printf("[KdbWriter] Thread started. Connecting to localhost:5001...\n");

        h_ = khpu((S)"localhost", 5001, (S)"");
        if (h_ <= 0) {
            std::printf("[KdbWriter] ERROR: Could not connect to kdb+ on port 5001.\n");
            // We won't exit the loop so we don't crash the pipeline, we just drop.
        } else {
            std::printf("[KdbWriter] Connected to kdb+.\n");
        }

        KdbRecord rec;
        
        while (running_.load(std::memory_order_relaxed)) {
            bool idle = true;

            // Drain Feed Queue (Trades)
            while (feed_q_.try_pop(rec)) {
                idle = false;
                process_record(rec);
            }

            // Drain Exec Queue (Fills, PnL, Latency)
            while (exec_q_.try_pop(rec)) {
                idle = false;
                process_record(rec);
            }

            if (idle) {
                // If queues are empty, flush partial batches and yield
                flush_all();
                std::this_thread::yield();
            } else {
                // Check if any batch is full
                if (batch_trade_.size() >= BATCH_SIZE) flush_trade();
                if (batch_fill_.size() >= BATCH_SIZE) flush_fill();
                if (batch_pnl_.size() >= BATCH_SIZE) flush_pnl();
                if (batch_lat_.size() >= BATCH_SIZE) flush_latency();
            }
        }

        // Final flush on shutdown
        flush_all();

        if (h_ > 0) {
            std::printf("[KdbWriter] Closing kdb+ connection.\n");
            kclose(h_);
            h_ = -1;
        }
        std::printf("[KdbWriter] Thread exiting.\n");
    }

private:
    void process_record(const KdbRecord& rec) {
        if (h_ <= 0) return; // Drop if not connected

        switch (rec.type) {
            case KdbRecType::TRADE:   batch_trade_.push_back(rec); break;
            case KdbRecType::FILL:    batch_fill_.push_back(rec); break;
            case KdbRecType::PNL:     batch_pnl_.push_back(rec); break;
            case KdbRecType::LATENCY: batch_lat_.push_back(rec); break;
        }
    }

    inline long long ks_time(uint64_t epoch_ns) {
        return static_cast<long long>(epoch_ns) - 946684800000000000LL;
    }

    void flush_all() {
        if (batch_trade_.size() > 0) flush_trade();
        if (batch_fill_.size() > 0) flush_fill();
        if (batch_pnl_.size() > 0) flush_pnl();
        if (batch_lat_.size() > 0) flush_latency();
    }

    void flush_trade() {
        if (h_ <= 0 || batch_trade_.empty()) return;
        int n = batch_trade_.size();
        
        // Col 0: time (timestamp list `P`)
        K t = ktn(KP, n);
        // Col 1: sym (symbol list `S`)
        K s = ktn(KS, n);
        // Col 2: price (float list `F`)
        K p = ktn(KF, n);
        // Col 3: size (float list `F`)
        K z = ktn(KF, n);

        for (int i = 0; i < n; ++i) {
            kJ(t)[i] = ks_time(batch_trade_[i].ts);
            kS(s)[i] = ss((S)SYMBOL);
            kF(p)[i] = batch_trade_[i].data.trade.price;
            kF(z)[i] = batch_trade_[i].data.trade.size;
        }

        // kdb list of columns
        K cols = knk(4, t, s, p, z);
        
        // Async IPC call (-h)
        k(-h_, (S)".u.upd", ss((S)"trade"), cols, (K)0);

        batch_trade_.clear();
    }

    void flush_fill() {
        if (h_ <= 0 || batch_fill_.empty()) return;
        int n = batch_fill_.size();
        
        K t = ktn(KP, n);
        K s = ktn(KS, n);
        K side = ktn(KS, n);
        K p = ktn(KF, n);
        K q = ktn(KF, n);

        for (int i = 0; i < n; ++i) {
            kJ(t)[i] = ks_time(batch_fill_[i].ts);
            kS(s)[i] = ss((S)SYMBOL);
            kS(side)[i] = ss((S)(batch_fill_[i].data.fill.side == Action::BUY ? "BUY" : "SELL"));
            kF(p)[i] = batch_fill_[i].data.fill.price;
            kF(q)[i] = batch_fill_[i].data.fill.qty;
        }

        K cols = knk(5, t, s, side, p, q);
        k(-h_, (S)".u.upd", ss((S)"fills"), cols, (K)0);

        batch_fill_.clear();
    }

    void flush_pnl() {
        if (h_ <= 0 || batch_pnl_.empty()) return;
        int n = batch_pnl_.size();
        
        K t = ktn(KP, n);
        K r = ktn(KF, n);
        K u = ktn(KF, n);

        for (int i = 0; i < n; ++i) {
            kJ(t)[i] = ks_time(batch_pnl_[i].ts);
            kF(r)[i] = batch_pnl_[i].data.pnl.realized;
            kF(u)[i] = batch_pnl_[i].data.pnl.unrealized;
        }

        K cols = knk(3, t, r, u);
        k(-h_, (S)".u.upd", ss((S)"pnl"), cols, (K)0);

        batch_pnl_.clear();
    }

    void flush_latency() {
        if (h_ <= 0 || batch_lat_.empty()) return;
        int n = batch_lat_.size();
        
        K t = ktn(KP, n);
        K e = ktn(KF, n);

        for (int i = 0; i < n; ++i) {
            kJ(t)[i] = ks_time(batch_lat_[i].ts);
            kF(e)[i] = batch_lat_[i].data.latency.end_to_end;
        }

        K cols = knk(2, t, e);
        k(-h_, (S)".u.upd", ss((S)"latency"), cols, (K)0);

        batch_lat_.clear();
    }
};

} // namespace chl
