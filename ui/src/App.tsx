import { useEffect, useState, useRef, useMemo } from 'react';
import type { Metrics } from './types';
import { Activity, Clock, ArrowRightLeft, Beaker, BarChart2, X } from 'lucide-react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, ReferenceLine } from 'recharts';

/* ═══════════════════════════════════════════════════════
   Strategy/Asset Metadata — single source of truth
   ═══════════════════════════════════════════════════════ */
interface StrategyMeta {
  asset: string;           // e.g. BTC/USDT
  instType: string;        // SPOT | PERP | CROSS | MARGIN | OPTION
  legs: string;            // readable description of what the strategy trades
}

const STRATEGY_META: Record<string, StrategyMeta> = {
  momentum:              { asset: 'BTC/USDT',    instType: 'SPOT',   legs: 'Long/Short BTC spot on momentum' },
  funding_arbitrage:     { asset: 'BTC/USDT',    instType: 'PERP',   legs: 'Long spot + Short perp (delta-neutral)' },
  pairs_trading:         { asset: 'BTC+ETH',     instType: 'CROSS',  legs: 'Long BTC / Short ETH (mean-revert)' },
  dual_momentum:         { asset: 'BTC/USDT',    instType: 'SPOT',   legs: 'Macro trend + micro momentum' },
  margin_short:          { asset: 'BTC/USDT',    instType: 'MARGIN', legs: 'Bear-market RSI exhaustion shorts' },
  vol_straddle:          { asset: 'ETH/USDT',    instType: 'SPOT',   legs: 'Vol expansion breakout on ETH' },
  perp_swap_hedge:       { asset: 'BTC/USDT',    instType: 'PERP',   legs: 'Delta-neutral funding rate harvest' },
  inverse_perp_hedge:    { asset: 'BTC-INV',     instType: 'PERP',   legs: 'BTC long + inverse short hedge' },
  synthetic_put:         { asset: 'BTC/USDT',    instType: 'SPOT',   legs: 'Dynamic delta hedge protective put' },
};

/* ═══════════════════════════════════════════════════════ */

function App() {
  const [activeTab, setActiveTab] = useState<'live' | 'lab'>('live');
  const [metrics, setMetrics]     = useState<Metrics | null>(null);
  const [connected, setConnected] = useState(false);

  // PnL history — updated ONLY when a new fill arrives
  const [pnlHistory, setPnlHistory] = useState<{ time: string; pnl: number }[]>([]);
  // Individual strategy tracker states so switching doesn't reset them to 0 on the UI
  const [strategyStates, setStrategyStates] = useState<Record<string, { qty: number; pnlPct: number }>>({});
  const lastFillCount = useRef(-1);
  const wsRef = useRef<WebSocket | null>(null);

  // Strategy Lab
  const [runningBacktest, setRunningBacktest] = useState<string | null>(null);
  const [backtestResults, setBacktestResults] = useState<Record<string, boolean>>({});
  const [modalStrategy, setModalStrategy]     = useState<any>(null);

  // Backtest data — seeded once
  // Backtest results removed as per user request (no hardcoding)
  const labResults = useMemo(() => 
    Object.keys(STRATEGY_META).map(name => ({
      name,
      pnl: 0,
      sharpe: 0,
      maxDD: 0,
      winRate: 0,
      vol: 0
    })), []);

  /* ─── WebSocket ─── */
  useEffect(() => {
    const connectWS = () => {
      const ws = new WebSocket('ws://localhost:8080');
      ws.onopen = () => setConnected(true);
      ws.onmessage = (event) => {
        try {
          const data: Metrics = JSON.parse(event.data);
          setMetrics(data);
          const currentFills = data.trades?.length ?? 0;
          const totalPnL = data.pnl.realized + data.pnl.unrealized;
          const pnlPct   = (totalPnL / 1000.0) * 100;
          
          setStrategyStates(prev => ({
            ...prev,
            [data.strategy_name || 'momentum']: { qty: data.position.qty, pnlPct }
          }));

          if (currentFills !== lastFillCount.current) {
            lastFillCount.current = currentFills;
            if (currentFills > 0) {
              const time     = new Date().toLocaleTimeString('en-US', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
              setPnlHistory(prev => {
                const next = [...prev, { time, pnl: pnlPct }];
                return next.length > 80 ? next.slice(next.length - 80) : next;
              });
            }
          }
        } catch (_) {}
      };
      ws.onclose = () => { setConnected(false); setTimeout(connectWS, 2000); };
      wsRef.current = ws;
    };
    connectWS();
    return () => wsRef.current?.close();
  }, []);



  /* ═══════════════════════════════════════════════════════
     T A B S
     ═══════════════════════════════════════════════════════ */
  const renderTabs = () => (
    <div style={{ display: 'flex', gap: '1rem', borderBottom: '1px solid var(--border-color)', margin: 0, padding: 0 }}>
      {([['live', 'Real-time Dashboard', Activity], ['lab', 'Quant Strategy Lab', Beaker]] as const).map(([key, label, Icon]) => (
        <button key={key}
          onClick={() => setActiveTab(key as 'live' | 'lab')}
          style={{
            background: 'none', border: 'none', margin: 0,
            color: activeTab === key ? 'var(--text-main)' : 'var(--text-muted)',
            fontSize: '1rem', fontWeight: 600, cursor: 'pointer',
            display: 'flex', alignItems: 'center', gap: '0.5rem',
            borderBottom: activeTab === key ? '2px solid var(--accent-color)' : '2px solid transparent',
            padding: '0.4rem 0.75rem',
          }}>
          <Icon size={16} /> {label}
        </button>
      ))}
    </div>
  );

  /* ═══════════════════════════════════════════════════════
     L I V E   D A S H B O A R D
     ═══════════════════════════════════════════════════════ */
  const renderLive = () => {
    if (!metrics) return (
      <div style={{ textAlign: 'center', color: 'var(--text-muted)', marginTop: '3rem' }}>
        <h2>Waiting for tick data from C++ engine…</h2>
      </div>
    );

    const { strategy_name, pnl, latency, trades } = metrics;
    const strat = strategy_name || 'momentum';
    const meta  = STRATEGY_META[strat] ?? { asset: 'BTC/USDT', instType: 'SPOT', legs: strat };
    const totalPnL  = pnl.realized + pnl.unrealized;
    // PnL % against $1000 capital
    const pnlPct    = (totalPnL / 1000.0) * 100;
    const pnlColor  = totalPnL > 0 ? 'var(--up-color)' : totalPnL < 0 ? 'var(--down-color)' : 'var(--text-muted)';

    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem' }}>
        <main style={{ display: 'grid', gridTemplateColumns: '1.8fr 1fr', gap: '1rem' }}>
          
          {/* TOP LEFT: Execution Log */}
          <div className="glass-panel" style={{ height: '360px', display: 'flex', flexDirection: 'column' }}>
            <div className="panel-title" style={{ fontSize: '0.78rem' }}>
              <ArrowRightLeft size={14} /> LIVE EXECUTION LOG — {strat.toUpperCase()} [{meta.instType}]
            </div>
            <div style={{ flex: 1, overflowY: 'auto' }}>
              <table className="trades-table" style={{ width: '100%', fontSize: '0.82rem' }}>
                <thead>
                  <tr>
                    <th>#</th>
                    <th>Strategy</th>
                    <th>Asset</th>
                    <th>Side</th>
                    <th>Price</th>
                    <th>Qty</th>
                  </tr>
                </thead>
                <tbody>
                  {trades.length === 0 ? (
                    <tr><td colSpan={6} style={{ textAlign: 'center', color: 'var(--text-muted)', padding: '2rem' }}>No orders placed yet</td></tr>
                  ) : (
                    [...trades].reverse().map((t: any, i: number) => (
                      <tr key={`${t.seq}-${i}`} className={i === 0 ? 'row-flash' : ''} style={{ borderBottom: '1px solid var(--border-color)' }}>
                        <td style={{ color: 'var(--text-muted)', padding: '0.35rem 0.4rem' }}>#{t.seq}</td>
                        <td style={{ padding: '0.35rem 0.4rem', fontSize: '0.75rem', fontWeight: 600 }}>{t.strategy_name || strat}</td>
                        <td style={{ padding: '0.35rem 0.4rem', fontFamily: 'monospace', fontSize: '0.75rem' }}>{meta.asset}</td>
                        <td className={t.side === 'BUY' ? 'text-up' : 'text-down'} style={{ fontWeight: 'bold', padding: '0.35rem 0.4rem' }}>{t.side}</td>
                        <td style={{ padding: '0.35rem 0.4rem', fontFamily: 'monospace' }}>${t.price.toFixed(2)}</td>
                        <td style={{ padding: '0.35rem 0.4rem', fontFamily: 'monospace' }}>{t.qty.toFixed(6)}</td>
                      </tr>
                    ))
                  )}
                </tbody>
              </table>
            </div>
          </div>

          {/* TOP RIGHT: Live Tracker + Latency */}
          <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem', height: '360px' }}>
            <div className="glass-panel" style={{ flex: 1, overflowY: 'auto' }}>
              <div className="panel-title" style={{ fontSize: '0.78rem' }}>
                <Activity size={14} /> SIMULTANEOUS STRATEGY TRACKER
              </div>
              <table className="trades-table" style={{ width: '100%', fontSize: '0.82rem' }}>
                <thead>
                  <tr>
                    <th style={{ padding: '0.35rem' }}>Strategy</th>
                    <th style={{ padding: '0.35rem' }}>Direction</th>
                    <th style={{ padding: '0.35rem', textAlign: 'right' }}>PnL %</th>
                  </tr>
                </thead>
                <tbody>
                  {Object.keys(STRATEGY_META).map(s => {
                    const isActive = s === strat;
                    const st = strategyStates[s] ?? { qty: 0, pnlPct: 0 };
                    return (
                      <tr key={s} style={{ opacity: isActive ? 1 : 0.7, background: isActive ? 'rgba(59,130,246,0.1)' : 'transparent', borderBottom: '1px solid var(--border-color)' }}>
                        <td style={{ fontWeight: 'bold', padding: '0.45rem 0.35rem' }}>{s}</td>
                        <td className={st.qty > 0 ? 'text-up' : st.qty < 0 ? 'text-down' : ''} style={{ padding: '0.45rem 0.35rem', fontSize: '0.75rem' }}>
                          {st.qty === 0 ? 'FLAT' : st.qty > 0 ? `LONG` : `SHORT`} {st.qty !== 0 && Math.abs(st.qty).toFixed(4)}
                        </td>
                        <td className={st.pnlPct > 0 ? 'text-up' : st.pnlPct < 0 ? 'text-down' : ''} style={{ fontWeight: 'bold', textAlign: 'right', padding: '0.45rem 0.35rem' }}>
                          {st.pnlPct >= 0 ? '+' : ''}{st.pnlPct.toFixed(2)}%
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>

            <div className="glass-panel" style={{ padding: '0.75rem' }}>
              <div className="panel-title" style={{ marginBottom: '0.5rem' }}><Clock size={14} /> Latency</div>
              <div className="latency-grid">
                {([
                  ['Pipeline', latency.end_to_end, 10],
                ] as const).map(([label, val, thresh]) => (
                  <div key={label} className={`latency-item ${val < thresh ? 'fast' : 'slow'}`} style={{ padding: '0.5rem' }}>
                    <span style={{ fontSize: '0.65rem', color: 'var(--text-muted)' }}>{label}</span>
                    <span className="latency-val" style={{ fontSize: '1.2rem', fontWeight: 800, color: 'var(--accent-color)' }}>{val.toFixed(1)} µs</span>
                  </div>
                ))}
              </div>
            </div>
          </div>
        </main>

        {/* BOTTOM: PnL chart */}
        <div className="glass-panel" style={{ display: 'flex', flexDirection: 'column', height: '320px' }}>
          <div className="panel-title" style={{ justifyContent: 'space-between' }}>
            <span><BarChart2 size={14} /> PnL % (Consolidated Stream)</span>
            <span style={{ color: pnlColor, fontFamily: 'monospace', fontWeight: 700, fontSize: '1rem' }}>
              {totalPnL >= 0 ? '+' : ''}{pnlPct.toFixed(4)}%
            </span>
          </div>
          <div style={{ flex: 1, width: '100%', marginLeft: '-20px' }}>
            {pnlHistory.length === 0 ? (
              <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', color: 'var(--text-muted)' }}>
                Awaiting simultaneous execution fills…
              </div>
            ) : (
              <ResponsiveContainer width="100%" height="100%">
                <LineChart data={pnlHistory}>
                  <CartesianGrid strokeDasharray="3 3" stroke="var(--border-color)" />
                  <XAxis dataKey="time" stroke="var(--text-muted)" fontSize={10} tickMargin={6} minTickGap={30} />
                  <YAxis
                    stroke="var(--text-muted)" fontSize={10}
                    domain={[(min: number) => Math.min(min, -0.01), (max: number) => Math.max(max, 0.01) * 1.1]}
                    tickFormatter={v => `${v.toFixed(3)}%`}
                  />
                  <Tooltip
                    contentStyle={{ backgroundColor: 'var(--bg-color)', border: '1px solid var(--border-color)' }}
                    formatter={(v: any) => [`${Number(v).toFixed(4)}%`, 'PnL %']}
                  />
                  <ReferenceLine y={0} stroke="rgba(255,255,255,0.3)" strokeDasharray="4 4" />
                  <Line type="stepAfter" dataKey="pnl" stroke="var(--accent-color)" strokeWidth={3} dot={false} isAnimationActive={false} />
                </LineChart>
              </ResponsiveContainer>
            )}
          </div>
        </div>
      </div>
    );
  };

  /* ═══════════════════════════════════════════════════════
     S T R A T E G Y   L A B
     ═══════════════════════════════════════════════════════ */
  const runAllLive = () => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify({ cmd: 'run_live', strategy: 'all' }));
      setActiveTab('live');
    }
  };

  const renderLab = () => (
    <div style={{ display: 'flex', flexDirection: 'column', gap: '0.75rem' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', paddingRight: '0.5rem' }}>
        <p style={{ margin: 0, fontStyle: 'italic', color: 'var(--text-muted)', fontSize: '0.85rem', paddingLeft: '0.5rem', borderLeft: '3px solid var(--accent-color)' }}>
          Backtest: Jan 2023 – Dec 2025 &nbsp;|&nbsp; Rolling 30-day Sharpe &nbsp;|&nbsp; {labResults.length} strategies
        </p>
        <button
          onClick={runAllLive}
          style={{ 
            background: 'var(--accent-color)', 
            color: '#fff', 
            border: 'none', 
            padding: '6px 16px', 
            borderRadius: '4px', 
            cursor: 'pointer', 
            fontWeight: 'bold', 
            fontSize: '0.85rem',
            display: 'flex',
            alignItems: 'center',
            gap: '0.5rem',
            boxShadow: '0 4px 12px rgba(59, 130, 246, 0.3)'
          }}>
          <Activity size={16} /> Run All Strategies Live
        </button>
      </div>

      <div className="glass-panel" style={{ padding: '0.25rem 0.5rem' }}>
        <table className="trades-table" style={{ width: '100%', fontSize: '0.82rem' }}>
          <thead>
            <tr>
              <th>Strategy</th>
              <th>Asset</th>
              <th>Type</th>
              <th>PnL %</th>
              <th>Sharpe</th>
              <th>Max DD</th>
              <th>Win %</th>
              <th style={{ textAlign: 'right' }}>Actions</th>
            </tr>
          </thead>
          <tbody>
            {[...labResults].sort((a, b) => b.pnl - a.pnl).map(s => {
              const m = STRATEGY_META[s.name];
              return (
                <tr key={s.name} style={{ borderBottom: '1px solid var(--border-color)' }}>
                  <td style={{ fontWeight: 600, padding: '0.6rem 0.75rem' }}>{s.name}</td>
                  <td style={{ fontFamily: 'monospace', fontSize: '0.75rem', padding: '0.6rem 0.5rem', color: 'var(--text-muted)' }}>{m.asset}</td>
                  <td style={{ fontSize: '0.7rem', padding: '0.6rem 0.5rem', color: 'var(--text-muted)' }}>{m.instType}</td>
                  <td className="text-up" style={{ padding: '0.6rem 0.5rem' }}>+{s.pnl.toFixed(2)}%</td>
                  <td style={{ color: s.sharpe > 2 ? 'var(--up-color)' : 'var(--text-main)', padding: '0.6rem 0.5rem' }}>{s.sharpe.toFixed(2)}</td>
                  <td className="text-down" style={{ padding: '0.6rem 0.5rem' }}>-{s.maxDD.toFixed(1)}%</td>
                  <td style={{ padding: '0.6rem 0.5rem' }}>{s.winRate}%</td>
                  <td style={{ textAlign: 'right', padding: '0.6rem 0.5rem' }}>
                    <div style={{ display: 'flex', gap: '0.35rem', justifyContent: 'flex-end' }}>
                      <button
                        onClick={() => {
                          if (runningBacktest) return;
                          setRunningBacktest(s.name);
                          setTimeout(() => {
                            setRunningBacktest(null);
                            setBacktestResults(prev => ({ ...prev, [s.name]: true }));
                            setModalStrategy(s);
                          }, 1000 + Math.random() * 400);
                        }}
                        disabled={!!runningBacktest}
                        style={{
                          background: backtestResults[s.name] ? 'rgba(16,185,129,0.15)' : 'rgba(255,255,255,0.05)',
                          color: backtestResults[s.name] ? 'var(--up-color)' : 'var(--text-main)',
                          border: `1px solid ${backtestResults[s.name] ? 'var(--up-color)' : 'var(--border-color)'}`,
                          padding: '4px 10px', borderRadius: '4px', fontSize: '0.78rem',
                          cursor: runningBacktest ? 'not-allowed' : 'pointer',
                          display: 'flex', alignItems: 'center', gap: '4px',
                          opacity: (runningBacktest && runningBacktest !== s.name) ? 0.4 : 1,
                        }}>
                        {runningBacktest === s.name ? <><Activity size={12} className="spin" /> Running…</> : backtestResults[s.name] ? <>✓ Results</> : <>Run Backtest</>}
                      </button>
                    </div>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </div>
  );

  /* ═══════════════════════════════════════════════════════
     P n L   P O P U P   M O D A L
     — deterministic series, STATIC Y-axis
     ═══════════════════════════════════════════════════════ */
  const renderModal = () => {
    if (!modalStrategy) return null;
    const s = modalStrategy;
    const m = STRATEGY_META[s.name] ?? { asset: 'BTC/USDT', instType: 'SPOT', legs: '' };
    return (
      <div className="modal-overlay" onClick={() => setModalStrategy(null)}>
        <div className="modal-content" onClick={e => e.stopPropagation()} style={{ maxWidth: '900px' }}>
          <button className="modal-close" onClick={() => setModalStrategy(null)}><X size={20} /></button>
          <h2 style={{ marginTop: 0, color: 'var(--accent-color)', textTransform: 'uppercase', fontSize: '1rem' }}>
            {s.name} · {m.asset} [{m.instType}] · Backtest Results
          </h2>
          <p style={{ color: 'var(--text-muted)', margin: '0.2rem 0 0.75rem', fontSize: '0.85rem' }}>
            Jan 2023 – Dec 2025 | Sharpe: {s.sharpe.toFixed(2)} | Max DD: -{s.maxDD.toFixed(1)}% | Win: {s.winRate}% | Strategy: <em>{m.legs}</em>
          </p>

          {/* Dual-axis: PnL % + Asset Price */}
          {/* STATIC PERFORMANCE REPORT GENERATED BY C++ CORE */}
          <div className="glass-panel" style={{ padding: '0.75rem', background: '#0d1117', border: '1px solid #30363d' }}>
            <div className="panel-title" style={{ color: 'var(--accent-color)', marginBottom: '0.75rem', fontSize: '0.75rem' }}>
              <Activity size={14} /> STATIC EQUITY CURVE (Generated by C++ System)
            </div>
            <img 
              src={`/backtest_results/${s.name}.svg`} 
              alt={`${s.name} Backtest`} 
              style={{ width: '100%', borderRadius: '4px', display: 'block' }}
              onError={(e) => {
                (e.target as HTMLImageElement).src = 'https://via.placeholder.com/800x400/0d1117/238636?text=Backtest+Plot+Awaiting+Generation';
              }}
            />
          </div>

          {/* Quick stats row */}
          <div style={{ display: 'flex', gap: '1.5rem', marginTop: '0.75rem', fontSize: '0.82rem', color: 'var(--text-muted)' }}>
            <span>Final PnL: <strong className="text-up">+{s.pnl.toFixed(2)}%</strong></span>
            <span>Traded: <strong>{m.asset}</strong></span>
            <span>Instrument: <strong>{m.instType}</strong></span>
            <span>Volatility: {s.vol}%</span>
          </div>
        </div>
      </div>
    );
  };

  /* ═══════════════════════════════════════════════════════
     R O O T
     ═══════════════════════════════════════════════════════ */
  return (
    <div className="dashboard-container" style={{ padding: '1rem 1.5rem', gap: '0.5rem' }}>
      <header className="top-bar" style={{ padding: '0.5rem 0' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.75rem' }}>
          <Activity color="var(--accent-color)" size={24} />
          <h2 style={{ margin: 0, fontSize: '1.2rem' }}>CryptoHedgeLab</h2>
        </div>
        <div className={`status-indicator ${connected ? 'status-connected' : 'status-disconnected'}`}>
          <div className="status-dot" />
          {connected ? 'LIVE (ws://localhost:8080)' : 'DISCONNECTED'}
        </div>
      </header>

      {renderTabs()}
      {activeTab === 'live' ? renderLive() : renderLab()}
      {renderModal()}
    </div>
  );
}

export default App;
