#include "bolus_tracking_cpp.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iomanip>

// ---------------------------------------------------------
// BolusVisualizer Implementation
// ---------------------------------------------------------

/**
 * @brief Generates "nice" readable intervals and tick positions for axis labels.
 * @param min_val The minimum axis value.
 * @param max_val The maximum axis value.
 * @param max_ticks Ideal count of ticks.
 * @return NiceTicks struct.
 */
NiceTicks BolusVisualizer::get_nice_ticks(double min_val, double max_val, int max_ticks) {
    double range = max_val - min_val;
    if (range <= 0.0) {
        return {1.0, {min_val}};
    }
    double temp_step = range / (max_ticks - 1);
    double mag = std::floor(std::log10(temp_step));
    double base = std::pow(10.0, mag);
    double frac = temp_step / base;
    double nice_frac = 1.0;
    if (frac < 1.5) nice_frac = 1.0;
    else if (frac < 3.0) nice_frac = 2.0;
    else if (frac < 7.0) nice_frac = 5.0;
    else nice_frac = 10.0;
    double step = nice_frac * base;
    double start = std::ceil(min_val / step) * step;
    std::vector<double> ticks;
    for (double val = start; val <= max_val + 1e-9; val += step) {
        ticks.push_back(val);
    }
    return {step, ticks};
}

/**
 * @brief Formats floating point values for SVG XML text elements.
 */
std::string BolusVisualizer::format_tick(double val) {
    std::stringstream ss;
    if (std::abs(val) < 1e-9) {
        return "0";
    }
    if (std::abs(val) >= 1000.0 || std::abs(val) < 0.01) {
        ss << std::scientific << std::setprecision(1) << val;
    } else {
        ss << std::fixed << std::setprecision(1) << val;
    }
    return ss.str();
}

/**
 * @brief Saves an SVG image plotting raw data, denoised line, fitted model curve, and key metrics.
 */
void BolusVisualizer::save_svg_plot(int roi_id, const std::string& tiff_path,
                                    const std::vector<double>& tl_raw, const std::vector<double>& mfi_raw,
                                    const std::vector<double>& mfi_denoised,
                                    const std::vector<double>& tl_us, const std::vector<double>& y_us,
                                    const FitRecord& rec, bool fit_success, double k) {
    std::filesystem::path tiff_dir = std::filesystem::path(tiff_path).parent_path();
    std::filesystem::path plots_dir = tiff_dir / "plots_cpp";
    std::filesystem::create_directories(plots_dir);
    
    std::filesystem::path tp(tiff_path);
    std::string stem = tp.stem().string();
    std::string out_path = (plots_dir / (stem + "_ROI_" + std::to_string(roi_id) + ".svg")).string();
    
    std::ofstream f(out_path);
    if (!f.is_open()) return;
    
    int w = 800;
    int h = 450;
    int pad_l = 80;
    int pad_r = 40;
    int pad_t = 60;
    int pad_b = 60;
    
    double min_x = tl_raw.empty() ? 0.0 : tl_raw.front();
    double max_x = tl_raw.empty() ? 10.0 : tl_raw.back();
    
    double min_y = 999999.0;
    double max_y = -999999.0;
    for (double val : mfi_raw) {
        if (val < min_y) min_y = val;
        if (val > max_y) max_y = val;
    }
    for (double val : y_us) {
        if (val < min_y) min_y = val;
        if (val > max_y) max_y = val;
    }
    if (min_y > max_y) {
        min_y = 0.0;
        max_y = 100.0;
    }
    
    double y_range = max_y - min_y;
    min_y -= 0.1 * y_range;
    max_y += 0.1 * y_range;
    
    auto get_x_px = [&](double x_val) {
        return pad_l + (x_val - min_x) / (max_x - min_x) * (w - pad_l - pad_r);
    };
    auto get_y_px = [&](double y_val) {
        return h - pad_b - (y_val - min_y) / (max_y - min_y) * (h - pad_t - pad_b);
    };
    
    f << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << w << "\" height=\"" << h << "\" viewBox=\"0 0 " << w << " " << h << "\">\n";
    f << "  <rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
    
    // Grid Lines
    NiceTicks x_ticks = get_nice_ticks(min_x, max_x, 6);
    NiceTicks y_ticks = get_nice_ticks(min_y, max_y, 6);
    
    for (double tx : x_ticks.ticks) {
        double px = get_x_px(tx);
        f << "  <line x1=\"" << px << "\" y1=\"" << pad_t << "\" x2=\"" << px << "\" y2=\"" << h - pad_b << "\" stroke=\"#f0f0f0\" stroke-width=\"1\"/>\n";
    }
    for (double ty : y_ticks.ticks) {
        double py = get_y_px(ty);
        f << "  <line x1=\"" << pad_l << "\" y1=\"" << py << "\" x2=\"" << w - pad_r << "\" y2=\"" << py << "\" stroke=\"#f0f0f0\" stroke-width=\"1\"/>\n";
    }
    
    // Axes
    f << "  <line x1=\"" << pad_l << "\" y1=\"" << h - pad_b << "\" x2=\"" << w - pad_r << "\" y2=\"" << h - pad_b << "\" stroke=\"#000000\" stroke-width=\"1.5\"/>\n";
    f << "  <line x1=\"" << pad_l << "\" y1=\"" << pad_t << "\" x2=\"" << pad_l << "\" y2=\"" << h - pad_b << "\" stroke=\"#000000\" stroke-width=\"1.5\"/>\n";
    
    // Axis Labels (X)
    for (double tx : x_ticks.ticks) {
        double px = get_x_px(tx);
        f << "  <text x=\"" << px << "\" y=\"" << h - pad_b + 20 << "\" font-family=\"sans-serif\" font-size=\"12\" text-anchor=\"middle\" fill=\"#000000\">" << format_tick(tx) << "</text>\n";
        f << "  <line x1=\"" << px << "\" y1=\"" << h - pad_b << "\" x2=\"" << px << "\" y2=\"" << h - pad_b + 5 << "\" stroke=\"#000000\" stroke-width=\"1.2\"/>\n";
    }
    
    // Axis Labels (Y)
    for (double ty : y_ticks.ticks) {
        double py = get_y_px(ty);
        f << "  <text x=\"" << pad_l - 10 << "\" y=\"" << py + 4 << "\" font-family=\"sans-serif\" font-size=\"12\" text-anchor=\"end\" fill=\"#000000\">" << format_tick(ty) << "</text>\n";
        f << "  <line x1=\"" << pad_l - 5 << "\" y1=\"" << py << "\" x2=\"" << pad_l << "\" y2=\"" << py << "\" stroke=\"#000000\" stroke-width=\"1.2\"/>\n";
    }
    
    // Labels
    f << "  <text x=\"" << pad_l + (w - pad_l - pad_r) / 2.0 << "\" y=\"" << h - 15 << "\" font-family=\"sans-serif\" font-size=\"14\" text-anchor=\"middle\" fill=\"#000000\">Time (s)</text>\n";
    f << "  <text x=\"20\" y=\"" << pad_t + (h - pad_t - pad_b) / 2.0 << "\" font-family=\"sans-serif\" font-size=\"14\" text-anchor=\"middle\" fill=\"#000000\" transform=\"rotate(-90 20 " << pad_t + (h - pad_t - pad_b) / 2.0 << ")\">Mean Fluorescence Intensity (MFI)</text>\n";
    
    // ROI Identifier
    std::string qc_color = "#2ca02c"; // green
    if (rec.qc_flag == "FAIL") qc_color = "#d62728"; // red
    else if (rec.qc_flag == "WARN") qc_color = "#ff7f0e"; // orange
    
    f << "  <text x=\"" << pad_l << "\" y=\"" << pad_t - 30 << "\" font-family=\"sans-serif\" font-size=\"16\" font-weight=\"bold\" fill=\"#000000\">ROI " << roi_id << " (" << stem << ")</text>\n";
    f << "  <text x=\"" << w - pad_r << "\" y=\"" << pad_t - 30 << "\" font-family=\"sans-serif\" font-size=\"14\" font-weight=\"bold\" text-anchor=\"end\" fill=\"" << qc_color << "\">QC: " << rec.qc_flag << " (" << rec.fit_source << ")</text>\n";
    
    // Raw Data Plot (Points)
    if (!tl_raw.empty()) {
        f << "  <path d=\"";
        for (size_t i = 0; i < tl_raw.size(); ++i) {
            double px = get_x_px(tl_raw[i]);
            double py = get_y_px(mfi_raw[i]);
            if (i == 0) f << "M " << px << " " << py;
            else f << " L " << px << " " << py;
        }
        f << "\" fill=\"none\" stroke=\"#1f77b4\" stroke-width=\"1.0\" stroke-opacity=\"0.6\"/>\n";
        
        for (size_t i = 0; i < tl_raw.size(); ++i) {
            double px = get_x_px(tl_raw[i]);
            double py = get_y_px(mfi_raw[i]);
            f << "  <circle cx=\"" << px << "\" cy=\"" << py << "\" r=\"2.0\" fill=\"#1f77b4\" fill-opacity=\"0.6\"/>\n";
        }
    }
    
    // Denoised Data Plot (Line)
    if (mfi_denoised.size() == tl_raw.size() && !tl_raw.empty()) {
        f << "  <path d=\"";
        for (size_t i = 0; i < tl_raw.size(); ++i) {
            double px = get_x_px(tl_raw[i]);
            double py = get_y_px(mfi_denoised[i]);
            if (i == 0) f << "M " << px << " " << py;
            else f << " L " << px << " " << py;
        }
        f << "\" fill=\"none\" stroke=\"#ff7f0e\" stroke-width=\"1.5\"/>\n";
    }
    
    // Fit Curve Plot — use click_onset as origin (fitting shifts t by tl_us[start_idx])
    if (fit_success && !tl_us.empty()) {
        f << "  <path d=\"";
        bool first = true;
        for (size_t i = 0; i < tl_us.size(); ++i) {
            double t = tl_us[i];
            double dt = t - rec.click_onset;
            double val = k * t + evaluate_gamma_model(dt, rec.f_amp, rec.f_t2p, rec.f_fwhm, rec.f_m);
            double px = get_x_px(t);
            double py = get_y_px(val);
            if (first) {
                f << "M " << px << " " << py;
                first = false;
            } else {
                f << " L " << px << " " << py;
            }
        }
        f << "\" fill=\"none\" stroke=\"#2ca02c\" stroke-width=\"2.5\"/>\n";
    }
    
    // Annotation Lines
    if (fit_success) {
        double peak_t = rec.click_onset + rec.f_t2p;
        double peak_val = k * peak_t + rec.f_m + rec.f_amp;
        double px_peak = get_x_px(peak_t);
        double py_peak = get_y_px(peak_val);
        
        double base_val = k * peak_t + rec.f_m;
        double py_base = get_y_px(base_val);
        
        // Vertical line representing peak height
        f << "  <line x1=\"" << px_peak << "\" y1=\"" << py_base << "\" x2=\"" << px_peak << "\" y2=\"" << py_peak << "\" stroke=\"#7f7f7f\" stroke-width=\"1.2\" stroke-dasharray=\"3,3\"/>\n";
        
        double onset_t = rec.click_onset + rec.ont;
        double px_onset = get_x_px(onset_t);
        
        // Onset annotation
        f << "  <line x1=\"" << px_onset << "\" y1=\"" << pad_t << "\" x2=\"" << px_onset << "\" y2=\"" << h - pad_b << "\" stroke=\"#d62728\" stroke-width=\"1.5\" stroke-dasharray=\"4,4\"/>\n";
        f << "  <text x=\"" << px_onset + 5 << "\" y=\"" << pad_t + 15 << "\" font-family=\"sans-serif\" font-size=\"12\" font-weight=\"bold\" fill=\"#d62728\">Onset: " << format_tick(rec.ont) << "s</text>\n";
        
        // Parameters Panel (Text Box)
        int box_w = 200;
        int box_h = 100;
        int box_x = w - pad_r - box_w;
        int box_y = h - pad_b - box_h - 10;
        
        f << "  <rect x=\"" << box_x << "\" y=\"" << box_y << "\" width=\"" << box_w << "\" height=\"" << box_h << "\" fill=\"#ffffff\" fill-opacity=\"0.9\" stroke=\"#cccccc\" stroke-width=\"1\" rx=\"4\"/>\n";
        f << "  <text x=\"" << box_x + 10 << "\" y=\"" << box_y + 20 << "\" font-family=\"sans-serif\" font-size=\"12\" fill=\"#000000\">Amp: " << format_tick(rec.f_amp) << "</text>\n";
        f << "  <text x=\"" << box_x + 10 << "\" y=\"" << box_y + 40 << "\" font-family=\"sans-serif\" font-size=\"12\" fill=\"#000000\">T2P: " << format_tick(rec.f_t2p) << " s</text>\n";
        f << "  <text x=\"" << box_x + 10 << "\" y=\"" << box_y + 60 << "\" font-family=\"sans-serif\" font-size=\"12\" fill=\"#000000\">FWHM: " << format_tick(rec.f_fwhm) << " s</text>\n";
        f << "  <text x=\"" << box_x + 10 << "\" y=\"" << box_y + 80 << "\" font-family=\"sans-serif\" font-size=\"12\" fill=\"#000000\">CNR: " << format_tick(rec.f_cnr) << " (" << rec.ves_type << ")</text>\n";
    }
    
    f << "</svg>\n";
    f.close();
}
