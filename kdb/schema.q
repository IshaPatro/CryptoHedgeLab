/ CryptoHedgeLab Module 5 + 6: q kdb+ Schema

/ Standard .u.upd definition for direct inserts
.u.upd:{[t;x] insert[t;x]};

/ ── Module 5 Tables ─────────────────────────────────────────────────────

trade: ([time:`timestamp$()]
    sym:`symbol$();
    price:`float$();
    size:`float$()
 );

fills: ([time:`timestamp$()]
    sym:`symbol$();
    side:`symbol$();
    price:`float$();
    qty:`float$()
 );

pnl: ([time:`timestamp$()]
    realized:`float$();
    unrealized:`float$()
 );

latency: ([time:`timestamp$()]
    end_to_end:`float$()
 );

/ ── Module 6 Tables (Quant Strategy Lab) ────────────────────────────────

strategy_results: ([]
    time:`timestamp$();
    name:`symbol$();
    pnl:`float$();
    sharpe:`float$();
    drawdown:`float$();
    win_rate:`float$();
    volatility:`float$()
 );

hedge_results: ([]
    time:`timestamp$();
    name:`symbol$();
    hedge_pnl:`float$();
    base_pnl:`float$()
 );

comparison_results: ([]
    time:`timestamp$();
    rank:`int$();
    name:`symbol$();
    pnl:`float$();
    sharpe:`float$();
    drawdown:`float$()
 );

\p 5001
-1 "CryptoHedgeLab kdb+ instance started on port 5001";
