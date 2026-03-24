export interface Position {
  qty: number;
  avg_price: number;
}

export interface PnL {
  realized: number;
  unrealized: number;
}

export interface Latency {
  feed_to_strategy: number;
  strategy_to_execution: number;
  end_to_end: number;
}

export interface Trade {
  side: 'BUY' | 'SELL';
  price: number;
  qty: number;
  seq: number;
}

export interface Metrics {
  strategy_name?: string;
  price: number;
  bid: number;
  ask: number;
  position: Position;
  pnl: PnL;
  latency: Latency;
  trades: Trade[];
}
