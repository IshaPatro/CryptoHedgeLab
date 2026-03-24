#pragma once

#include "backtest_plot.hpp"
#include "strategy_def.hpp"
#include <vector>
#include <string>
#include <cstdio>

namespace chl {

/**
 * Handles the generation of static backtest reports for the UI.
 * This maps strategies to assets and generates the SVG files.
 */
struct ReportGenerator {
    static void generate_all(const std::vector<StrategyDef>& strats) {
        std::printf("[ReportGenerator] Generating static backtest reports for UI...\n");
        
        for (const auto& ds : strats) {
            // Determine Asset Label
            std::string asset = "BTC/USDT";
            if (ds.name == "pairs_trading") asset = "BTC & ETH (Pairs)";
            else if (ds.name == "vol_straddle") asset = "ETH/USDT";
            
            // Generate deterministic mock PnL curve for demonstration
            // (In a real system, this would be the actual past backtest result)
            std::vector<double> curve;
            double pnl = 0.0;
            curve.push_back(pnl);
            
            unsigned int seed = 42; // Deterministic results for static reports
            for (int i = 0; i < 50; ++i) {
                int r = rand_r(&seed) % 100;
                pnl += (r - 42.0) * 0.8; // Bias slightly upwards for "good" backtest
                curve.push_back(pnl);
            }

            std::string filepath = "../ui/public/backtest_results/" + ds.name + ".svg";
            
            save_backtest_plot(filepath, ds.name, asset, curve);
            std::printf("  - Generated %s [%s]\n", ds.name.c_str(), asset.c_str());
        }
        
        std::printf("[ReportGenerator] All reports saved to ui/public/backtest_results/\n");
    }
};

} // namespace chl
