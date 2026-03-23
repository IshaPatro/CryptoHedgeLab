# ⚡ CryptoHedgeLab

> **A high-performance, low-latency crypto paper trading and backtesting platform built in C++ with kdb+/q**

CryptoHedgeLab is engineered for quant developers and algorithmic traders who need deterministic, microsecond-level strategy execution with real-time hedging via futures and options. It ingests live market data from Binance, evaluates pre-compiled strategies, applies hedging logic, and stores every tick in kdb+/q for high-fidelity backtesting — all without a single line of interpreted code in the hot path.

---

## 📑 Table of Contents

1. [Project Philosophy](#-project-philosophy)
2. [Core Features](#-core-features)
3. [System Architecture](#-system-architecture)
4. [Tech Stack](#-tech-stack)
5. [Project Structure](#-project-structure)
6. [Market Data Ingestion](#-market-data-ingestion-binance)
7. [Strategy Engine](#-strategy-engine)
8. [Hedging Engine](#-hedging-engine)
9. [Paper Trading Engine](#-paper-trading-engine)
10. [Backtesting Engine](#-backtesting-engine)
11. [Performance Metrics](#-performance-metrics)
12. [Latency Monitoring](#-latency-monitoring)
13. [Data Storage — kdb+/q](#-data-storage--kdbq)
14. [Getting Free Market Data](#-getting-free-market-data)
15. [Installation](#-installation)
16. [Configuration](#-configuration)
17. [Running the Platform](#-running-the-platform)
18. [Performance Goals](#-performance-goals)
19. [Roadmap](#-roadmap)
20. [Author](#-author)

---

## 🧠 Project Philosophy

CryptoHedgeLab is built on one belief: **if you can't measure it, you can't trust it.**

Most retail and semi-professional trading platforms abstract away the details that matter most in high-frequency environments — memory allocation patterns, lock contention, tick-to-decision latency, and the true cost of hedging. This platform is designed to expose all of those details and give you full control over them.

### Design Principles

| Principle | What It Means in Practice |
|---|---|
| ✅ Pre-compiled strategy execution | Zero overhead strategy evaluation in the hot path |
| ✅ Fully deterministic logic | Same inputs always produce the same outputs |
| ✅ Microsecond latency tracking | Every pipeline stage is timestamped and measured |
| ✅ Real hedging instruments | Futures and options hedging, not just PnL math |
| ✅ Lock-free architecture | No mutex contention in the critical path |
| ✅ Rigorous performance measurement | Sharpe, drawdown, PnL tracked across every backtest run |

---

## 🚀 Core Features

### 📡 Real-Time Market Data Ingestion
- Connects directly to Binance WebSocket streams for Spot, Futures, and Options
- Processes trade events and full order book depth updates in real time
- Reconstructs an in-memory order book with full bid/ask ladder at every tick
- Zero-copy data flow through the pipeline wherever possible

### ⚡ Low-Latency Execution Engine
- Lock-free single-producer/single-consumer queues between all pipeline stages
- Pre-allocated memory pools — zero heap allocation in the hot path
- Cache-line-aligned data structures to minimize false sharing
- Deterministic execution order with no GC pauses

### 🧠 Strategy Engine (Deterministic)
- Strategies are authored via the UI and compiled down to native C++ logic
- No runtime parsing, no scripting overhead, no interpreter
- Supports price signals, order book imbalance, volume triggers, and EMA-based conditions
- Each strategy run is fully reproducible given the same tick data

### 🛡️ Hedging Engine (Futures + Options)
- Primary hedging via futures: fast, liquid, low-latency delta neutralization
- Secondary hedging via options: tail-risk protection and volatility-aware overlays
- Configurable hedge ratios per strategy run
- Hedged vs. unhedged PnL comparison built into the backtester

### 🏦 Paper Trading Engine
- Full simulated matching engine with realistic order handling
- Supports market orders, limit orders, partial fills, and configurable slippage
- Runs on live Binance data with zero risk of real capital exposure

### 🧪 Backtesting Engine
- Replay historical tick data directly from kdb+/q at configurable speed multipliers
- Deterministic execution ensures backtest = forward test (given same data)
- Outputs full performance report: PnL curve, Sharpe ratio, max drawdown, win rate

### ⏱️ Latency Monitoring
- Timestamps every stage of the pipeline: tick arrival → decision → order → fill
- Real-time latency dashboard in the frontend
- Historical latency percentile tracking (p50, p95, p99)

---

## 🏗️ System Architecture

The platform is structured as a linear, staged pipeline. Each stage communicates with the next via lock-free queues, ensuring no blocking between data ingestion and order execution.

```
┌─────────────────────────────────────────┐
│     Frontend (Strategy Builder +        │
│     Latency Dashboard + PnL UI)         │
└────────────────────┬────────────────────┘
                     │
          ┌──────────▼──────────┐
          │  Strategy Compiler  │
          │    (UI → C++ DSL)   │
          └──────────┬──────────┘
                     │
          ┌──────────▼──────────┐
          │  C++ Execution Core │
          └──────────┬──────────┘
                     │
     ┌───────────────▼───────────────┐
     │   Binance WebSocket Feeds     │
     │  (Spot + Futures + Options)   │
     └───────────────┬───────────────┘
                     │
          ┌──────────▼──────────┐
          │  Order Book Recon   │
          │  (in-memory ladder) │
          └──────────┬──────────┘
                     │
          ┌──────────▼──────────┐
          │   Strategy Engine   │
          │ (compiled C++ logic)│
          └──────────┬──────────┘
                     │
          ┌──────────▼──────────┐
          │    Hedge Engine     │
          │ (Futures + Options) │
          └──────────┬──────────┘
                     │
          ┌──────────▼──────────┐
          │    Risk Controls    │
          └──────────┬──────────┘
                     │
          ┌──────────▼──────────┐
          │  Execution Engine   │
          └──────────┬──────────┘
                     │
     ┌───────────────▼───────────────┐
     │  Paper Trading / Exchange Sim │
     └───────────────┬───────────────┘
                     │
          ┌──────────▼──────────┐
          │    kdb+/q Storage   │
          └──────────┬──────────┘
                     │
          ┌──────────▼──────────┐
          │  Backtesting Engine │
          │  + Replay Engine    │
          └─────────────────────┘
```

---

## ⚙️ Tech Stack

| Layer | Technology | Why |
|---|---|---|
| Core Engine | C++ 17/20 | Deterministic, compiled, zero-overhead abstractions |
| Networking | Boost.Beast / uWebSockets | Async WebSocket handling with minimal latency |
| JSON Parsing | simdjson | SIMD-accelerated parsing, fastest available |
| Concurrency | Lock-free queues (SPSC) | No mutex contention in the hot path |
| Database | kdb+/q | Column-store built for time-series, industry standard in HFT |
| Frontend | React + React Flow | Interactive strategy builder and live dashboards |
| Build System | CMake | Cross-platform, fine-grained compilation control |

---

## 📂 Project Structure

```
cryptohedgelab/
│
├── core/
│   ├── feed_handler/         # Binance WebSocket connection + stream parsing
│   │   ├── spot_feed.cpp
│   │   ├── futures_feed.cpp
│   │   └── options_feed.cpp
│   │
│   ├── order_book/           # In-memory order book reconstruction
│   │   ├── book.hpp
│   │   └── book.cpp
│   │
│   ├── strategy_engine/      # Compiled strategy execution
│   │   ├── strategy_base.hpp
│   │   └── compiled/         # Generated C++ strategies live here
│   │
│   ├── hedge_engine/         # Futures and options hedging logic
│   │   ├── futures_hedge.cpp
│   │   └── options_hedge.cpp
│   │
│   ├── risk_engine/          # Position limits, drawdown guards
│   │   └── risk_controls.cpp
│   │
│   ├── execution_engine/     # Order routing to exchange simulator
│   │   └── executor.cpp
│   │
│   └── exchange_simulator/   # Paper trading matching engine
│       ├── matcher.cpp
│       └── slippage_model.cpp
│
├── kdb/
│   ├── schema.q              # Table definitions
│   ├── ingestion.q           # Real-time data write handlers
│   └── queries.q             # Backtest queries and analytics
│
├── frontend/
│   ├── strategy_builder/     # Visual drag-and-drop strategy composer
│   ├── latency_dashboard/    # Real-time latency chart by pipeline stage
│   ├── pnl_dashboard/        # Live PnL curve, Sharpe, drawdown display
│   └── components/           # Shared UI components
│
├── backtester/
│   └── replay_engine/        # Tick-level historical replay from kdb+
│
├── config/
│   └── settings.json         # Strategy params, hedge ratios, risk limits
│
└── README.md
```

---

## 📡 Market Data Ingestion (Binance)

CryptoHedgeLab consumes multi-instrument WebSocket streams from Binance. All streams are public and require no API key for market data consumption.

### 🪙 Spot Data

| Stream | Fields |
|---|---|
| `btcusdt@trade` | price, quantity, timestamp, buyer/maker flag |
| `btcusdt@depth` | bid/ask price levels, quantities, update ID |

### ⚡ Futures Data (Primary Hedge Instrument)

| Stream | Fields |
|---|---|
| `btcusdt@trade` | price, quantity, timestamp |
| `btcusdt@depth` | bid/ask ladder |
| `btcusdt@markPrice` | mark price, funding rate, next funding time |

Futures are the **primary hedging instrument** due to their liquidity, tight spreads, and low-latency execution characteristics.

### 📊 Options Data (Tail-Risk Hedging)

| Field | Description |
|---|---|
| strike | Strike price of the option contract |
| expiry | Expiration date |
| option price | Current mid-market price |
| implied vol | Used for volatility-aware hedge sizing |

Options are used as a **secondary, event-driven hedge** for tail-risk scenarios where a large adverse move is detected.

---

## 🧠 Strategy Engine

### How Strategies Are Built

Strategies are authored visually in the frontend Strategy Builder — a drag-and-drop node editor built with React Flow. The UI generates a DSL (domain-specific language) representation which is then compiled into native C++ before execution. There is no interpretation at runtime.

**Supported signal types:**
- Price relative to moving averages (EMA, SMA)
- Order book imbalance (bid size vs ask size ratio)
- Volume spike detection
- Spread thresholds
- Custom threshold conditions

### Strategy Compilation Flow

```
UI Node Graph
     ↓
DSL Representation (JSON)
     ↓
Strategy Compiler (C++ codegen)
     ↓
Compiled .so / static strategy object
     ↓
Loaded into Strategy Engine at startup
```

### Example — Order Book Imbalance + EMA Signal

When order book imbalance exceeds 0.6 and price is below the 10-period EMA, the strategy emits a buy signal. When imbalance drops below 0.4 and price is above the EMA, it emits a sell. Both conditions are evaluated in compiled native code with no runtime overhead.

---

## 🛡️ Hedging Engine

### Futures Hedging (Primary)

Futures hedging is applied in real time as the strategy accumulates a spot position. When a long spot position is open and a hedge signal is active, the engine shorts an equivalent notional in perpetual futures scaled by the configured hedge ratio. Short spot positions are hedged with a long futures leg. The hedge ratio is configurable between 0.0 (no hedge) and 1.0 (full delta neutral).

**Why futures first:**
- Deep liquidity on Binance USDT-M perpetuals
- No expiry management (perpetual contracts)
- Funding rate tracked and factored into PnL
- Sub-millisecond order placement latency in simulation

### Options Hedging (Secondary — Tail Risk)

Options are used when the risk engine detects a high-volatility environment or a large adverse position has formed. A protective put is purchased at a configurable OTM strike (e.g. 5% out of the money) with the nearest weekly expiry. Quantity is sized proportionally to the open spot position. Options premium paid is tracked separately in PnL attribution.

**Hedge comparison in backtester:**

Every backtest run outputs a side-by-side comparison:

| Metric | Unhedged | Futures Hedged | Options Hedged |
|---|---|---|---|
| Total PnL | — | — | — |
| Sharpe Ratio | — | — | — |
| Max Drawdown | — | — | — |
| Volatility | — | — | — |

*(Values populated per backtest run — see Performance Metrics section)*

---

## 🏦 Paper Trading Engine

The exchange simulator replicates the core matching logic of a real exchange without any network round-trips or real capital exposure.

### Supported Order Types

| Order Type | Behavior |
|---|---|
| Market Order | Filled immediately at best available price |
| Limit Order | Queued and filled when price crosses the limit |
| Partial Fill | Large orders may fill across multiple price levels |

### Slippage Model

Slippage is modeled as a function of order size relative to available liquidity at each price level. Larger orders that consume multiple book levels incur proportionally higher slippage. The slippage factor is configurable in `settings.json` and can be calibrated against historical Binance order book data.

---

## 🧪 Backtesting Engine

The backtester replays historical tick data stored in kdb+/q at configurable speed multipliers. Execution is fully deterministic — given the same data, the same strategy will always produce the same fills.

### Running a Backtest

```bash
./replay_engine --symbol BTCUSDT --speed 10x --start 2024-01-01 --end 2024-03-31
```

### Backtest Output

Each run produces:
- **PnL curve** — cumulative profit and loss over the backtest window, plotted tick-by-tick
- **Sharpe ratio** — annualized risk-adjusted return
- **Maximum drawdown** — largest peak-to-trough decline
- **Win rate** — percentage of trades that closed profitable
- **Average trade duration** — time in position per trade
- **Hedge effectiveness** — how much the hedge reduced realized drawdown vs unhedged

All results are written back to kdb+/q and visualized in the frontend PnL dashboard.

---

## 📊 Performance Metrics

Performance metrics are computed automatically at the end of every backtest run and tracked in real time during paper trading sessions.

### Returns
- Cumulative PnL curve tracked at tick resolution, stored in kdb+ and visualized in the frontend
- Entry and exit points overlaid on the price series
- PnL reported before and after hedge costs (funding rates, options premium) for full attribution
- Hedged vs unhedged equity curves displayed side by side

### Risk-Adjusted Return
- Annualized Sharpe ratio computed separately for hedged and unhedged runs
- Annualized on a 365-day basis since crypto markets trade continuously
- A Sharpe above 1.5 is the minimum threshold for a strategy to be considered viable

### Drawdown
- Maximum drawdown tracked over the full backtest window, reported in both percentage and absolute USDT
- Drawdown guard in the risk engine halts paper trading if the live drawdown breaches a configurable limit (default: 5%)
- Calmar ratio (annualized return divided by max drawdown) reported alongside Sharpe

### Trade-Level Metrics
- Win rate — percentage of trades that closed in profit
- Profit factor — ratio of gross profit to gross loss
- Average win vs average loss — risk-reward per trade
- Total trade count and average hold time per position
- Turnover — total volume traded relative to starting capital

### Cost Attribution
- Futures funding costs broken out separately from trading PnL
- Options premium spent on protective puts tracked per backtest run
- Net PnL reported after all costs, so strategy viability is never overstated

---

## ⏱️ Latency Monitoring

Every stage of the pipeline is instrumented with high-resolution timestamps. The frontend latency dashboard displays live readings and historical percentile distributions.

### Pipeline Stages Tracked

| Stage | Description |
|---|---|
| Tick Received | Timestamp when WebSocket message arrives at feed handler |
| Book Updated | Time to reconstruct order book after depth event |
| Strategy Decision | Time to evaluate strategy logic and emit signal |
| Order Sent | Time to pass order to exchange simulator |
| Fill Confirmed | Time to receive fill acknowledgement |

### Example Latency Readings

```
Tick → Book Update      :   1.2 µs
Book → Strategy Decision:   3.1 µs
Decision → Order Sent   :   4.8 µs
Order → Fill            :   2.5 µs
──────────────────────────────────
End-to-End              :  11.6 µs
```

### Latency Percentiles (p50 / p95 / p99)

| Stage | p50 | p95 | p99 |
|---|---|---|---|
| Tick → Decision | 3.1 µs | 5.4 µs | 9.2 µs |
| Decision → Fill | 7.1 µs | 11.3 µs | 18.6 µs |
| End-to-End | 11.6 µs | 19.4 µs | 28.1 µs |

---

## 🗄️ Data Storage — kdb+/q

All market data, fills, positions, and latency measurements are persisted to kdb+/q, a columnar time-series database purpose-built for high-frequency financial data.

### What to Store

Not everything coming off the wire needs to be persisted. Storage is kept lean and purposeful:

* ✅ **Trades (tick data, short-term only)** — every individual trade event from Binance, kept for a rolling window to feed the backtester and signal computation. Older data is expired or archived to keep query performance tight.
* ✅ **Best bid/ask (not full depth)** — only the top-of-book is stored, not the full 20-level ladder. Full depth is maintained in memory for live strategy evaluation but writing every depth update to disk is unnecessary and expensive at tick frequency.
* ✅ **Executed fills** — every simulated order fill with side, price, quantity, and slippage, forming the authoritative trade log.
* ✅ **Positions** — periodic snapshots of spot, futures, and options positions for risk monitoring and replay.
* ✅ **PnL snapshots** — cumulative realized and unrealized PnL, both hedged and unhedged, at regular intervals for the equity curve.
* ✅ **Latency measurements** — tick-to-decision and decision-to-fill timings for every event, used to build percentile distributions in the dashboard.
* ✅ **Futures mark price + funding rate** — needed for accurate PnL attribution on futures hedge legs.
* ✅ **Options fills** — strike, expiry, premium paid, and position size when a protective put is exercised.
* ❌ **Full order book depth history** — not stored. Too voluminous, and the in-memory book is sufficient for live use. Historical depth replay can be sourced externally if needed.
* ❌ **Raw WebSocket frames** — not stored. Parsed and forwarded only.

---

### Schema

```q
trade:([]
  time:    `timestamp$();
  sym:     `symbol$();
  price:   `float$();
  size:    `int$()
)
```

### Futures Table

```q
futures:([]
  time:        `timestamp$();
  sym:         `symbol$();
  price:       `float$();
  size:        `int$();
  markPrice:   `float$();
  fundingRate: `float$()
)
```

### Options Table

```q
options:([]
  time:   `timestamp$();
  sym:    `symbol$();
  strike: `float$();
  expiry: `date$();
  price:  `float$();
  iv:     `float$()
)
```

### Order Book Snapshots

```q
book:([]
  time:    `timestamp$();
  sym:     `symbol$();
  bid:     `float$();
  ask:     `float$();
  bidSize: `int$();
  askSize: `int$()
)
```

### Executed Fills

```q
fills:([]
  time:     `timestamp$();
  sym:      `symbol$();
  side:     `symbol$();
  price:    `float$();
  qty:      `int$();
  slippage: `float$()
)
```

### Positions (Multi-Asset)

```q
positions:([]
  time:    `timestamp$();
  sym:     `symbol$();
  spot:    `float$();
  futures: `float$();
  options: `float$()
)
```

### PnL Snapshots

```q
pnl:([]
  time:           `timestamp$();
  sym:            `symbol$();
  realizedPnl:    `float$();
  unrealizedPnl:  `float$();
  hedgedPnl:      `float$();
  unhedgedPnl:    `float$();
  drawdown:       `float$()
)
```

### Latency Log

```q
latency:([]
  time:         `timestamp$();
  tickToDecision: `long$();
  decisionToFill: `long$();
  endToEnd:       `long$()
)
```

---

## 🆓 Getting Free Market Data

CryptoHedgeLab is designed to run entirely on free, publicly available Binance data.

### Live Data (100% Free — No API Key Required)

| Data | Source | Notes |
|---|---|---|
| Spot trades + order book | Binance WebSocket | Public stream, no auth needed |
| Futures trades + order book | Binance Futures WebSocket | Includes mark price + funding rate |
| Options quotes | Binance Options WebSocket | Available on USDT-settled options |

### Historical Data (Free via Binance Data Portal)

Visit [data.binance.vision](https://data.binance.vision) for bulk historical downloads:

| Data | Availability | Notes |
|---|---|---|
| Spot trade ticks | ✅ Full history | Daily/monthly CSVs per symbol |
| Futures trade ticks | ✅ Full history | Same format as spot |
| Klines (OHLCV) | ✅ Full history | 1s to 1M intervals |
| Order book snapshots | ⚠️ Partial | Snapshots, not full depth stream history |
| Options history | ⚠️ Limited | Sparse data, especially pre-2023 |

### Notes on Order Book History

Full order book replay (every depth update tick) is the hardest data to get for free. The Binance data portal provides periodic snapshots, not a continuous stream of depth events. For production-grade order book backtesting, consider:

- **Tardis.dev** — normalized full depth data, ~$20–50 for targeted datasets
- **Building your own recorder** — run the feed handler in record mode to accumulate your own tick database in kdb+

For most strategy research purposes, trade-tick level data from the Binance data portal is sufficient to get meaningful backtest results.

---

## 🔧 Installation

### Prerequisites

- C++17/20 compatible compiler (GCC 11+ or Clang 14+)
- CMake 3.20+
- Boost 1.80+ (Beast, Asio)
- kdb+/q installed (free 32-bit version available from kx.com)
- Node.js 18+ and npm (for frontend)

### Start kdb+/q

```bash
# Start kdb+ on port 5000 and load the schema
q kdb/schema.q -p 5000
```

---

## ⚙️ Configuration

All runtime parameters are set in `config/settings.json`:

```json
{
  "feed": {
    "symbol": "BTCUSDT",
    "streams": ["spot", "futures", "options"],
    "orderbook_depth": 20
  },
  "strategy": {
    "name": "imbalance_ema_v1",
    "ema_period": 10,
    "imbalance_threshold": 0.6
  },
  "hedge": {
    "mode": "futures",
    "ratio": 0.8,
    "options_otm_factor": 0.95,
    "tail_risk_threshold": 0.05
  },
  "risk": {
    "max_position_usdt": 10000,
    "max_drawdown_pct": 5.0,
    "halt_on_drawdown": true
  },
  "paper_trading": {
    "initial_capital_usdt": 100000,
    "slippage_factor": 0.0002
  },
  "kdb": {
    "host": "localhost",
    "port": 5000
  }
}
```

---

## ▶️ Running the Platform

### Live Paper Trading

```bash
# Start the execution engine with live Binance data
./build/cryptohedgelab --config config/settings.json --mode paper
```

### Backtesting

```bash
# Replay historical tick data from kdb+ at 10x speed
./build/replay_engine \
  --symbol BTCUSDT \
  --start 2024-01-01 \
  --end 2024-03-31 \
  --speed 10x \
  --hedge futures
```

### Frontend Dashboard

```bash
cd frontend
npm run dev
# Open http://localhost:3000
```

The dashboard provides:
- **Strategy Builder** — visual node editor to compose and compile strategies
- **Latency Dashboard** — real-time pipeline latency by stage, with p50/p95/p99 percentiles
- **PnL Dashboard** — live cumulative PnL curve, Sharpe, max drawdown counter
- **Backtest Viewer** — replay results with hedged vs unhedged comparison

---

## 🎯 Performance Goals

| Metric | Target | Notes |
|---|---|---|
| Tick Processing | < 5 µs | Feed handler to order book update |
| Strategy Execution | < 2 µs | Signal evaluation only |
| End-to-End Latency | < 20 µs | Tick arrival to fill confirmation |
| Backtest Throughput | > 1M ticks/sec | kdb+ replay speed |
| Sharpe Threshold | > 1.5 | Minimum acceptable for strategy viability |
| Max Drawdown Limit | < 5% | Enforced by risk engine in paper trading |

---

## 🗺️ Roadmap

- [ ] Multi-symbol strategy support (portfolio-level hedging)
- [ ] Options Greeks computation (delta, gamma, vega) for smarter hedge sizing
- [ ] Transaction cost model calibrated against real Binance fee tiers
- [ ] Walk-forward optimization in the backtester
- [ ] Cross-exchange feed ingestion (OKX, Bybit)
- [ ] REST API for external strategy submission
- [ ] Persistent latency anomaly alerting

---

## 👩‍💻 Author

**Isha Patro**
Software Engineer | Quant Developer

Built with a focus on HFT-style system design — where performance, correctness, and timing are not optional.

---

*CryptoHedgeLab is a paper trading and research platform. It does not place real orders or manage real capital.*