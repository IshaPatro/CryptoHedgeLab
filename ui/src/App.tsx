import { useEffect, useState, useRef } from 'react';
import type { Metrics } from './types';
import { Activity, Clock, DollarSign, ArrowRightLeft } from 'lucide-react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, ReferenceLine } from 'recharts';

function App() {
  const [metrics, setMetrics] = useState<Metrics | null>(null);
  const [connected, setConnected] = useState(false);
  
  // Keep history for chart
  const [pnlHistory, setPnlHistory] = useState<{ time: string; total: number }[]>([]);
  const wsRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    const connectWS = () => {
      const ws = new WebSocket('ws://localhost:8080');
      
      ws.onopen = () => {
        setConnected(true);
        console.log('Connected to backend');
      };

      ws.onmessage = (event) => {
        try {
          const data: Metrics = JSON.parse(event.data);
          setMetrics(data);
          
          // Update Chart
          const totalPnL = data.pnl.realized + data.pnl.unrealized;
          setPnlHistory(prev => {
            const time = new Date().toLocaleTimeString('en-US', { hour12: false, hour: '2-digit', minute:'2-digit', second:'2-digit' });
            const newPoint = { time, total: totalPnL };
            const newHistory = [...prev, newPoint];
            if (newHistory.length > 50) return newHistory.slice(newHistory.length - 50); // Keep last 50 points
            return newHistory;
          });
          
        } catch (e) {
          console.error("Parse error:", e);
        }
      };

      ws.onclose = () => {
        setConnected(false);
        // Attempt reconnect after 2s
        setTimeout(connectWS, 2000);
      };

      wsRef.current = ws;
    };

    connectWS();
    return () => {
      wsRef.current?.close();
    };
  }, []);

  if (!metrics) {
    return (
      <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', height: '100vh' }}>
        <h2 style={{ color: 'var(--text-muted)' }}>Waiting for tick data from C++ Engine...</h2>
      </div>
    );
  }

  const { price, bid, ask, position, pnl, latency, trades } = metrics;
  const isUp = price > bid; // Simple visual heuristic

  return (
    <div className="dashboard-container">
      
      <header className="top-bar">
        <div style={{ display: 'flex', alignItems: 'center', gap: '1rem' }}>
          <Activity color="var(--accent-color)" size={28} />
          <h2>CryptoHedgeLab <span className="text-accent">Module 4</span></h2>
        </div>
        <div className={`status-indicator ${connected ? 'status-connected' : 'status-disconnected'}`}>
          <div className="status-dot"></div>
          {connected ? 'LIVE (ws://localhost:8080)' : 'DISCONNECTED'}
        </div>
      </header>

      <main className="main-grid">
        {/* Left Side: Stats and Price (Top) + Chart (Middle) */}
        <section className="chart-section" style={{ display: 'flex', flexDirection: 'column', gap: '1.5rem' }}>
          
          <div style={{ display: 'flex', gap: '1.5rem' }}>
            {/* Price Panel */}
            <div className="glass-panel" style={{ flex: 1 }}>
              <div className="panel-title">BTCUSDT L1 </div>
              <div className="price-display">
                <span className={`main-price ${isUp ? 'text-up' : 'text-down'}`}>
                  ${price.toFixed(2)}
                </span>
                <div className="bid-ask">
                  <span style={{ color: 'var(--text-muted)' }}>Bid: <span style={{ color: 'var(--text-main)'}}>{bid.toFixed(2)}</span></span>
                  <span style={{ color: 'var(--text-muted)' }}>Ask: <span style={{ color: 'var(--text-main)'}}>{ask.toFixed(2)}</span></span>
                </div>
              </div>
            </div>

            {/* Position Panel */}
            <div className="glass-panel" style={{ flex: 1 }}>
              <div className="panel-title"><ArrowRightLeft size={16} /> Net Position</div>
              <div>
                <span className={`metric-value ${position.qty > 0 ? 'text-up' : position.qty < 0 ? 'text-down' : ''}`}>
                  {position.qty === 0 ? 'FLAT' : `${position.qty > 0 ? '+' : ''}${position.qty.toFixed(6)} BTC`}
                </span>
                {position.qty !== 0 && (
                  <div className="metric-label">Avg Entry: ${position.avg_price.toFixed(2)}</div>
                )}
              </div>
            </div>

            {/* PnL Panel */}
            <div className="glass-panel" style={{ flex: 1 }}>
              <div className="panel-title"><DollarSign size={16} /> Total PnL</div>
              <div>
                <span className={`metric-value ${(pnl.realized + pnl.unrealized) >= 0 ? 'text-up' : 'text-down'}`}>
                  ${(pnl.realized + pnl.unrealized).toFixed(4)}
                </span>
                <div className="metric-label">
                  Realized: ${pnl.realized.toFixed(4)} <br/>
                  Unreal: ${pnl.unrealized.toFixed(4)}
                </div>
              </div>
            </div>
          </div>

          {/* Chart Panel */}
          <div className="glass-panel" style={{ flex: 1, minHeight: '300px', display: 'flex', flexDirection: 'column' }}>
            <div className="panel-title">Cumulative PnL (Live)</div>
            <div style={{ flex: 1, width: '100%', marginLeft: '-20px' }}>
              <ResponsiveContainer width="100%" height="100%">
                <LineChart data={pnlHistory}>
                  <CartesianGrid strokeDasharray="3 3" stroke="var(--border-color)" />
                  <XAxis dataKey="time" stroke="var(--text-muted)" fontSize={11} tickMargin={10} minTickGap={30} />
                  <YAxis stroke="var(--text-muted)" fontSize={11} domain={['auto', 'auto']} tickFormatter={(val) => `$${val.toFixed(4)}`} />
                  <Tooltip 
                    contentStyle={{ backgroundColor: 'var(--bg-color)', border: '1px solid var(--border-color)' }}
                    itemStyle={{ color: 'var(--text-main)' }}
                    formatter={(val: any) => [`$${Number(val).toFixed(4)}`, 'PnL']}
                  />
                  <ReferenceLine y={0} stroke="rgba(255,255,255,0.2)" />
                  <Line 
                    type="stepAfter" 
                    dataKey="total" 
                    stroke="var(--accent-color)" 
                    strokeWidth={2}
                    dot={false}
                    isAnimationActive={false}
                  />
                </LineChart>
              </ResponsiveContainer>
            </div>
          </div>

        </section>

        {/* Right Side: Latency */}
        <section className="stats-section">
          <div className="glass-panel" style={{ height: '100%' }}>
             <div className="panel-title"><Clock size={16} /> Hot-Path Latency</div>
             <p style={{ fontSize:'0.85rem', color: 'var(--text-muted)', marginBottom: '1.5rem' }}>
               Hardware clock measurements from feed ingress to paper execution (in microseconds).
             </p>
             <div className="latency-grid">
               
               <div className={`latency-item ${latency.feed_to_strategy < 5 ? 'fast' : 'slow'}`}>
                 <span style={{ fontSize: '0.85rem', color: 'var(--text-muted)'}}>Feed → Strategy</span>
                 <span className="latency-val">{latency.feed_to_strategy.toFixed(1)} µs</span>
               </div>
               
               <div className={`latency-item ${latency.strategy_to_execution < 5 ? 'fast' : 'slow'}`}>
                 <span style={{ fontSize: '0.85rem', color: 'var(--text-muted)'}}>Strategy → Exec</span>
                 <span className="latency-val">{latency.strategy_to_execution.toFixed(1)} µs</span>
               </div>
               
               <div className={`latency-item ${latency.end_to_end < 10 ? 'fast' : 'slow'}`}>
                 <span style={{ fontSize: '0.85rem', color: 'var(--text-muted)'}}>End-to-End</span>
                 <span className="latency-val" style={{ color: 'var(--accent-color)'}}>{latency.end_to_end.toFixed(1)} µs</span>
               </div>

             </div>
          </div>
        </section>

        {/* Bottom Panel: Trade Log */}
        <section className="trades-section">
          <div className="glass-panel">
            <div className="panel-title">Recent Fills</div>
            <div style={{ maxHeight: '200px', overflowY: 'auto' }}>
              <table className="trades-table">
                <thead>
                  <tr>
                    <th>Seq Number</th>
                    <th>Side</th>
                    <th>Price</th>
                    <th>Quantity (BTC)</th>
                  </tr>
                </thead>
                <tbody>
                  {trades.length === 0 ? (
                    <tr><td colSpan={4} style={{ textAlign: 'center', color: 'var(--text-muted)'}}>No recent trades</td></tr>
                  ) : (
                    trades.map((t, idx) => (
                      <tr key={`${t.seq}-${idx}`} className="row-flash">
                        <td style={{ color: 'var(--text-muted)'}}>#{t.seq}</td>
                        <td className={t.side === 'BUY' ? 'text-up' : 'text-down'}>{t.side}</td>
                        <td>${t.price.toFixed(2)}</td>
                        <td>{t.qty.toFixed(6)}</td>
                      </tr>
                    ))
                  )}
                </tbody>
              </table>
            </div>
          </div>
        </section>

      </main>
    </div>
  );
}

export default App;
