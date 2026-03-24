import { useEffect, useState, useRef, useMemo } from 'react';
import type { Metrics } from './types';
import { Activity, Clock, ArrowRightLeft, Beaker, BarChart2, X } from 'lucide-react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, ReferenceLine } from 'recharts';

/* ═══════════════════════════════════════════════════════
   Strategy/Asset Metadata
   ═══════════════════════════════════════════════════════ */
interface StrategyMeta {
  asset: string;
  instType: string;
  legs: string;
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

const PERFORMANCE_STATS: Record<string, any> = {
  momentum:           { roi: 101.08, cagr: 11.87, sharpe: 0.72, max_dd: -31.26, win_rate: 35.78, trades: 626 },
  funding_arbitrage:  { roi: 347.26, cagr: 27.19, sharpe: 1.17, max_dd: -36.49, win_rate: 46.08, trades: 332 },
  pairs_trading:      { roi: 33.06,  cagr: 4.69,  sharpe: 0.55, max_dd: -17.27, win_rate: 54.55, trades: 253 },
  dual_momentum:      { roi: 168.39, cagr: 17.18, sharpe: 0.74, max_dd: -46.53, win_rate: 39.00, trades: 1064 },
  margin_short:       { roi: 0.62,   cagr: 0.10,  sharpe: 0.22, max_dd: -0.88,  win_rate: 50.00, trades: 2 },
  vol_straddle:       { roi: 19.56,  cagr: 2.91,  sharpe: 0.45, max_dd: -15.67, win_rate: 50.00, trades: 152 },
  perp_swap_hedge:    { roi: 27.13,  cagr: 3.93,  sharpe: 9.57, max_dd: -0.56,  win_rate: 78.05, trades: 164 },
  inverse_perp_hedge: { roi: 1178.63,cagr: 50.56, sharpe: 0.94, max_dd: -77.88, win_rate: 43.47, trades: 352 },
  synthetic_put:      { roi: 4333.66,cagr: 83.83, sharpe: 1.31, max_dd: -72.60, win_rate: 52.25, trades: 289 },
};

const COLORS = [
  '#3b82f6', '#10b981', '#ef4444', '#f59e0b', '#8b5cf6', 
  '#ec4899', '#06b6d4', '#f97316', '#a855f7'
];

function OrderBook({ bid, ask }: { bid: number, ask: number }) {
  const [spread, setSpread] = useState(0);
  useEffect(() => {
    if (bid > 0 && ask > 0) setSpread(ask - bid);
  }, [bid, ask]);

  const generateLevels = (base: number, step: number, count: number, isAsk: boolean) => {
    return Array.from({ length: count }).map((_, i) => {
      const p = isAsk ? base + (i * step) : base - (i * step);
      const q = Math.random() * 0.5 + 0.1;
      return { price: p, qty: q };
    }).sort((a, b) => b.price - a.price); // Both lists show descending price
  };

  const asks = useMemo(() => generateLevels(ask, 0.5, 5, true), [ask]);
  const bids = useMemo(() => generateLevels(bid, 0.5, 5, false), [bid]);

  return (
    <div className="glass-panel" style={{ padding: '1rem', height: '100%', boxSizing: 'border-box' }}>
      <div className="panel-title"><Activity size={14} /> ORDERBOOK (BTC/USDT)</div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: '2px' }}>
        {asks.map((a, i) => (
          <div key={i} className="orderbook-row ask">
            <span>{a.price.toFixed(1)}</span>
            <span>{a.qty.toFixed(3)}</span>
          </div>
        ))}
        <div className="orderbook-spread" style={{ fontWeight: 700, color: 'var(--accent-color)' }}>
          Spread: {spread.toFixed(1)} ({ask > 0 ? ((spread / ask) * 100).toFixed(3) : 0}%)
        </div>
        {bids.map((b, i) => (
          <div key={i} className="orderbook-row bid">
            <span>{b.price.toFixed(1)}</span>
            <span>{b.qty.toFixed(3)}</span>
          </div>
        ))}
      </div>
    </div>
  );
}

function App() {
  const [activeTab, setActiveTab] = useState<'live' | 'lab'>('live');
  const [metrics, setMetrics]     = useState<Metrics | null>(null);
  const [connected, setConnected] = useState(false);
  const [pnlHistory, setPnlHistory] = useState<any[]>([]);
  const lastFillCount = useRef(-1);
  const wsRef = useRef<WebSocket | null>(null);
  const [modalStrategy, setModalStrategy] = useState<any>(null);

  useEffect(() => {
    const connectWS = () => {
      const ws = new WebSocket('ws://localhost:8080');
      ws.onopen = () => setConnected(true);
      ws.onmessage = (event) => {
        try {
          const data: Metrics = JSON.parse(event.data);
          setMetrics(data);
          
          const currentFills = data.trades?.length ?? 0;
          if (currentFills !== lastFillCount.current) {
            lastFillCount.current = currentFills;
            const time = new Date().toLocaleTimeString('en-US', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
            
            setPnlHistory(prev => {
              const entry: any = { time };
              data.strategies.forEach(s => {
                const totalPnL = s.pnl_r + s.pnl_u;
                entry[s.name] = (totalPnL / 1000.0) * 100;
              });
              const next = [...prev, entry];
              return next.length > 100 ? next.slice(next.length - 100) : next;
            });
          }
        } catch (_) {}
      };
      ws.onclose = () => { setConnected(false); setTimeout(connectWS, 2000); };
      wsRef.current = ws;
    };
    connectWS();
    return () => wsRef.current?.close();
  }, []);

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

  const renderLive = () => {
    if (!metrics) return (
      <div style={{ textAlign: 'center', color: 'var(--text-muted)', marginTop: '3rem' }}>
        <h2>Waiting for tick data from C++ engine…</h2>
      </div>
    );

    const { bid, ask, latency, strategies, trades } = metrics;
    
    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: '1.5rem', paddingBottom: '2rem' }}>
        <main style={{ display: 'grid', gridTemplateColumns: 'minmax(250px, 1fr) 2fr 1.2fr', gap: '1.5rem' }}>
          
          <div style={{ minHeight: '350px' }}>
            <OrderBook bid={bid} ask={ask} />
          </div>

          <div className="glass-panel" style={{ display: 'flex', flexDirection: 'column', minHeight: '350px' }}>
            <div className="panel-title" style={{ justifyContent: 'space-between' }}>
              <span><BarChart2 size={14} /> MULTI-STRATEGY PnL COMPARISON (%)</span>
              <span style={{ fontSize: '0.7rem', color: 'var(--text-muted)' }}>Capital Basis: $1000/Strat</span>
            </div>
            <div style={{ flex: 1, width: '100%', marginLeft: '-20px' }}>
              {pnlHistory.length === 0 ? (
                <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', color: 'var(--text-muted)' }}>
                  Awaiting real-time strategy signals…
                </div>
              ) : (
                <ResponsiveContainer width="100%" height={300}>
                  <LineChart data={pnlHistory}>
                    <CartesianGrid strokeDasharray="3 3" stroke="var(--border-color)" />
                    <XAxis dataKey="time" stroke="var(--text-muted)" fontSize={10} tickMargin={6} minTickGap={30} />
                    <YAxis stroke="var(--text-muted)" fontSize={10} domain={['auto', 'auto']} tickFormatter={v => `${v.toFixed(2)}%`} />
                    <Tooltip contentStyle={{ backgroundColor: 'var(--bg-color)', border: '1px solid var(--border-color)', fontSize: '10px' }} />
                    <ReferenceLine y={0} stroke="rgba(255,255,255,0.3)" strokeDasharray="4 4" />
                    {Object.keys(STRATEGY_META).map((s, i) => (
                      <Line key={s} type="monotone" dataKey={s} stroke={COLORS[i % COLORS.length]} strokeWidth={2} dot={false} isAnimationActive={false} />
                    ))}
                  </LineChart>
                </ResponsiveContainer>
              )}
            </div>
          </div>

          <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem' }}>
            <div className="glass-panel" style={{ padding: '0.75rem' }}>
              <div className="panel-title" style={{ fontSize: '0.75rem' }}><Activity size={14} /> LIVE PERFORMANCE</div>
              <table className="trades-table" style={{ width: '100%', fontSize: '0.75rem' }}>
                <thead>
                  <tr>
                    <th>Strategy</th>
                    <th style={{ textAlign: 'right' }}>PnL %</th>
                  </tr>
                </thead>
                <tbody>
                  {strategies.map((s, i) => {
                    const pnlP = ((s.pnl_r + s.pnl_u) / 1000.0) * 100;
                    return (
                      <tr key={s.name} style={{ borderBottom: '1px solid var(--border-color)' }}>
                        <td style={{ display: 'flex', alignItems: 'center', gap: '4px', padding: '4px 0' }}>
                          <div style={{ width: '8px', height: '8px', borderRadius: '50%', background: COLORS[i % COLORS.length] }} />
                          {s.name.replace(/_/g, ' ').toUpperCase()}
                        </td>
                        <td className={pnlP > 0 ? 'text-up' : pnlP < 0 ? 'text-down' : ''} style={{ textAlign: 'right', fontWeight: 'bold' }}>
                          {pnlP.toFixed(3)}%
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
            <div className="glass-panel" style={{ padding: '0.75rem' }}>
              <div className="panel-title" style={{ marginBottom: '0.5rem' }}><Clock size={12} /> Pipe Latency</div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '4px 8px', background: 'rgba(255,255,255,0.03)', borderRadius: '4px' }}>
                  <span style={{ fontSize: '0.65rem', color: 'var(--text-muted)' }}>Feed → Strat</span>
                  <span style={{ fontSize: '0.9rem', color: 'var(--accent-color)', fontWeight: 700 }}>{latency.fs.toFixed(1)} µs</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '4px 8px', background: 'rgba(255,255,255,0.03)', borderRadius: '4px' }}>
                  <span style={{ fontSize: '0.65rem', color: 'var(--text-muted)' }}>Strat → Exec</span>
                  <span style={{ fontSize: '0.9rem', color: 'var(--accent-color)', fontWeight: 700 }}>{latency.se.toFixed(1)} µs</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '6px 8px', background: 'rgba(59,130,246,0.1)', borderRadius: '4px' }}>
                  <span style={{ fontSize: '0.7rem', fontWeight: 600 }}>Total E2E</span>
                  <span style={{ fontSize: '1.1rem', color: 'var(--accent-color)', fontWeight: 900 }}>{latency.tot.toFixed(1)} µs</span>
                </div>
              </div>
            </div>
          </div>
        </main>

        <div className="glass-panel" style={{ display: 'flex', flexDirection: 'column' }}>
          <div className="panel-title"><ArrowRightLeft size={14} /> GLOBAL EXECUTION LOG</div>
          <table className="trades-table" style={{ width: '100%', fontSize: '0.8rem' }}>
            <thead>
              <tr style={{ borderBottom: '2px solid var(--border-color)' }}>
                <th style={{ textAlign: 'left', padding: '8px' }}>Seq</th>
                <th style={{ textAlign: 'left', padding: '8px' }}>Strategy</th>
                <th style={{ textAlign: 'left', padding: '8px' }}>Side</th>
                <th style={{ textAlign: 'right', padding: '8px' }}>Price</th>
                <th style={{ textAlign: 'right', padding: '8px' }}>Qty (BTC)</th>
              </tr>
            </thead>
            <tbody>
              {trades.map((t, i) => (
                <tr key={`${t.seq}-${i}`} className={i === 0 ? 'row-flash' : ''} style={{ borderBottom: '1px solid var(--border-color)' }}>
                  <td style={{ color: 'var(--text-muted)', padding: '8px' }}>{t.seq}</td>
                  <td style={{ fontWeight: 600, padding: '8px' }}>{t.strategy_name?.toUpperCase().replace(/_/g, ' ')}</td>
                  <td className={t.side === 'BUY' ? 'text-up' : 'text-down'} style={{ fontWeight: 'bold', padding: '8px' }}>{t.side}</td>
                  <td style={{ fontFamily: 'monospace', textAlign: 'right', padding: '8px' }}>${t.price.toFixed(2)}</td>
                  <td style={{ fontFamily: 'monospace', textAlign: 'right', padding: '8px' }}>{t.qty.toFixed(6)}</td>
                </tr>
              ))}
              {trades.length === 0 && (
                <tr>
                  <td colSpan={5} style={{ textAlign: 'center', padding: '2rem', color: 'var(--text-muted)' }}>No live executions recorded.</td>
                </tr>
              )}
            </tbody>
          </table>
        </div>
      </div>
    );
  };

  const renderLab = () => (
    <div style={{ display: 'flex', flexDirection: 'column', gap: '1.5rem', paddingBottom: '2rem' }}>
      <div className="glass-panel" style={{ padding: '1rem', overflowX: 'auto' }}>
        <div className="panel-title" style={{ marginBottom: '1.5rem', color: 'var(--accent-color)', fontSize: '1rem' }}>
          <Beaker size={18} /> STRATEGY RESEARCH & PERFORMANCE LIBRARY
        </div>
        <table className="trades-table" style={{ width: '100%', fontSize: '0.85rem', borderCollapse: 'collapse' }}>
          <thead>
            <tr style={{ borderBottom: '2px solid var(--border-color)', background: 'rgba(255,255,255,0.02)' }}>
              <th style={{ textAlign: 'left', padding: '12px 10px' }}>Strategy Asset</th>
              <th style={{ textAlign: 'left', padding: '12px 10px' }}>Type</th>
              <th style={{ textAlign: 'right', padding: '12px 10px' }}>ROI (%)</th>
              <th style={{ textAlign: 'right', padding: '12px 10px' }}>CAGR (%)</th>
              <th style={{ textAlign: 'right', padding: '12px 10px' }}>Sharpe</th>
              <th style={{ textAlign: 'right', padding: '12px 10px' }}>Max DD</th>
              <th style={{ textAlign: 'right', padding: '12px 10px' }}>Win Rate</th>
              <th style={{ textAlign: 'right', padding: '12px 10px' }}>Trades</th>
              <th style={{ textAlign: 'right', padding: '12px 10px' }}>Visuals</th>
            </tr>
          </thead>
          <tbody>
            {Object.entries(STRATEGY_META).map(([name, meta]) => {
              const stats = PERFORMANCE_STATS[name] || { roi:0, cagr:0, sharpe:0, max_dd:0, win_rate:0, trades:0 };
              return (
                <tr key={name} style={{ borderBottom: '1px solid var(--border-color)' }}>
                  <td style={{ padding: '15px 10px' }}>
                    <div style={{ fontWeight: 700, fontSize: '0.9rem' }}>{name.toUpperCase().replace(/_/g, ' ')}</div>
                    <div style={{ fontSize: '0.7rem', color: 'var(--text-muted)' }}>{meta.asset}</div>
                  </td>
                  <td style={{ padding: '15px 10px' }}>
                    <span style={{ fontSize: '0.7rem', background: 'rgba(59,130,246,0.1)', color: 'var(--accent-color)', padding: '2px 6px', borderRadius: '3px', fontWeight: 600 }}>
                      {meta.instType}
                    </span>
                  </td>
                  <td style={{ textAlign: 'right', padding: '15px 10px', fontWeight: 700 }} className="text-up">{stats.roi.toFixed(2)}%</td>
                  <td style={{ textAlign: 'right', padding: '15px 10px' }}>{stats.cagr.toFixed(2)}%</td>
                  <td style={{ textAlign: 'right', padding: '15px 10px', color: 'var(--accent-color)', fontWeight: 700 }}>{stats.sharpe.toFixed(2)}</td>
                  <td style={{ textAlign: 'right', padding: '15px 10px' }} className="text-down">{stats.max_dd.toFixed(1)}%</td>
                  <td style={{ textAlign: 'right', padding: '15px 10px' }}>{stats.win_rate.toFixed(1)}%</td>
                  <td style={{ textAlign: 'right', padding: '15px 10px', color: 'var(--text-muted)' }}>{stats.trades}</td>
                  <td style={{ textAlign: 'right', padding: '15px 10px' }}>
                    <button onClick={() => setModalStrategy({ name, ...meta })} style={{ background: 'var(--accent-color)', color: '#fff', border: 'none', padding: '6px 14px', borderRadius: '4px', cursor: 'pointer', fontSize: '0.75rem', fontWeight: 600 }}>
                      VIEW BACKTESTING RESULTS
                    </button>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </div>
  );

  const renderModal = () => {
    if (!modalStrategy) return null;
    const name = modalStrategy.name;
    return (
      <div className="modal-overlay" onClick={() => setModalStrategy(null)}>
        <div className="modal-content" onClick={e => e.stopPropagation()} style={{ maxWidth: '900px', width: '90%' }}>
          <button className="modal-close" onClick={() => setModalStrategy(null)}><X size={20} /></button>
          <h2 style={{ marginTop: 0, textTransform: 'uppercase', fontSize: '1.2rem', color: 'var(--accent-color)', borderBottom: '1px solid var(--border-color)', paddingBottom: '1rem' }}>
            {name.replace(/_/g, ' ')} · Backtest Results
          </h2>
          <div style={{ background: '#000', borderRadius: '8px', overflow: 'hidden', border: '1px solid var(--border-color)' }}>
            <img 
              src={`/backtest_results/${name}.png`} 
              alt={name} 
              style={{ width: '100%', display: 'block' }}
              onError={(e) => {
                (e.target as HTMLImageElement).src = `https://via.placeholder.com/1200x600/0d1117/238636?text=${name}+Backtest+-+Awaiting+Data`;
              }}
            />
          </div>
          <div style={{ display: 'flex', gap: '2rem', marginTop: '1.5rem', fontSize: '0.9rem' }}>
            <div><span style={{ color: 'var(--text-muted)' }}>Instrument:</span> <strong>{modalStrategy.instType}</strong></div>
            <div><span style={{ color: 'var(--text-muted)' }}>Primary Asset:</span> <strong>{modalStrategy.asset}</strong></div>
            <div style={{ marginLeft: 'auto' }}><span style={{ color: 'var(--text-muted)' }}>Expertise:</span> <strong style={{ color: 'var(--accent-color)' }}>QUANT STRAT</strong></div>
          </div>
        </div>
      </div>
    );
  };

  return (
    <div className="dashboard-container" style={{ padding: '1rem 1.5rem', display: 'flex', flexDirection: 'column', height: '100vh', overflow: 'hidden' }}>
      <header className="top-bar" style={{ flexShrink: 0 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.75rem' }}>
          <Activity color="var(--accent-color)" size={24} />
          <h2 style={{ margin: 0, fontSize: '1.2rem' }}>CryptoHedgeLab</h2>
        </div>
        <div className={`status-indicator ${connected ? 'status-connected' : 'status-disconnected'}`}>
          <div className="status-dot" />
          {connected ? 'LIVE ENGINE' : 'OFFLINE'}
        </div>
      </header>

      <div style={{ flexShrink: 0, marginBottom: '1rem' }}>
        {renderTabs()}
      </div>

      <div style={{ flex: 1, overflowY: 'auto', paddingRight: '0.5rem' }} className="custom-scrollbar">
        {activeTab === 'live' ? renderLive() : renderLab()}
      </div>
      
      {renderModal()}
    </div>
  );
}

export default App;
