#include "bolus_tracking_cpp.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <future>
#include <chrono>
#include <tiffio.h>

// ---------------------------------------------------------
// DatasetProcessor Implementation
// ---------------------------------------------------------

/**
 * @brief Constructs a DatasetProcessor instance.
 */
DatasetProcessor::DatasetProcessor(double drift_window, bool enable_plots, const BolusFitter& fitter, const QCSettings& qc_settings)
    : drift_window(drift_window), enable_plots(enable_plots), fitter(fitter), qc_settings(qc_settings) {}

/**
 * @brief Extracts the subject number from a filepath using regex matching.
 */
int DatasetProcessor::parse_subject_number(const std::string& filepath) const {
    size_t pos = filepath.find("subject");
    if (pos != std::string::npos) {
        size_t start = pos + 7;
        while (start < filepath.length() && (filepath[start] == '-' || filepath[start] == '_')) {
            start++;
        }
        std::string num_str = "";
        while (start < filepath.length() && std::isdigit(filepath[start])) {
            num_str += filepath[start];
            start++;
        }
        if (!num_str.empty()) {
            try { return std::stoi(num_str); } catch (...) {}
        }
    }
    for (size_t i = 0; i + 3 < filepath.length(); ++i) {
        if (std::isdigit(filepath[i]) && std::isdigit(filepath[i+1]) &&
            std::isdigit(filepath[i+2]) && std::isdigit(filepath[i+3])) {
            try { return std::stoi(filepath.substr(i, 4)); } catch (...) {}
        }
    }
    return 0;
}

/**
 * @brief Extracts the experiment name from a filepath.
 */
std::string DatasetProcessor::parse_experiment(const std::string& filepath) const {
    size_t last_slash = filepath.find_last_of("/\\");
    std::string filename = (last_slash == std::string::npos) ? filepath : filepath.substr(last_slash + 1);
    size_t last_dot = filename.find_last_of('.');
    if (last_dot != std::string::npos) {
        return filename.substr(0, last_dot);
    }
    return filename;
}

/**
 * @brief Runs the complete signal processing and curve fitting workflow on a single ROI.
 */
FitRecord DatasetProcessor::process_single_roi(int roi_id, const std::vector<std::pair<double, double>>& poly,
                                               const std::vector<std::vector<float>>& frames, int width, int height,
                                               double fr, int up_f, const std::string& tiff_path,
                                               double prior_t2p, double prior_fwhm) const {
    std::vector<int> mask = ROIMaskRasterizer::get_mask_pixels(poly, width, height);
    
    int mask_size = 0;
    for (int val : mask) mask_size += val;
    
    std::vector<double> mfi_raw(frames.size(), 0.0);
    if (mask_size > 0) {
        for (size_t f = 0; f < frames.size(); ++f) {
            double sum = 0.0;
            for (int r = 0; r < height; ++r) {
                for (int c = 0; c < width; ++c) {
                    if (mask[r * width + c]) {
                        sum += frames[f][r * width + c];
                    }
                }
            }
            mfi_raw[f] = sum / mask_size;
        }
    }
    
    std::vector<double> tl_raw(mfi_raw.size());
    for (size_t i = 0; i < tl_raw.size(); ++i) tl_raw[i] = i / fr;

    double sum_t = 0.0, sum_y = 0.0;
    double sum_tt = 0.0, sum_ty = 0.0;
    int count = 0;
    for (size_t i = 0; i < tl_raw.size(); ++i) {
        if (tl_raw[i] <= drift_window) {
            double t = tl_raw[i];
            double y = mfi_raw[i];
            sum_t += t;
            sum_y += y;
            sum_tt += t * t;
            sum_ty += t * y;
            count++;
        }
    }
    double k = 0.0;
    if (count > 1) {
        double mean_t = sum_t / count;
        double mean_y = sum_y / count;
        double num = sum_ty - count * mean_t * mean_y;
        double den = sum_tt - count * mean_t * mean_t;
        if (std::abs(den) > 1e-9) {
            k = num / den;
        }
    }

    std::vector<double> mfi_raw_detrended = mfi_raw;
    for (size_t i = 0; i < mfi_raw.size(); ++i) {
        mfi_raw_detrended[i] -= k * tl_raw[i];
    }
    
    // Calculate raw trace CNR
    int n_base = std::min((int)std::round(2.0 * fr), (int)std::round(mfi_raw.size() * 0.1));
    n_base = std::max(2, n_base);
    std::vector<double> raw_base_win(mfi_raw_detrended.begin(), mfi_raw_detrended.begin() + n_base);
    double raw_baseline = SignalProcessor::compute_median(raw_base_win);
    double sum_raw_base = 0.0;
    for (double val : raw_base_win) sum_raw_base += val;
    double mean_raw_base = sum_raw_base / raw_base_win.size();
    double raw_sd_base = SignalProcessor::compute_std(raw_base_win, mean_raw_base);
    
    double raw_max_val = -1e9;
    for (double val : mfi_raw_detrended) {
        if (val > raw_max_val) raw_max_val = val;
    }
    double raw_amp = raw_max_val - raw_baseline;
    double raw_cnr = (raw_sd_base > 0.0) ? (raw_amp / raw_sd_base) : 0.0;
    
    double denoise_thresh = 2.0;
    int denoise_half_win = 5;
    bool is_low_cnr = false;
    if (raw_cnr < 4.0) {
        denoise_thresh = 1.5;
        denoise_half_win = 7;
        is_low_cnr = true;
    } else if (raw_cnr >= 15.0) {
        denoise_thresh = 3.0;
        denoise_half_win = 3;
    } else if (raw_cnr >= 8.0) {
        denoise_thresh = 2.5;
        denoise_half_win = 5;
    }
    
    std::vector<double> mfi_denoised = SignalProcessor::denoise_trace(mfi_raw_detrended, denoise_thresh, denoise_half_win);
    
    std::vector<double> tl_us(mfi_raw.size() * up_f);
    for (size_t i = 0; i < tl_us.size(); ++i) tl_us[i] = i / (fr * up_f);
    
    SplineInterpolator spline;
    spline.build(tl_raw, mfi_denoised);
    
    std::vector<double> y_us(tl_us.size());
    for (size_t i = 0; i < tl_us.size(); ++i) y_us[i] = spline.eval(tl_us[i]);
    
    AutoEstimateResults auto_res = fitter.auto_estimate_params(y_us, tl_us, fr, up_f, is_low_cnr);
    
    double init_cnr = (auto_res.sd_base > 0.0) ? (auto_res.init_params[0] / auto_res.sd_base) : 0.0;
    if (init_cnr < 5.0 && !is_low_cnr) {
        mfi_denoised = SignalProcessor::denoise_trace(mfi_raw_detrended, 1.5, 7);
        spline.build(tl_raw, mfi_denoised);
        for (size_t i = 0; i < tl_us.size(); ++i) y_us[i] = spline.eval(tl_us[i]);
        auto_res = fitter.auto_estimate_params(y_us, tl_us, fr, up_f, true);
    }
    
    int start_idx = auto_res.start_idx;
    int end_idx = auto_res.end_idx;
    
    bool debug_roi = (roi_id == 10 || roi_id == 21 || roi_id == 27 || roi_id == 43 || roi_id == 44);
    if (debug_roi) {
        std::cout << "[C++ ROI " << roi_id << " DEBUG] k = " << k << ", raw_baseline = " << raw_baseline 
                  << ", raw_sd_base = " << raw_sd_base << ", raw_amp = " << raw_amp 
                  << ", raw_cnr = " << raw_cnr << ", is_low_cnr = " << is_low_cnr 
                  << ", init_cnr = " << init_cnr << ", prior_t2p = " << prior_t2p 
                  << ", prior_fwhm = " << prior_fwhm << std::endl;
        std::cout << "  Initial parameters: Amp = " << auto_res.init_params[0] 
                  << ", T2p = " << auto_res.init_params[1] << ", FWHM = " << auto_res.init_params[2] 
                  << ", M = " << auto_res.init_params[3] << std::endl;
        std::cout << "  Start_idx = " << auto_res.start_idx << ", End_idx = " << auto_res.end_idx 
                  << ", sd_base = " << auto_res.sd_base << std::endl;
    }
    
    std::vector<double> t_fit(end_idx - start_idx);
    std::vector<double> y_fit(end_idx - start_idx);
    for (int i = start_idx; i < end_idx; ++i) {
        t_fit[i - start_idx] = tl_us[i] - tl_us[start_idx];
        y_fit[i - start_idx] = y_us[i];
    }
    
    bool fit_success = false;
    bool pass2_run = false;
    std::vector<double> popt;
    
    if (prior_t2p > 0.0 && prior_fwhm > 0.0) {
        std::vector<double> prior_params = auto_res.init_params;
        prior_params[1] = prior_t2p;
        prior_params[2] = prior_fwhm;
        
        double b_min_amp = fitter.min_amp;
        double b_max_amp = fitter.max_amp;
        double b_min_t2p = 0.5 * prior_t2p;
        double b_max_t2p = 1.5 * prior_t2p;
        double b_min_fwhm = 0.5 * prior_fwhm;
        double b_max_fwhm = 1.5 * prior_fwhm;
        
        popt = fitter.run_nonlinear_fit_with_bounds(t_fit, y_fit, prior_params, auto_res.sd_base,
                                                     b_min_amp, b_max_amp,
                                                     b_min_t2p, b_max_t2p,
                                                     b_min_fwhm, b_max_fwhm,
                                                     fit_success, debug_roi);
    } else {
        popt = fitter.run_nonlinear_fit(t_fit, y_fit, auto_res.init_params, auto_res.sd_base, fit_success, pass2_run, debug_roi);
    }
    
    if (debug_roi) {
        std::cout << "  [C++ ROI " << roi_id << " DEBUG] fit_success = " << fit_success << ", pass2_run = " << pass2_run << std::endl;
        if (fit_success) {
            std::cout << "  fitted popt: Amp=" << popt[0] << ", T2p=" << popt[1] << ", FWHM=" << popt[2] << ", M=" << popt[3] << std::endl;
        }
    }
    
    FitRecord rec;
    rec.roi_id = roi_id;
    rec.subj_num = parse_subject_number(tiff_path);
    rec.exp = parse_experiment(tiff_path);
    rec.roi_size = mask_size;
    rec.ves_type = "U";
    
    rec.init_amp = auto_res.init_params[0];
    rec.init_t2p = auto_res.init_params[1];
    rec.init_fwhm = auto_res.init_params[2];
    rec.init_m = auto_res.init_params[3];
    rec.init_cnr = (auto_res.sd_base > 0.0) ? (auto_res.init_params[0] / auto_res.sd_base) : NAN;
    rec.init_snr = (auto_res.sd_base > 0.0) ? (auto_res.init_params[3] / auto_res.sd_base) : NAN;
    rec.click_start = auto_res.click_start;
    rec.click_onset = auto_res.click_onset;
    rec.click_peak = auto_res.click_peak;
    rec.click_end = auto_res.click_end;
    
    std::string qc_flag = "FAIL";
    if (fit_success) {
        double actual_max_t2p = (fitter.max_t2p >= 1e5 && !t_fit.empty()) ? (tl_us[end_idx] - tl_us[start_idx]) : fitter.max_t2p;
        double actual_max_fwhm = (fitter.max_fwhm >= 1e5 && !t_fit.empty()) ? (tl_us[end_idx] - tl_us[start_idx]) : fitter.max_fwhm;
        
        double final_min_t2p = (prior_t2p > 0.0) ? (0.5 * prior_t2p) : fitter.min_t2p;
        double final_max_t2p = (prior_t2p > 0.0) ? (1.5 * prior_t2p) : actual_max_t2p;
        double final_min_fwhm = (prior_fwhm > 0.0) ? (0.5 * prior_fwhm) : fitter.min_fwhm;
        double final_max_fwhm = (prior_fwhm > 0.0) ? (1.5 * prior_fwhm) : actual_max_fwhm;
        
        double f_cnr = (auto_res.sd_base > 0.0) ? (popt[0] / auto_res.sd_base) : 0.0;
        qc_flag = BolusFitter::determine_qc_flag(popt[0], popt[1], popt[2], popt[3], f_cnr,
                                                  fitter.min_amp, fitter.max_amp, final_min_t2p, final_max_t2p,
                                                  final_min_fwhm, final_max_fwhm, fit_success, pass2_run);
    } else {
        qc_flag = "FAIL";
    }
    rec.qc_flag = qc_flag;
    rec.fit_source = (prior_t2p > 0.0) ? "population_prior" : "auto";
    
    if (fit_success) {
        rec.f_amp = popt[0];
        rec.f_t2p = popt[1];
        rec.f_fwhm = popt[2];
        rec.f_m = popt[3];
        rec.f_cnr = (auto_res.sd_base > 0.0) ? (popt[0] / auto_res.sd_base) : NAN;
        rec.f_snr = (auto_res.sd_base > 0.0) ? (popt[3] / auto_res.sd_base) : NAN;
        
        std::vector<double> y_fit_model(t_fit.size());
        for (size_t i = 0; i < t_fit.size(); ++i) {
            y_fit_model[i] = evaluate_gamma_model(t_fit[i], popt[0], popt[1], popt[2], popt[3]);
        }
        
        double sum_y = 0.0;
        for (double val : y_fit_model) sum_y += val;
        rec.auc = sum_y - (y_fit_model.front() + y_fit_model.back()) / 2.0;
        
        double min_y = y_fit_model[0];
        double max_y = y_fit_model[0];
        for (double val : y_fit_model) {
            if (val < min_y) min_y = val;
            if (val > max_y) max_y = val;
        }
        double range = max_y - min_y;
        double sum_yn = 0.0;
        for (double val : y_fit_model) {
            sum_yn += (range > 0.0) ? (val - min_y) / range : 0.0;
        }
        double first_yn = (range > 0.0) ? (y_fit_model.front() - min_y) / range : 0.0;
        double last_yn = (range > 0.0) ? (y_fit_model.back() - min_y) / range : 0.0;
        rec.aucn = sum_yn - (first_yn + last_yn) / 2.0;
        
        std::vector<int> I;
        for (size_t i = 0; i < y_fit_model.size(); ++i) {
            double val_n = (range > 0.0) ? (y_fit_model[i] - min_y) / range : 0.0;
            if (val_n < 0.1) {
                I.push_back(i);
            }
        }
        int onset_idx = 0;
        if (!I.empty()) {
            int last_idx = -1;
            for (size_t k = 0; k + 1 < I.size(); ++k) {
                if (I[k+1] - I[k] == 1) {
                    last_idx = k;
                }
            }
            if (last_idx != -1) {
                onset_idx = I[last_idx] + 1;
            } else {
                onset_idx = I[0];
            }
        }
        rec.ont = (double)onset_idx / (fr * up_f);
        rec.ttm = std::abs(popt[1] - rec.ont);
        
        double sum_sq_resid = 0.0;
        for (size_t i = 0; i < y_fit.size(); ++i) {
            double diff = y_fit[i] - y_fit_model[i];
            sum_sq_resid += diff * diff;
        }
        double mse = (y_fit.size() > 4) ? (sum_sq_resid / (y_fit.size() - 4)) : 0.0;
        std::vector<double> se = fitter.get_parameter_se(t_fit, popt, mse);
        double se_t2p = se[1];
        
        double ci_lower = popt[1] - 1.96 * se_t2p;
        double ci_upper = popt[1] + 1.96 * se_t2p;
        rec.ttlb = std::abs(ci_lower - rec.ont);
        rec.tthb = std::abs(ci_upper - rec.ont);
        
        if (std::isnan(rec.f_amp) || std::isnan(rec.f_t2p) || std::isnan(rec.f_fwhm) || std::isnan(rec.f_m) ||
            std::isnan(rec.f_cnr) || std::isnan(rec.f_snr) || std::isnan(rec.auc) || std::isnan(rec.aucn) ||
            std::isnan(rec.ttlb) || std::isnan(rec.ttm) || std::isnan(rec.tthb) || std::isnan(rec.ont)) {
            rec.qc_flag = "FAIL";
        }
    } else {
        rec.f_amp = NAN;
        rec.f_t2p = NAN;
        rec.f_fwhm = NAN;
        rec.f_m = NAN;
        rec.f_cnr = NAN;
        rec.f_snr = NAN;
        rec.auc = NAN;
        rec.aucn = NAN;
        rec.ttlb = NAN;
        rec.ttm = NAN;
        rec.tthb = NAN;
        rec.ont = NAN;
    }
    
    rec.ves_type = BolusFitter::suggest_vessel_type(rec.ont, rec.f_t2p, rec.f_fwhm, rec.f_amp, rec.qc_flag);
    
    double sum_sq_diff = 0.0;
    for (size_t i = 0; i < mfi_raw.size(); ++i) {
        double diff = mfi_raw_detrended[i] - mfi_denoised[i];
        sum_sq_diff += diff * diff;
    }
    rec.denoise_rms = (mfi_raw.size() > 0) ? std::sqrt(sum_sq_diff / mfi_raw.size()) : 0.0;
    
    if (enable_plots) {
        BolusVisualizer::save_svg_plot(roi_id, tiff_path, tl_raw, mfi_raw, mfi_denoised, tl_us, y_us, rec, fit_success, k);
    }
    
    return rec;
}

/**
 * @brief Processes a single TIFF file containing multi-frame images and ROI definitions.
 */
bool DatasetProcessor::process_dataset_file(const std::string& tiff_path, const std::string& rois_path, double fr, int up_f, const std::string& out_csv) const {
    TIFFSetWarningHandler(nullptr);
    std::cout << "Starting C++ Bolus Tracking for: " << tiff_path << std::endl;
    
    TIFF* tif = TIFFOpen(tiff_path.c_str(), "r");
    if (!tif) {
        std::cerr << "Failed to open TIFF file: " << tiff_path << std::endl;
        return false;
    }
    
    uint32_t width = 0, height = 0;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
    
    std::vector<std::vector<float>> frames;
    do {
        std::vector<float> frame(width * height);
        uint16_t bitspersample = 8;
        uint16_t sampleformat = 1;
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitspersample);
        TIFFGetField(tif, TIFFTAG_SAMPLEFORMAT, &sampleformat);
        
        tdata_t buf = _TIFFmalloc(TIFFScanlineSize(tif));
        for (uint32_t row = 0; row < height; row++) {
            TIFFReadScanline(tif, buf, row);
            for (uint32_t col = 0; col < width; col++) {
                float val = 0.0f;
                if (bitspersample == 16) {
                    val = static_cast<float>(((uint16_t*)buf)[col]);
                } else if (bitspersample == 8) {
                    val = static_cast<float>(((uint8_t*)buf)[col]);
                } else if (bitspersample == 32 && sampleformat == 3) {
                    val = ((float*)buf)[col];
                }
                frame[row * width + col] = val;
            }
        }
        _TIFFfree(buf);
        frames.push_back(frame);
    } while (TIFFReadDirectory(tif));
    
    TIFFClose(tif);
    std::cout << "Loaded " << frames.size() << " frames (" << width << "x" << height << ")" << std::endl;
    
    std::ifstream rois_file(rois_path);
    if (!rois_file.is_open()) {
        std::cerr << "Failed to open ROIs file: " << rois_path << std::endl;
        return false;
    }
    
    int n_rois = 0;
    rois_file >> n_rois;
    std::vector<ROI> rois;
    rois.reserve(n_rois);
    for (int i = 0; i < n_rois; ++i) {
        int roi_id, n_pts;
        rois_file >> roi_id >> n_pts;
        std::vector<std::pair<double, double>> poly(n_pts);
        for (int j = 0; j < n_pts; ++j) {
            rois_file >> poly[j].first >> poly[j].second;
        }
        if (n_pts < 3) {
            std::cout << "Skipping ROI " << roi_id + 1 << ": Not enough points for a polygon (" << n_pts << ")." << std::endl;
            continue;
        }
        rois.push_back({roi_id, poly});
    }
    rois_file.close();
    std::cout << "Loaded " << rois.size() << " ROIs" << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<std::future<FitRecord>> futures;
    futures.reserve(rois.size());
    
    for (size_t i = 0; i < rois.size(); ++i) {
        futures.push_back(std::async(std::launch::async, &DatasetProcessor::process_single_roi, this,
                                     rois[i].id + 1, rois[i].poly, std::ref(frames),
                                     (int)width, (int)height, fr, up_f, tiff_path, 0.0, 0.0));
    }
    
    std::vector<FitRecord> results;
    results.reserve(rois.size());
    for (auto& f : futures) {
        results.push_back(f.get());
    }
    
    // --- Pass 3: Quality-Aware Population Priors Refitting ---
    std::vector<double> high_quality_t2ps;
    std::vector<double> high_quality_fwhms;
    for (const auto& rec : results) {
        if (rec.qc_flag == "PASS" && rec.f_cnr > 10.0) {
            high_quality_t2ps.push_back(rec.f_t2p);
            high_quality_fwhms.push_back(rec.f_fwhm);
        }
    }
    
    if (!high_quality_t2ps.empty()) {
        double median_t2p = SignalProcessor::compute_median(high_quality_t2ps);
        double median_fwhm = SignalProcessor::compute_median(high_quality_fwhms);
        
        std::cout << "[Population Priors] Calculated scan-wide medians: median_t2p = " << median_t2p 
                  << " s, median_fwhm = " << median_fwhm << " s from " 
                  << high_quality_t2ps.size() << " high-CNR PASS vessels." << std::endl;
                  
        // Parallel rerun of warned/failed vessels using the population priors
        std::vector<std::pair<size_t, std::future<FitRecord>>> prior_futures;
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& rec = results[i];
            if (rec.qc_flag == "FAIL" || rec.qc_flag == "WARN") {
                prior_futures.push_back({i, std::async(std::launch::async, &DatasetProcessor::process_single_roi, this,
                                                      rois[i].id + 1, rois[i].poly, std::ref(frames),
                                                      (int)width, (int)height, fr, up_f, tiff_path,
                                                      median_t2p, median_fwhm)});
            }
        }
        
        for (auto& pf : prior_futures) {
            size_t idx = pf.first;
            FitRecord refit_rec = pf.second.get();
            bool improvement = false;
            if (refit_rec.qc_flag == "PASS" && results[idx].qc_flag != "PASS") {
                improvement = true;
            } else if (refit_rec.qc_flag == "WARN" && results[idx].qc_flag == "FAIL") {
                improvement = true;
            } else if (refit_rec.qc_flag == "WARN" && results[idx].qc_flag == "WARN") {
                bool first_outside = (results[idx].f_t2p < 0.1 || results[idx].f_fwhm < 0.5);
                bool refit_inside = (refit_rec.f_t2p >= 0.1 && refit_rec.f_fwhm >= 0.5);
                if (first_outside && refit_inside) {
                    improvement = true;
                }
            }
            if (improvement) {
                results[idx] = refit_rec;
                std::cout << "  ROI " << results[idx].roi_id << " successfully refit with population priors: QC -> " << results[idx].qc_flag << std::endl;
            }
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;
    std::cout << "Parallel fitting complete in " << diff.count() << " seconds!" << std::endl;
    
    double min_ont = 999999.0;
    for (const auto& rec : results) {
        if (!std::isnan(rec.ont) && rec.ont < min_ont) {
            min_ont = rec.ont;
        }
    }
    
    std::ofstream out(out_csv);
    if (!out.is_open()) {
        std::cerr << "Failed to open output CSV: " << out_csv << std::endl;
        return false;
    }
    
    out << "ROI,SubjNum,Exp,InitAmp,InitT2p,InitFWHM,InitM,InitSNR,InitCNR,"
        << "Click1_Start_T,Click2_Onset_T,Click3_Peak_T,Click4_End_T,"
        << "F_Amp,F_T2p,F_FWHM,F_M,F_SNR,F_CNR,"
        << "AUC,AUCn,TTlb,TTm,TThb,OnT,OnTSc,ROISize,Denoise_RMS,VesType,QC_Flag,Fit_Source\n";
    
    for (auto& rec : results) {
        if (!std::isnan(rec.ont) && min_ont < 99999.0) {
            rec.ont_sc = rec.ont - min_ont;
        } else {
            rec.ont_sc = NAN;
        }
        
        out << rec.roi_id << ","
            << rec.subj_num << ","
            << rec.exp << ","
            << rec.init_amp << "," << rec.init_t2p << "," << rec.init_fwhm << "," << rec.init_m << "," << rec.init_snr << "," << rec.init_cnr << ","
            << rec.click_start << "," << rec.click_onset << "," << rec.click_peak << "," << rec.click_end << ","
            << rec.f_amp << "," << rec.f_t2p << "," << rec.f_fwhm << "," << rec.f_m << "," << rec.f_snr << "," << rec.f_cnr << ","
            << rec.auc << "," << rec.aucn << "," << rec.ttlb << "," << rec.ttm << "," << rec.tthb << "," << rec.ont << "," << rec.ont_sc << ","
            << rec.roi_size << "," << rec.denoise_rms << "," << rec.ves_type << ","
            << rec.qc_flag << "," << rec.fit_source << "\n";
    }
    out.close();
    std::cout << "Saved C++ results to: " << out_csv << std::endl;
    return true;
}
