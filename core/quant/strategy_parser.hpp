#pragma once

// ─── Minimal zero-dependency JSON parser for StrategyDef ──────────────────
// Hand-rolled to avoid any external library dependency. Supports only the
// subset of JSON needed for strategy config files:
//   - string values
//   - number values (integer and float)
//   - boolean values
//   - one level of nested objects
//
// Parsing happens once at startup. No allocations in the hot path.

#include "strategy_def.hpp"

#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <stdexcept>

namespace chl {

namespace detail {

// Trim leading/trailing whitespace
inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                          s.front() == '\n' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                          s.back() == '\n' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

// Extract value for a key in a flat JSON snippet (no recursive objects)
// Returns empty string_view if not found.
// e.g.  extract_value(json, "condition") → "price_above_ema"
inline std::string extract_string(std::string_view json, std::string_view key) {
    std::string search = "\"";
    search += key;
    search += "\"";
    auto pos = json.find(search);
    if (pos == std::string_view::npos) return {};
    pos = json.find(':', pos + search.size());
    if (pos == std::string_view::npos) return {};
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return {};
    if (json[pos] == '"') {
        pos++;
        auto end = json.find('"', pos);
        if (end == std::string_view::npos) return {};
        return std::string(json.substr(pos, end - pos));
    }
    // non-string scalar
    auto end = json.find_first_of(",}\n", pos);
    return std::string(trim(json.substr(pos, end - pos)));
}

// Extract a nested JSON object block for a given key
inline std::string extract_object(std::string_view json, std::string_view key) {
    std::string search = "\"";
    search += key;
    search += "\"";
    auto pos = json.find(search);
    if (pos == std::string_view::npos) return {};
    pos = json.find('{', pos + search.size());
    if (pos == std::string_view::npos) return {};
    // Find matching closing brace (handles 1-level nesting)
    int depth = 0;
    auto start = pos;
    for (; pos < json.size(); ++pos) {
        if (json[pos] == '{') depth++;
        else if (json[pos] == '}') { depth--; if (depth == 0) break; }
    }
    return std::string(json.substr(start, pos - start + 1));
}

inline ConditionType parse_condition(const std::string& s) {
    if (s == "price_up")          return ConditionType::PRICE_UP;
    if (s == "price_down")        return ConditionType::PRICE_DOWN;
    if (s == "price_above_ema")   return ConditionType::PRICE_ABOVE_EMA;
    if (s == "price_below_ema")   return ConditionType::PRICE_BELOW_EMA;
    if (s == "funding_arb_entry") return ConditionType::FUNDING_ARB_ENTRY;
    if (s == "funding_arb_exit")  return ConditionType::FUNDING_ARB_EXIT;
    if (s == "pairs_z_score_entry") return ConditionType::PAIRS_Z_SCORE_ENTRY;
    if (s == "pairs_z_score_exit")  return ConditionType::PAIRS_Z_SCORE_EXIT;
    if (s == "dual_momentum_entry") return ConditionType::DUAL_MOMENTUM_ENTRY;
    if (s == "dual_momentum_exit")  return ConditionType::DUAL_MOMENTUM_EXIT;
    if (s == "margin_short_entry")  return ConditionType::MARGIN_SHORT_ENTRY;
    if (s == "margin_short_exit")   return ConditionType::MARGIN_SHORT_EXIT;
    if (s == "vol_straddle_entry")  return ConditionType::VOL_STRADDLE_ENTRY;
    if (s == "vol_straddle_exit")   return ConditionType::VOL_STRADDLE_EXIT;
    if (s == "perp_swap_entry")     return ConditionType::PERP_SWAP_ENTRY;
    if (s == "perp_swap_exit")      return ConditionType::PERP_SWAP_EXIT;
    if (s == "inverse_perp_entry")  return ConditionType::INVERSE_PERP_ENTRY;
    if (s == "inverse_perp_exit")   return ConditionType::INVERSE_PERP_EXIT;
    if (s == "synthetic_put_entry") return ConditionType::SYNTHETIC_PUT_ENTRY;
    if (s == "synthetic_put_exit")  return ConditionType::SYNTHETIC_PUT_EXIT;
    std::fprintf(stderr, "[Parser] Unknown condition: '%s', defaulting to price_up\n", s.c_str());
    return ConditionType::PRICE_UP;
}

} // namespace detail

// ─── StrategyParser ───────────────────────────────────────────────────────
class StrategyParser {
public:
    // Parse a JSON file on disk into a StrategyDef.
    // Throws std::runtime_error if the file cannot be opened.
    static StrategyDef parse_file(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open())
            throw std::runtime_error("[StrategyParser] Cannot open: " + path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return parse_string(ss.str());
    }

    static StrategyDef parse_string(const std::string& json) {
        using namespace detail;
        StrategyDef def;

        // Name
        def.name = extract_string(json, "name");
        if (def.name.empty()) def.name = "unnamed";

        // Entry block
        auto entry_block = extract_object(json, "entry");
        if (!entry_block.empty()) {
            def.entry_cond = parse_condition(extract_string(entry_block, "condition"));
            auto p = extract_string(entry_block, "param");
            if (!p.empty()) def.ema_period = std::stod(p);
        }

        // Exit block
        auto exit_block = extract_object(json, "exit");
        if (!exit_block.empty()) {
            def.exit_cond = parse_condition(extract_string(exit_block, "condition"));
            // Allow exit to override EMA period if specified
            auto p = extract_string(exit_block, "param");
            if (!p.empty()) def.ema_period = std::stod(p);
        }

        // Size
        auto size_str = extract_string(json, "size");
        if (!size_str.empty()) def.size_btc = std::stod(size_str);

        // Hedge block
        auto hedge_block = extract_object(json, "hedge");
        if (!hedge_block.empty()) {
            auto enabled_str = extract_string(hedge_block, "enabled");
            def.hedge_enabled = (enabled_str == "true" || enabled_str == "1");
            auto ratio_str   = extract_string(hedge_block, "ratio");
            if (!ratio_str.empty()) def.hedge_ratio = std::stod(ratio_str);
        }

        return def;
    }
};

} // namespace chl
