#pragma once

#include <string>
#include <cstdint>

namespace chl {

// ─── Condition Types (pre-compiled, zero interpretation cost on hot path) ──
enum class ConditionType : uint8_t {
    PRICE_UP,          // price > prev_price
    PRICE_DOWN,        // price < prev_price
    PRICE_ABOVE_EMA,   // price > EMA(N)
    PRICE_BELOW_EMA,   // price < EMA(N)
    
    // Advanced Multi-Asset & Derivatives
    FUNDING_ARB_ENTRY,      // funding_rate > threshold for N ticks
    FUNDING_ARB_EXIT,       // funding_rate < 0
    PAIRS_Z_SCORE_ENTRY,    // z-score > threshold OR z-score < -threshold
    PAIRS_Z_SCORE_EXIT,     // z-score ~ 0
    DUAL_MOMENTUM_ENTRY,    // short_mom < 0 && long_trend > 0
    DUAL_MOMENTUM_EXIT,     // trailing stop or reversal
    MARGIN_SHORT_ENTRY,     // price < EMA_20 && negative mom
    MARGIN_SHORT_EXIT,      // target hit
    VOL_STRADDLE_ENTRY,     // fixed time / vol spike
    VOL_STRADDLE_EXIT,      // pnl target hit
    
    // Derivatives & Hedging strategies (from backtest notebook)
    PERP_SWAP_ENTRY,        // funding_rate_ma > 15%/yr + uptrend
    PERP_SWAP_EXIT,         // funding dried up or trend broke
    INVERSE_PERP_ENTRY,     // RSI > 70 + near BB upper + bull regime
    INVERSE_PERP_EXIT,      // RSI < 50 or price < EMA50
    SYNTHETIC_PUT_ENTRY,    // put_delta > 0.10 (RSI elevated + vol spike)
    SYNTHETIC_PUT_EXIT      // put_delta < 0.05 (normalisation)
};

// ─── Strategy Definition (immutable, parsed once at startup) ───────────────
// This struct is the pre-compiled result of parsing a JSON strategy file.
// It is passed by const-ref into StrategyInstance at construction and
// never modified during execution — zero allocation on the hot path.
struct StrategyDef {
    std::string   name;

    // Entry/exit logic
    ConditionType entry_cond   = ConditionType::PRICE_UP;
    ConditionType exit_cond    = ConditionType::PRICE_DOWN;
    
    // Core parameters
    double        ema_period   = 20.0;  
    double        threshold    = 0.01;  // For funding/z-score/margins
    int           counter_max  = 5;     // For sustained signals
    
    // Sizing
    double        size_btc     = 0.001; // Position size in BTC per signal

    // Hedging
    bool          hedge_enabled = false;
    double        hedge_ratio   = 1.0;  // 0.0–1.0 of base position
};

// ─── Human-readable label for logging ─────────────────────────────────────
inline const char* condition_name(ConditionType c) {
    switch (c) {
        case ConditionType::PRICE_UP:          return "price_up";
        case ConditionType::PRICE_DOWN:        return "price_down";
        case ConditionType::PRICE_ABOVE_EMA:   return "price_above_ema";
        case ConditionType::PRICE_BELOW_EMA:   return "price_below_ema";
        case ConditionType::FUNDING_ARB_ENTRY: return "funding_arb_entry";
        case ConditionType::FUNDING_ARB_EXIT:  return "funding_arb_exit";
        case ConditionType::PAIRS_Z_SCORE_ENTRY:return "pairs_z_score_entry";
        case ConditionType::PAIRS_Z_SCORE_EXIT: return "pairs_z_score_exit";
        case ConditionType::DUAL_MOMENTUM_ENTRY:return "dual_momentum_entry";
        case ConditionType::DUAL_MOMENTUM_EXIT: return "dual_momentum_exit";
        case ConditionType::MARGIN_SHORT_ENTRY: return "margin_short_entry";
        case ConditionType::MARGIN_SHORT_EXIT:  return "margin_short_exit";
        case ConditionType::VOL_STRADDLE_ENTRY: return "vol_straddle_entry";
        case ConditionType::VOL_STRADDLE_EXIT:  return "vol_straddle_exit";
        case ConditionType::PERP_SWAP_ENTRY:    return "perp_swap_entry";
        case ConditionType::PERP_SWAP_EXIT:     return "perp_swap_exit";
        case ConditionType::INVERSE_PERP_ENTRY: return "inverse_perp_entry";
        case ConditionType::INVERSE_PERP_EXIT:  return "inverse_perp_exit";
        case ConditionType::SYNTHETIC_PUT_ENTRY:return "synthetic_put_entry";
        case ConditionType::SYNTHETIC_PUT_EXIT: return "synthetic_put_exit";
        default:                               return "unknown";
    }
}

} // namespace chl
