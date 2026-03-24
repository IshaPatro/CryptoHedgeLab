#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace chl {

/**
 * A ultra-lightweight SVG sparkline generator for strategy performance.
 */
inline void save_backtest_plot(const std::string& filepath, 
                             const std::string& strategy_name, 
                             const std::string& asset_name,
                             const std::vector<double>& pnl_curve) {
    if (pnl_curve.empty()) return;

    double min_v = pnl_curve[0];
    double max_v = pnl_curve[0];
    for (double v : pnl_curve) {
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }

    double range = max_v - min_v;
    if (range < 0.1) range = 0.1;

    int width = 800;
    int height = 500;
    int padding_x = 70;
    int padding_y = 60;

    std::ofstream ofs(filepath);
    ofs << "<svg viewBox=\"0 0 " << width << " " << height << "\" xmlns=\"http://www.w3.org/2000/svg\">\n";
    ofs << "  <rect width=\"100%\" height=\"100%\" fill=\"#0d1117\" rx=\"8\" />\n";
    
    // Grid & Axis
    ofs << "  <g stroke=\"#30363d\" stroke-width=\"1\">\n";
    for (int i = 0; i <= 4; ++i) {
        int y = padding_y + (height - 2 * padding_y) * i / 4;
        ofs << "    <line x1=\"" << padding_x << "\" y1=\"" << y << "\" x2=\"" << (width - padding_x) << "\" y2=\"" << y << "\" />\n";
        double pnl_val = max_v - (max_v - min_v) * i / 4;
        ofs << "    <text x=\"" << (padding_x - 10) << "\" y=\"" << (y + 4) << "\" fill=\"#8b949e\" font-family=\"monospace\" font-size=\"12\" text-anchor=\"end\">" 
            << std::fixed << std::setprecision(1) << pnl_val << "%</text>\n";
    }
    for (int i = 0; i <= 4; ++i) {
        int x = padding_x + (width - 2 * padding_x) * i / 4;
        ofs << "    <line x1=\"" << x << "\" y1=\"" << padding_y << "\" x2=\"" << x << "\" y2=\"" << (height - padding_y) << "\" />\n";
        ofs << "    <text x=\"" << x << "\" y=\"" << (height - padding_y + 20) << "\" fill=\"#8b949e\" font-family=\"monospace\" font-size=\"12\" text-anchor=\"middle\">T" << i * 100 << "</text>\n";
    }
    ofs << "  </g>\n";

    // PnL Curve
    ofs << "  <polyline fill=\"none\" stroke=\"#238636\" stroke-width=\"3\" stroke-linejoin=\"round\" points=\"";
    for (size_t i = 0; i < pnl_curve.size(); ++i) {
        double x = padding_x + (double)i / (pnl_curve.size() - 1) * (width - 2 * padding_x);
        double y = padding_y + (max_v - pnl_curve[i]) / range * (height - 2 * padding_y);
        ofs << x << "," << y << " ";
    }
    ofs << "\" />\n";

    // Legend Box (Top Right-ish)
    int lx = width - 210, ly = padding_y + 10;
    ofs << "  <rect x=\"" << lx << "\" y=\"" << ly << "\" width=\"140\" height=\"50\" fill=\"rgba(255,255,255,0.05)\" rx=\"4\" stroke=\"#30363d\" />\n";
    ofs << "  <rect x=\"" << (lx + 10) << "\" y=\"" << (ly + 10) << "\" width=\"12\" height=\"12\" fill=\"#238636\" rx=\"2\" />\n";
    ofs << "  <text x=\"" << (lx + 30) << "\" y=\"" << (ly + 20) << "\" fill=\"#fff\" font-family=\"monospace\" font-size=\"12\">Cum. PnL</text>\n";
    ofs << "  <text x=\"" << (lx + 10) << "\" y=\"" << (ly + 40) << "\" fill=\"#8b949e\" font-family=\"monospace\" font-size=\"11\">Asset: " << asset_name << "</text>\n";

    // Header info
    ofs << "  <text x=\"" << padding_x << "\" y=\"" << (padding_y - 25) << "\" fill=\"#fff\" font-family=\"monospace\" font-size=\"18\" font-weight=\"bold\">" 
        << "STRATEGY: " << strategy_name << "</text>\n";
    
    ofs << "  <rect x=\"" << (width - 220) << "\" y=\"" << (padding_y - 45) << "\" width=\"180\" height=\"30\" rx=\"4\" fill=\"rgba(35,134,54,0.2)\" stroke=\"#238636\" />\n";
    ofs << "  <text x=\"" << (width - 130) << "\" y=\"" << (padding_y - 25) << "\" fill=\"#238636\" font-family=\"monospace\" font-size=\"16\" font-weight=\"bold\" text-anchor=\"middle\">"
        << "Final: " << std::fixed << std::setprecision(2) << pnl_curve.back() << "%</text>\n";

    ofs << "</svg>";
    ofs.close();
}

} // namespace chl
