# Developer Diary: CryptoHedgeLab — Module 1

*A chronological log of building the high-performance Binance WebSocket feed handler.*

---

## Entry 1: Laying the Foundation (Latency & Memory First)

**The Goal:** Build the core structs that will sit in the absolute hottest part of the execution path. Before touching the network, I needed to know exactly how data would be stored and measured. 

**What I built:**
1. `core/latency/latency.hpp`: A high-resolution tracker. I used `std::chrono::steady_clock` instead of `system_clock` because it maps directly to the CPU's hardware cycle counter (via `mrs` instructions on Apple Silicon). This avoids negative latency jumps if the OS synchronizes the clock via NTP. The entire `LatencyTracker` is just 24 bytes on the stack.
2. `core/order_book/order_book.hpp`: A minimal Top-of-Book struct. I deliberately avoided `std::map` or any heap-allocated trees. The `OrderBook` is a pure C++ struct with 4 `double` fields (32 bytes). Why? Because 32 bytes fits exactly into half a CPU cache line. When the strategy engine eventually reads this book, it will never miss the L1 cache.

---

## Entry 2: The Parsing Dilemma

**The Goal:** Extract price, volume, and time from Binance JSON strings without allocating memory.

**The Implementation:**
I initially considered using `simdjson`, the fastest JSON parser available. However, `simdjson` builds a memory "tape" of the JSON structure. Since Binance's `depth5` messages are tiny (~200 bytes) and we only need ~3 fields, building a tape was wasted work.

I wrote a custom `message_parser.hpp` that uses zero-allocation string slicing (`std::string_view::find`). It scans for static keys like `"p":` and `"q":` and extracts the numbers directly.

**Error encountered: `std::from_chars` failure**
I tried using C++17's `std::from_chars` for ultra-fast, locale-independent floating-point parsing. The build failed spectacularly. It turns out Apple's Clang compiler does not fully support `std::from_chars` for `double`.
**Resolution:** I fell back to using `std::strtod`. But since `strtod` requires a null-terminated string (and `string_view` isn't), I added a tiny 64-byte stack buffer, explicitly copied the characters into it, null-terminated it, and passed it to `strtod`. Still zero heap allocation, just a fast stack copy.

---

## Entry 3: Wiring the Network (Boost.Beast)

**The Goal:** Maintain a persistent, asynchronous WebSocket connection using OpenSSL and Boost.Asio.

**The Implementation:** 
I built `binance_ws.hpp` and `binance_ws.cpp`. I chose Boost.Beast because it gives absolute control over the input buffers. I initialized a `beast::flat_buffer` and explicitly reserved 64 KB of memory in the constructor. When a packet arrives, Boost writes directly into this pre-allocated block — no `malloc` is ever called during trading hours.

**Error encountered: Missing `ssl_stream`**
The first compile with CMake threw a template error: `no member named 'ssl_stream' in namespace 'boost::beast'`. 
**Resolution:** The project picked up Boost version 1.82.0 from an Anaconda path, which uses a slightly different type alias structure than the latest Boost docs. I fixed it by explicitly falling back to `boost::asio::ssl::stream<boost::beast::tcp_stream>` instead of the newer `beast::ssl_stream` wrapper.

---

## Entry 4: The Binance Geographic Block

**The Goal:** Connect to the `stream.binance.com:9443` combined streams endpoint.

**The Implementation:**
I fired up the async connection chain: DNS resolve → TCP connect → TLS handshake → WebSocket upgrade. The TLS handshake succeeded, but the WebSocket upgrade failed with: `WebSocket handshake was declined by the remote peer`.

**The Investigation & Errors:**
1. **Host Header:** I suspected Binance was rejecting the `Host:` header because it included the `:9443` port. I stripped the port out of the header. *Still rejected.*
2. **Endpoint Layout:** I suspected Boost.Beast was mangling the complex `/stream?streams=...` URL. I switched the connection target to the simplest raw stream: `/ws/btcusdt@trade`. *Still rejected.*
3. **The Root Cause:** I dropped into a quick Python script to debug the raw HTTP response headers. The server was returning **HTTP 451: Unavailable For Legal Reasons**. My IP was geoblocked by Binance's primary servers!

**The Resolution:**
I switched the target hostname to `data-stream.binance.vision`. This is Binance's alternative, data-only endpoint explicitly designed to bypass regional trading restrictions for developers who just want to consume market data. It connected instantly.

---

## Entry 5: The Dance of Dynamic Subscriptions

**The Goal:** Subscribe to both the trade stream AND the depth stream over a single connection to avoid multi-threading.

**The Implementation:**
Because I couldn't reliably format the URL path for combined streams on the alternative endpoint, I used a dynamic approach:
1. Connect via WebSocket to the core `/ws/btcusdt@trade` endpoint.
2. The moment `on_ws_handshake` succeeds, send an async JSON message back to Binance: 
   `{"method": "SUBSCRIBE", "params": ["btcusdt@depth5@100ms"], "id": 1}`.
3. Update my `message_parser` stream detection to identify both raw trade messages (`"e":"trade"`) and raw depth messages (`"lastUpdateId"`).

---

## Entry 6: Tying it All Together (The Flow)

**The Implementation:**
The `main.cpp` orchestrates the flow. Here is exactly what happens on every tick:

1. **Wait State:** `io_context.run()` is asleep, polling the OS kernel (epoll/kqueue) for network events.
2. **Data Arrives:** Boost wakes up, reads the TLS bytes, and decrypts them into the pre-allocated 64KB `flat_buffer`.
3. **`on_message` Callback fires:** 
   - A `LatencyTracker` is instantiated on the stack. `stamp_receive()` is called (reading the CPU cycle counter).
   - `detect_stream_type()` scans the first ~30 bytes to see if it's a trade or depth update.
   - The parser (`parse_trade` or `parse_depth`) slices out the prices and volumes directly from the read buffer into stack variables.
   - `stamp_parse()` is called.
   - The 32-byte `OrderBook` struct is updated via 4 direct memory assignments.
   - `stamp_book()` is called.
   - A summary is printed to the console containing the end-to-end latency.

All of this happens in a single thread, with no mutexes, no lock-free queues, and no memory allocations. The result? **~0.2 to 0.8 microseconds** for parsing and book updates. Module 1 is ready.

---
---

# Developer Diary: CryptoHedgeLab — Module 2

*Upgrading from a single-threaded event loop to a 3-thread lock-free pipeline.*

---

## Entry 7: The SPSC Ring Buffer

**The Goal:** Build a lock-free queue that connects the feed thread to a strategy thread without any mutex, any `std::queue`, or any dynamic allocation.

**The Implementation:**
I implemented `core/common/ring_buffer.hpp` — a Single-Producer Single-Consumer ring buffer. Here's why every line matters:

1. **Power-of-2 capacity (8192).** Modulo is the most expensive step in a ring buffer. With a power-of-2 size, `idx % N` becomes `idx & (N-1)` — a single AND instruction. Division would be ~20 cycles on ARM64.

2. **Cache-line padded atomics.** The `head_` index (written by producer, read by consumer) and `tail_` index (written by consumer, read by producer) are each placed on their own 64-byte cache line using `alignas(64)`. Without this padding, both atomics would share a cache line, and every push on core 0 would invalidate the cache on core 1 — even though they're writing to different variables. This is called **false sharing**, and it can degrade throughput by 10-50×.

3. **Acquire/release semantics.** I used `memory_order_release` on the store side (after writing data) and `memory_order_acquire` on the load side (before reading data). This is the weakest ordering that is correct for SPSC: it guarantees the data is visible before the index advances. Using `seq_cst` would add unnecessary memory fences on ARM64, costing ~10-20 ns per operation.

4. **No CAS (Compare-And-Swap).** Because there is exactly one producer and one consumer, there is no contention. A plain `load` + `store` is sufficient. CAS loops are only needed for MPMC queues.

5. **Drop policy.** If the queue is full, `try_push` returns `false` and the tick is dropped. In HFT, stale data is worse than no data. Blocking the feed thread to wait for a slow consumer would introduce unbounded latency jitter.

---

## Entry 8: The Data Highway (Tick and Signal Structs)

**The Goal:** Define the data that flows between threads. Every byte that crosses a cache line boundary is a performance tax.

**The Implementation:**
- `tick.hpp`: A `Tick` struct carrying price, bid, ask, quantity, exchange timestamp, feed timestamp, and sequence number. It's exactly 64 bytes — fills one cache line completely. The `feed_ts` is stamped by the feed thread the instant the tick is produced; this timestamp will be compared to the strategy timestamp later to measure cross-thread latency.
- `signal.hpp`: A `Signal` struct with an `Action` enum (`BUY` / `SELL` / `NONE`), the decision price, book state, and two timestamps (`feed_ts` carried through from the tick, and `strategy_ts` stamped when the strategy emits the signal). 

Both structs are **trivially copyable** (enforced by a `static_assert` in the ring buffer). This means the ring buffer can copy them with a single `memcpy` — no constructors, no move semantics, no pointer chasing.

---

## Entry 9: Strategy and Execution Threads

**The Goal:** Spin up two additional threads — one to evaluate a strategy, one to simulate execution — both connected via the SPSC queues.

**The Implementation:**
- `strategy_engine.hpp`: A free function `strategy_loop()` that polls the tick queue. It implements a trivial momentum strategy: if price went up → BUY, if price went down → SELL. The previous price is a single `double` on the stack. No virtual dispatch, no inheritance, no heap. The function stamps `strategy_ts` on every emitted signal.
- `execution_engine.hpp`: A free function `execution_loop()` that polls the signal queue. It stamps `execution_ts`, computes `feed_ts → strategy_ts → execution_ts` latency, and prints the result.

Both loops call `std::this_thread::yield()` when their queue is empty. In a production system with CPU pinning, I'd replace this with `__builtin_arm_yield()` (a single ARM WFE instruction) to avoid wasting cycles while still waking instantly when data arrives.

---

## Entry 10: Wiring the Pipeline in main.cpp

**The Goal:** Orchestrate the three threads with clean startup and shutdown.

**The Implementation:**
The new `main.cpp` creates two SPSC queues and three logically separate threads:
1. **Feed thread** (main thread): Runs `io_context.run()`. The `on_message` callback parses data, updates the local order book, and pushes a `Tick` into `tick_queue`.
2. **Strategy thread** (`std::thread`): Polls `tick_queue`, evaluates the momentum strategy, pushes `Signal` into `signal_queue`.
3. **Execution thread** (`std::thread`): Polls `signal_queue`, prints the execution and latency.

Shutdown is triggered by SIGINT: the handler sets `running = false`, calls `ws.close()`, and stops the `io_context`. The worker threads observe `running == false` on their next loop iteration and exit. Main then joins both threads.

**Error encountered: `static` variable lambda capture**
I initially declared the ring buffers as `static` to keep them out of main's stack frame (they're ~1 MB each). However, the compiler rejected it: `'tick_queue' cannot be captured because it does not have automatic storage duration`. Lambdas can only capture variables with automatic (stack) storage by reference.
**Resolution:** Removed `static`. The ring buffers now live on the stack, which is fine — the default stack size on macOS is 8 MB, and we're using ~1 MB per queue.

**Error encountered: Shutdown assertion**
When killing the process with SIGINT, Boost.Beast threw: `Assertion failed: (! impl.wr_close)`. This was a double-close: the SIGINT handler called `ws.close()` (async), and then the destructor called `ws_.close()` (sync) — but by then, the WebSocket was already in the closing state.
**Resolution:** Added `ws_.is_open()` check to both the `close()` method and the destructor.

---

## Entry 11: The Pipeline Flow (What Happens on Every Tick)

Here is the exact journey of a single price update through the pipeline:

```
1. Binance sends a WebSocket frame over TLS
2. Boost.Asio decrypts it into the pre-allocated flat_buffer (Feed Thread)
3. on_message callback fires:
   a. detect_stream_type() scans ~30 bytes to identify trade vs depth
   b. parse_trade() / parse_depth() extracts fields via string_view::find
   c. OrderBook.update() writes 4 doubles (32 bytes)
   d. A Tick struct is built on the stack (64 bytes)
   e. feed_ts = now()   ← pipeline clock starts
   f. tick_queue.try_push(tick)  ← atomic store, one cache line written
                                                          │
4. Strategy Thread (polling):                              │
   a. tick_queue.try_pop(tick)   ← atomic load             ←┘
   b. if price > prev_price → BUY
   c. strategy_ts = now()
   d. signal_queue.try_push(signal)  ← atomic store
                                                          │
5. Execution Thread (polling):                            │
   a. signal_queue.try_pop(signal)   ← atomic load        ←┘
   b. execution_ts = now()
   c. Print: "Executed BUY @ 68280.82"
   d. Print: Feed→Strategy: 1.0 µs, Strategy→Exec: 0.8 µs
```

Measured end-to-end latency (feed_ts → execution_ts): **0.9 to 5.4 µs**. Sub-microsecond book updates from Module 1 are preserved.

---
---

# Developer Diary: CryptoHedgeLab — Module 3

*Upgrading from log-only execution to a full paper trading engine.*

---

## Entry 12: Position Management — The Four Fill Cases

**The Goal:** Track the current net position (long/short/flat) and average entry price. Every fill changes the position, and some fills close trades that realize profit or loss.

**The Implementation:**
I built `core/execution/position.hpp` with a single `apply_fill(signed_qty, fill_price)` method that handles four distinct cases:

1. **Opening** (flat → long or flat → short): Set qty and avg_price directly. No realized PnL.
2. **Adding** (long + BUY or short + SELL): Compute weighted average entry price: `new_avg = (old_qty × old_avg + fill_qty × fill_price) / total_qty`. No realized PnL.
3. **Reducing** (long + SELL or short + BUY, where |fill| ≤ |position|): Realize PnL on the closed portion: `close_qty × (fill_price - avg_price)` for longs, inverted for shorts. Avg_price doesn't change because only the original entry price matters for the remaining position.
4. **Flipping** (long + SELL where |fill| > |position|): Realize PnL on the entire old position, then open a new position in the opposite direction at the fill price.

This handles all edge cases in a single function with zero allocation.

---

## Entry 13: Fill Simulation — Crossing the Spread

**The Goal:** Simulate realistic order execution. In live trading, market orders "cross the spread" — a BUY lifts the ask, a SELL hits the bid.

**The Implementation:**
The execution engine receives a Signal that already carries `best_bid` and `best_ask` from the order book snapshot. Fill prices are:
- **BUY → fills at `best_ask`** (you're buying at the seller's price)
- **SELL → fills at `best_bid`** (you're selling at the buyer's price)

This means every round-trip trade (BUY then SELL) incurs the spread as a cost — just like real trading. The fixed trade size is 0.001 BTC (~$70 notional), which is small enough that we don't need to model slippage or partial fills.

---

## Entry 14: PnL — Realized vs. Unrealized

**The Goal:** Track profit and loss in two components.

**The Implementation:**
`core/execution/pnl_tracker.hpp`:

- **Realized PnL:** Accumulated profit from closed trades. Updated every time `position.apply_fill()` returns a non-zero value (i.e., a position was reduced or flipped).
- **Unrealized PnL:** Mark-to-market on the open position. Computed on every tick as `position_qty × (mid_price - avg_entry_price)`. For a long position, if the market goes up, unrealized PnL is positive. For a short position, it's the reverse.
- **Total PnL:** `realized + unrealized`. This number represents your true economic value if you were to flatten everything at the current mid-price.

The `mid_price` is computed from the Signal's `best_bid` and `best_ask`: `(bid + ask) / 2`. This avoids the need for a separate market data feed to the execution thread.

---

## Entry 15: The Paper Trading Flow

Here is the full journey of a signal through the paper trading engine:

```
1. Signal arrives from Strategy Thread via SPSC queue
   → action=BUY, price=71041.48, best_bid=71041.47, best_ask=71041.48

2. Fill generated:
   → BUY 0.001 BTC @ 71041.48 (best_ask — crossing the spread)

3. Position updated:
   → apply_fill(+0.001, 71041.48)
   → Case 2 (adding to short): weighted avg recalculated
   → Returns realized_pnl = 0.0 (no close)

4. PnL computed:
   → mid_price = (71041.47 + 71041.48) / 2 = 71041.475
   → unrealized = qty × (mid_price - avg_price)
   → total = realized + unrealized

5. Latency measured:
   → Feed → Strategy:  0.9 µs
   → Strategy → Exec:  1.0 µs
   → Exec → Fill:      2.4 µs
   → Total Pipeline:   4.6 µs
```

Live test results: **575 ticks → 59 fills → +$0.05 realized PnL**. The momentum strategy managed to capture small profits by scalping the spread direction. Pipeline latency: 1.8–7.2 µs end-to-end.

## Entry 16: Scaling Thread Memory — The 8MB Stack Overflow

**The Issue:** When I spun up Module 5, I immediately hit an `AddressSanitizer: stack-overflow` in the main thread inside `main.cpp`.
**The Cause:** I had declared four lock-free rings (`tick_queue`, `signal_queue`, `kdb_feed_q`, `kdb_exec_q`) on the stack. My `SPSCRingBuffer` defaults to 65,536 slots, causing an allocation of ~12 MB. The default macOS thread stack size is 8 MB. 
**The Solution:** Wrapped them all in `std::make_unique<chl::SPSCRingBuffer>...` to allocate them on the heap. Crash completely prevented!

## Entry 17: kdb+ C API Universal Architecture

**The Objective:** Connect C++ sequentially to a kdb+ `:5001` tickerplant over IPC mapping `Tick`, `Fill`, and `PnL` events.
**The Journey:**
- Found out KxSystems doesn't open-source their C API (`c.c` is just an example test file!).
- Discovered that Kx bundles the modern macOS binary under `m64/c.o` as a **Universal Binary**. 
- Integrated `c.o` into CMake `add_executable`, wrapping `#include "k.h"` in `extern "C" { }` to resolve all linker errors.
- **Port Collision Issue:** Initially chose port `4000`, which was silently occupied by a macOS background service. Moved the port to `5001`.

## Entry 18: KdbWriter Batching & Throughput

**The Goal:** Write to kdb+ without blocking the C++ hot-path `Execution Loop`.
**The Implementation:** 
- Thread 5: `KdbWriter`.
- Uses two dedicated SPSC lock-free queues (`kdb_feed_q` and `kdb_exec_q`).
- Accumulates records into `std::vector` batches (1000 items each).
- Constructs large `K` matrix columns native to kdb+ and bulk-dispatches via a single asynchronous `khpu` syscall! (`-h`).

## Entry 19: Replay Engine Architecture

**The Goal:** Achieve >1,000,000 internal Ticks processed Per Second using historical data.
**The Implementation:**
- Rather than injecting ticks magically into the Strategy core, `ReplayEngine` acts strictly as an alternative *Feed Module*.
- Issues IPC `k(h, "select ...")` to load millions of records directly from `schema.q`.
- Wraps them in identical `Tick` structs and spins pushing them onto `tick_queue` or direct via callback.
- **Clock Mocking:** Developed a custom `speed` multiplier that dynamically yields the replay loops `std::chrono::wall_clock` targeting historical pacing.

## Entry 20: Auto-Hedging at Runtime

**The Goal:** Maintain neutral inventory risk! (Delta Neutrality).
**The Implementation:**
- A `HedgeEngine` hooked directly into the end of `execution_loop`.
- Checks the delta skew: Base Position (e.g. `+0.05 BTC` in Spot).
- Instantly models an equal and opposite Futures position: Short `-0.05 BTC` Perpetual.

## Entry 21: Zero-Cost Lock-Free Strategy Hot-Swapping
**The Goal:** Allow the React Frontend Strategy Lab to instantly switch the active backend trading strategy (e.g., from `funding_arbitrage` to `pairs_trading`) without blocking the hot path, mutating state unsafely, or incurring heap allocations.
**The Implementation:**
- Instead of using `std::shared_ptr` or heavily locked multi-threading, we pre-load all 5 advanced strategies (`funding_arb.json`, `pairs_trading.json`, `dual_momentum.json`, `margin_short.json`, `vol_straddle.json`) into a fixed `std::vector<StrategyDef> strats` at startup.
- A single `std::atomic<int> active_idx` acts as the pointer. The `strategy_loop` checks `active_idx.load(std::memory_order_relaxed)` on every tick.
- This is incredibly lightweight (a single assembly `mov` instruction), enabling real-time UI-driven hot-swapping without sacrificing the sub-microsecond latency profile of the trading engine.

## Entry 22: Multi-Asset Stream Multiplexing
**The Goal:** Support complex strategies like Statistical Pairs Trading (BTC & ETH) and Delta-Neutral Funding Arbitrage using simultaneous data streams.
**The Implementation:**
- Extended the `Tick` structure with `symbol_id` and `funding_rate` while enforcing the strict 64-byte limit. 
- Updated `BinanceWebSocket` to connect to Binance's multiplexed stream endpoint (`/stream?streams=btcusdt@trade/ethusdt@trade/btcusdt@markPrice@1s`).
- Introduced stream type detection in `message_parser.hpp` to parse underlying asset names dynamically and route order-book updates to dedicated `OrderBook` instances in `main.cpp`, before consolidating back into the single SPSC `tick_queue`.

---

# Developer Diary: CryptoHedgeLab — Module 6

*Decoupling strategy logic, adding JSON parsing, and building the Quant Comparison Engine.*

## Entry 21: Decoupling the Hot Path

**The Objective:** Instead of a hardcoded momentum strategy inside `strategy_loop`, we need to parse external JSON files without slowing down the hot path.
**The Implementation:**
- Defined `StrategyDef` (immutable struct) and `ConditionType` (enum).
- Wrote a zero-dependency JSON parser (`strategy_parser.hpp`) that runs strictly at startup. 
- During the hot path, evaluating a condition is a simple L1-cache integer `switch (cond)`, keeping the tick-to-signal latency under 1 microsecond.

## Entry 22: Multi-Strategy Matrix Evaluation

**The Objective:** Run multiple strategies simultaneously without duplicating Replay data or spawning N execution threads.
**The Implementation:**
- `StrategyEngine` owns a `std::vector<StrategyInstance>`. 
- When a `Tick` arrives from historical replay, we iterate through every strategy in a tight CPU cache loop. No SPSC queues are used between the Replay Engine and the Strategy Engine, leading to maximum backtesting throughput.

## Entry 23: Online Performance Metrics (Welford's)

**The Objective:** Calculate Sharpe Ratio and Volatility without storing arrays of PnL history.
**The Implementation:**
- Implemented Welford's online algorithm for computing running variance. 
- On every tick, we update `mean_delta` and `M2` incrementally. This provides an exact streaming standard deviation using O(1) memory, ensuring performance metrics never leak or bloat across millions of ticks.

---

## Entry 16: Scaling Thread Memory — The 8MB Stack Overflow

**The Issue:** When I spun up Module 5, I immediately hit an `AddressSanitizer: stack-overflow` in the main thread inside `main.cpp`.
**The Cause:** I had declared four lock-free rings (`tick_queue`, `signal_queue`, `kdb_feed_q`, `kdb_exec_q`) on the stack. My `SPSCRingBuffer` defaults to 65,536 slots. Multiplied by 64 bytes (`Tick`) or 40 bytes (`KdbRecord`), these arrays added up to about ~12 MB of memory. The default macOS thread stack size is 8 MB. 
**The Solution:** Wrapped them all in `std::make_unique<chl::SPSCRingBuffer>...` to allocate them on the heap, passing dereferenced pointers to the worker threads. Crash completely prevented!

---

## Entry 17: kdb+ C API Universal Architecture

**The Objective:** Connect C++ sequentially to a kdb+ `:5001` tickerplant over IPC mapping `Tick`, `Fill`, and `PnL` events.
**The Journey:**
- Found out the hard way that KxSystems doesn't open-source their C API (`c.c` is just an example test file!).
- Tried downloading `c.o` for ARM64 macOS. Received a `404 Not Found`.
- Discovered that Kx bundles the modern macOS binary under `m64/c.o` as a **Universal Binary** containing both `x86_64` and `arm64` static object files. 
- Integrated `c.o` into CMake `add_executable`, wrapping `#include "k.h"` in `extern "C" { }` to resolve all linker errors.
- **Port Collision Issue:** Initially chose port `4000`, which was silently occupied by a macOS background service. This caused the C++ API `k()` syscall to read garbage bytes, leading to a profound infinite recursion crash! Moved the port to `5001`.

---

## Entry 18: KdbWriter Batching & Throughput

**The Goal:** Write to kdb+ without blocking the C++ hot-path `Execution Loop`.
**The Implementation:** 
- Thread 5: `KdbWriter`.
- Uses two dedicated SPSC lock-free queues (`kdb_feed_q` and `kdb_exec_q`).
- Consumes continuously and pushes `KdbRecord` variants.
- Accumulates records into `std::vector` batches (1000 items each).
- Constructs large `K` matrix columns native to kdb+ and bulk-dispatches via a single asynchronous `khpu` syscall! (`-h`). This prevents socket write-saturation on high microbursts.

---

## Entry 19: Replay Engine Architecture

**The Goal:** Achieve >1,000,000 internal Ticks processed Per Second using historical data to radically accelerate backtesting logic iteration.
**The Implementation:**
- Rather than injecting ticks magically into the Strategy core, `ReplayEngine` acts strictly as an alternative *Feed Module*.
- Issues IPC `k(h, "select ...")` to load millions of records directly from `schema.q`.
- Wraps them in identical `Tick` structs and spins pushing them onto `tick_queue`.
- **Clock Mocking:** Developed a custom `speed` multiplier that dynamically yields the replay loops `std::chrono::wall_clock` targeting historical pacing. Setting `speed=0` skips the `sleep_for/yield` loop completely, allowing the system to backtest millions of rows strictly constrained by L1 cache hit-rates.

---

## Entry 20: Auto-Hedging at Runtime

**The Goal:** Maintain neutral inventory risk! (Delta Neutrality).
**The Implementation:**
- A `HedgeEngine` hooked directly into the end of `execution_loop`.
- Checks the delta skew: Base Position (e.g. `+0.05 BTC` in Spot).
- Instantly models an equal and opposite Futures position: Short `-0.05 BTC` Perpetual.
- It tracks both standalone strategy PnL, AND the Hedged PnL side-by-side. If the spot market pumps, the long strategy gains heavily, but the short futures immediately balance it dynamically. All of which serialize cleanly back to kdb!

---

## Entry 21: Zero-Cost Lock-Free Strategy Hot-Swapping (Module 7)

**The Goal:** Allow the React Frontend Strategy Lab to instantly switch the active backend trading strategy (e.g., from `funding_arbitrage` to `pairs_trading`) without blocking the hot path, mutating state unsafely, or incurring heap allocations.
**The Implementation:**
- Instead of using `std::shared_ptr` or heavily locked multi-threading, we pre-load all 5 advanced strategies (`funding_arb.json`, `pairs_trading.json`, `dual_momentum.json`, `margin_short.json`, `vol_straddle.json`) into a fixed `std::vector<StrategyDef> strats` at startup.
- A single `std::atomic<int> active_idx` acts as the pointer.
- The `strategy_loop` checks `active_idx.load(std::memory_order_relaxed)` on every tick.
- This is incredibly lightweight (a single assembly `mov` instruction), enabling real-time UI-driven hot-swapping without sacrificing the sub-microsecond latency profile of the trading engine.

---

## Entry 22: Multi-Asset Stream Multiplexing

**The Goal:** Support complex strategies like Statistical Pairs Trading (BTC & ETH) and Delta-Neutral Funding Arbitrage using simultaneous data streams.
**The Implementation:**
- Extended the `Tick` structure with `symbol_id` and `funding_rate` while enforcing the strict 64-byte limit (to keep cache lines happy). 
- Updated `BinanceWebSocket` to connect to Binance's multiplexed stream endpoint (`/stream?streams=btcusdt@trade/ethusdt@trade/btcusdt@markPrice@1s`).
- Introduced stream type detection in `message_parser.hpp` to parse underlying asset names dynamically and route order-book updates to dedicated `OrderBook` instances in `main.cpp`, before consolidating back into the single SPSC `tick_queue`.
