export interface Position {
  qty: number;
  avg_price: number;
}

export interface PnL {
  realized: number;
  unrealized: number;
}

export interface Latency {
  fs: number; // feed_to_strategy
  se: number; // strategy_to_execution
  tot: number; // total
}

export interface Trade {
  side: 'BUY' | 'SELL';
  price: number;
  qty: number;
  seq: number;
  strategy_name?: string;
}

export interface StrategySummary {
  name: string;
  qty: number;
  avg: number;
  pnl_r: number;
  pnl_u: number;
}

export interface Metrics {
  price: number;
  bid: number;
  ask: number;
  latency: Latency;
  strategies: StrategySummary[];
  trades: Trade[];
}
