#include "bolus_tracking_cpp.hpp"

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <Eigen/Dense>
#include <unsupported/Eigen/NonLinearOptimization>
#include <unsupported/Eigen/NumericalDiff>

// ---------------------------------------------------------
// GammaFunctor Implementation
// ---------------------------------------------------------

/**
 * @brief Evaluates the residuals between the fitted model and data for Levenberg-Marquardt optimization.
 * @param x The parameter vector in unbounded space.
 * @param fvec The output residuals vector.
 * @return Return code.
 */
int GammaFunctor::operator()(const Eigen::VectorXd& x, Eigen::VectorXd& fvec) const {
    double a1 = std::clamp(x[0], min_amp, max_amp);
    double peak1 = std::clamp(x[1], min_t2p, max_t2p);
    double fwhm1 = std::clamp(x[2], min_fwhm, max_fwhm);
    
    double L_m = m_init - m_bound;
    double U_m = m_init + m_bound;
    double m = std::clamp(x[3], L_m, U_m);
    
    int n = static_cast<int>(t.size());
    for (int i = 0; i < n; ++i) {
        double model_val = evaluate_gamma_model(t[i], a1, peak1, fwhm1, m);
        double r = y[i] - model_val;
        if (use_cauchy) {
            double r2 = r * r;
            double c2 = f_scale * f_scale;
            double val = c2 * std::log(1.0 + r2 / c2);
            double sign = (r >= 0.0) ? 1.0 : -1.0;
            fvec[i] = sign * std::sqrt(std::max(0.0, val));
        } else {
            fvec[i] = r;
        }
    }
    
    // Enforce parameter boundaries using quadratic penalties
    double weight = 1000.0;
    fvec[n + 0] = weight * (x[0] - a1);
    fvec[n + 1] = weight * (x[1] - peak1);
    fvec[n + 2] = weight * (x[2] - fwhm1);
    fvec[n + 3] = weight * (x[3] - m);
    
    return 0;
}

// ---------------------------------------------------------
// BolusFitter Implementation
// ---------------------------------------------------------

/**
 * @brief Constructs a BolusFitter instance with standard boundary parameters.
 */
BolusFitter::BolusFitter(double min_amp, double max_amp,
                         double min_t2p, double max_t2p,
                         double min_fwhm, double max_fwhm,
                         bool verbose)
    : min_amp(min_amp), max_amp(max_amp),
      min_t2p(min_t2p), max_t2p(max_t2p),
      min_fwhm(min_fwhm), max_fwhm(max_fwhm),
      verbose(verbose) {}

/**
 * @brief Automatically estimates initial guess parameters from the upsampled trace.
 * @param tr The upsampled trace.
 * @param t_us The time axis of the upsampled trace.
 * @param fr The camera frame rate.
 * @param up_f The upsampling factor.
 * @return Estimated results including bounds indices.
 */
AutoEstimateResults BolusFitter::auto_estimate_params(const std::vector<double>& tr, const std::vector<double>& t_us, double fr, int up_f, bool low_cnr) const {
    int n = tr.size();
    
    int n_base_frames = std::min((int)std::round(2.0 * fr * up_f), (int)std::round(n * 0.1));
    n_base_frames = std::max(1, n_base_frames);
    
    std::vector<double> base_win(tr.begin(), tr.begin() + n_base_frames);
    double baseline = SignalProcessor::compute_median(base_win);
    
    double mean_base = 0.0;
    for (double x : base_win) mean_base += x;
    mean_base /= base_win.size();
    double sd_base = SignalProcessor::compute_std(base_win, mean_base);
    
    int ignore_points = std::min((int)(3.0 * fr * up_f), (int)(0.05 * n));
    int valid_end = n - ignore_points;
    
    double rise_sigma = low_cnr ? (2.0 * fr * up_f) : (1.0 * fr * up_f);
    std::vector<double> smoothed_rise = SignalProcessor::gaussian_filter1d(tr, rise_sigma);
    std::vector<double> deriv_rise = SignalProcessor::gradient(smoothed_rise);
    
    int rise_idx = 0;
    double max_deriv = -1e9;
    for (int i = 0; i < valid_end; ++i) {
        if (deriv_rise[i] > max_deriv) {
            max_deriv = deriv_rise[i];
            rise_idx = i;
        }
    }
    
    int search_win = std::round(8.0 * fr * up_f);
    int peak_search_end = std::min(valid_end, rise_idx + search_win);
    int max_idx = rise_idx;
    double max_val = -1e9;
    for (int i = rise_idx; i < peak_search_end; ++i) {
        if (tr[i] > max_val) {
            max_val = tr[i];
            max_idx = i;
        }
    }
    
    double amp = max_val - baseline;
    
    double thresh = baseline + std::max(2.0 * sd_base, 0.02 * amp);
    int start_idx = 0;
    int persist_frames = std::max(1, (int)std::round(0.5 * fr * up_f)); // 0.5 seconds persistence
    for (int i = 0; i < max_idx; ++i) {
        if (smoothed_rise[i] > thresh) {
            bool persists = true;
            int check_len = std::min(persist_frames, max_idx - i);
            for (int j = 1; j < check_len; ++j) {
                if (smoothed_rise[i + j] <= thresh) {
                    persists = false;
                    break;
                }
            }
            if (persists) {
                // Return the index just before it crosses the threshold consistently
                start_idx = std::max(0, i - 1);
                break;
            }
        }
    }
    
    double t_start = t_us[start_idx];
    double t2p = std::max(t_us[max_idx] - t_start, 0.01);
    
    double half_max = baseline + 0.5 * amp;
    int idx_up = start_idx;
    for (int i = 0; i <= max_idx; ++i) {
        if (tr[i] >= half_max) {
            idx_up = i;
            break;
        }
    }
    
    int idx_down = -1;
    for (int i = max_idx; i < n; ++i) {
        if (tr[i] <= half_max) {
            idx_down = i;
            break;
        }
    }
    
    double fwhm = 0.0;
    if (idx_down == -1) {
        std::vector<double> deriv1 = SignalProcessor::gradient(smoothed_rise);
        int search_window_frames = std::round(15.0 * fr * up_f);
        int end_search_idx = std::min(n, max_idx + search_window_frames);
        
        double min_deriv = 1e9;
        int min_deriv_idx = max_idx;
        for (int i = max_idx; i < end_search_idx; ++i) {
            if (deriv1[i] < min_deriv) {
                min_deriv = deriv1[i];
                min_deriv_idx = i;
            }
        }
        
        double flatten_thresh = 0.2 * min_deriv;
        int knee_idx = min_deriv_idx + std::round(2.0 * fr * up_f);
        for (int i = min_deriv_idx; i < n; ++i) {
            if (deriv1[i] > flatten_thresh) {
                knee_idx = i;
                break;
            }
        }
        knee_idx = std::min(knee_idx, n - 1);
        fwhm = t_us[knee_idx] - t_us[idx_up];
    } else {
        fwhm = t_us[idx_down] - t_us[idx_up];
    }
    if (fwhm <= 0) fwhm = 0.5;
    
    double sigma_end = 0.8 * fr * up_f;
    std::vector<double> smoothed_end = SignalProcessor::gaussian_filter1d(tr, sigma_end);
    std::vector<double> deriv_end = SignalProcessor::gradient(smoothed_end);
    
    int local_min_idx = valid_end - 1;
    int downslope_start = max_idx;
    for (int i = max_idx; i < valid_end; ++i) {
        if (deriv_end[i] < 0) {
            downslope_start = i;
            break;
        }
    }
    for (int i = downslope_start; i < valid_end; ++i) {
        if (deriv_end[i] >= 0) {
            local_min_idx = i;
            break;
        }
    }
    if (local_min_idx <= max_idx) {
        local_min_idx = valid_end - 1;
    }
    
    double sigma_baseline = 1.0 * fr * up_f;
    std::vector<double> smoothed_tr = SignalProcessor::gaussian_filter1d(tr, sigma_baseline);
    int n_end_frames = std::min((int)std::round(2.0 * fr * up_f), (int)std::round(valid_end * 0.1));
    n_end_frames = std::max(1, n_end_frames);
    
    std::vector<double> end_win(smoothed_tr.begin() + (valid_end - n_end_frames), smoothed_tr.begin() + valid_end);
    double end_baseline = SignalProcessor::compute_median(end_win);
    
    double mean_end = 0.0;
    for (double x : end_win) mean_end += x;
    mean_end /= end_win.size();
    double end_sd_base = SignalProcessor::compute_std(end_win, mean_end);
    
    double end_thresh = end_baseline + std::max(3.0 * end_sd_base, 0.03 * amp);
    if (end_sd_base == 0.0 || end_thresh >= max_val) {
        end_thresh = end_baseline + 0.1 * amp;
    }
    
    int end_idx = local_min_idx;
    for (int i = local_min_idx; i >= max_idx; --i) {
        if (smoothed_end[i] > end_thresh) {
            end_idx = i;
            break;
        }
    }
    if (end_idx >= valid_end) {
        end_idx = valid_end - 1;
    }
    
    // Extend fit window by 25% of the fit duration
    int fit_dur = end_idx - start_idx;
    end_idx = std::min((int)tr.size() - 1, end_idx + (int)std::round(0.25 * fit_dur));
    double t_end = t_us[end_idx];
    
    return {
        {amp, t2p, fwhm, baseline},
        start_idx,
        end_idx,
        sd_base,
        t_us[0],
        t_start,
        t_us[max_idx],
        t_end
    };
}

/**
 * @brief Computes standard errors for the fitted parameters using Jacobian numerical approximation.
 * @param t The time vector.
 * @param popt The fitted parameters.
 * @param mse The mean squared error of the residuals.
 * @return Vector of standard errors.
 */
std::vector<double> BolusFitter::get_parameter_se(const std::vector<double>& t, const std::vector<double>& popt, double mse) const {
    int n = t.size();
    int p = 4;
    Eigen::MatrixXd J(n, p);
    double eps = 1e-5;
    
    auto eval_model = [](double t_val, const std::vector<double>& params) {
        return evaluate_gamma_model(t_val, params[0], params[1], params[2], params[3]);
    };
    
    for (int j = 0; j < p; ++j) {
        std::vector<double> perturbed = popt;
        perturbed[j] += eps;
        for (int i = 0; i < n; ++i) {
            double f1 = eval_model(t[i], perturbed);
            double f0 = eval_model(t[i], popt);
            J(i, j) = (f1 - f0) / eps;
        }
    }
    
    Eigen::MatrixXd JTJ = J.transpose() * J;
    for (int j = 0; j < p; ++j) JTJ(j, j) += 1e-8;
    
    Eigen::MatrixXd cov = mse * JTJ.inverse();
    std::vector<double> se(p, 0.0);
    for (int j = 0; j < p; ++j) {
        se[j] = (cov(j, j) > 0.0) ? std::sqrt(cov(j, j)) : 0.0;
    }
    return se;
}

/**
 * @brief Performs two-pass non-linear least squares optimization (least squares followed by Cauchy robust IRLS).
 */
std::vector<double> BolusFitter::run_nonlinear_fit_with_bounds(const std::vector<double>& t, const std::vector<double>& y,
                                                               const std::vector<double>& params_init, double sd_base,
                                                               double b_min_amp, double b_max_amp,
                                                               double b_min_t2p, double b_max_t2p,
                                                               double b_min_fwhm, double b_max_fwhm,
                                                               bool& success, bool debug_print) const {
    success = false;
    debug_print = debug_print || verbose;
    double m_init = params_init[3];
    double m_bound = std::max({0.5 * sd_base, 0.005 * m_init, 0.2});
    double L_m = m_init - m_bound;
    double U_m = m_init + m_bound;
    
    Eigen::VectorXd x(4);
    x[0] = std::clamp(params_init[0], b_min_amp, b_max_amp);
    x[1] = std::clamp(params_init[1], b_min_t2p, b_max_t2p);
    x[2] = std::clamp(params_init[2], b_min_fwhm, b_max_fwhm);
    x[3] = std::clamp(m_init, L_m, U_m);
    
    GammaFunctor functor{t, y, m_init, m_bound, false, 1.0, b_min_amp, b_max_amp, b_min_t2p, b_max_t2p, b_min_fwhm, b_max_fwhm};
    Eigen::NumericalDiff<GammaFunctor> numDiff(functor);
    Eigen::LevenbergMarquardt<Eigen::NumericalDiff<GammaFunctor>, double> lm(numDiff);
    lm.parameters.maxfev = 10000;
    lm.parameters.xtol = 1e-10;
    lm.parameters.ftol = 1e-10;
    
    int info = lm.minimize(x);
    
    double a1 = std::clamp(x[0], b_min_amp, b_max_amp);
    double peak1 = std::clamp(x[1], b_min_t2p, b_max_t2p);
    double fwhm1 = std::clamp(x[2], b_min_fwhm, b_max_fwhm);
    double m = std::clamp(x[3], L_m, U_m);
    
    if (debug_print) {
        std::cout << "    [DEBUG ROI bounds] linear fit: " << a1 << ", " << peak1 << ", " << fwhm1 << ", " << m << " (info=" << info << ")" << std::endl;
    }
    
    std::vector<double> residuals(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        residuals[i] = y[i] - evaluate_gamma_model(t[i], a1, peak1, fwhm1, m);
    }
    
    double median_res = SignalProcessor::compute_median(residuals);
    std::vector<double> abs_res(residuals.size());
    for (size_t i = 0; i < residuals.size(); ++i) {
        abs_res[i] = std::abs(residuals[i] - median_res);
    }
    double mad = SignalProcessor::compute_median(abs_res) / 0.6745;
    double dynamic_f_scale = std::max(2.3849 * mad, 0.1);
    
    GammaFunctor functor2{t, y, m_init, m_bound, true, dynamic_f_scale, b_min_amp, b_max_amp, b_min_t2p, b_max_t2p, b_min_fwhm, b_max_fwhm};
    Eigen::NumericalDiff<GammaFunctor> numDiff2(functor2);
    Eigen::LevenbergMarquardt<Eigen::NumericalDiff<GammaFunctor>, double> lm2(numDiff2);
    lm2.parameters.maxfev = 10000;
    lm2.parameters.xtol = 1e-10;
    lm2.parameters.ftol = 1e-10;
    
    info = lm2.minimize(x);
    if (debug_print) {
        double a2 = std::clamp(x[0], b_min_amp, b_max_amp);
        double peak2 = std::clamp(x[1], b_min_t2p, b_max_t2p);
        double fwhm2 = std::clamp(x[2], b_min_fwhm, b_max_fwhm);
        double m2 = std::clamp(x[3], L_m, U_m);
        std::cout << "    [DEBUG ROI bounds] cauchy fit: " << a2 << ", " << peak2 << ", " << fwhm2 << ", " << m2 << " (info=" << info << ")" << std::endl;
    }
    if (info >= 1 && info <= 5) {
        success = true;
    }
    
    double final_a1 = std::clamp(x[0], b_min_amp, b_max_amp);
    double final_peak1 = std::clamp(x[1], b_min_t2p, b_max_t2p);
    double final_fwhm1 = std::clamp(x[2], b_min_fwhm, b_max_fwhm);
    double final_m = std::clamp(x[3], L_m, U_m);
    
    return {final_a1, final_peak1, final_fwhm1, final_m};
}

std::vector<double> BolusFitter::run_nonlinear_fit(const std::vector<double>& t, const std::vector<double>& y,
                                                 const std::vector<double>& params_init, double sd_base, bool& success, bool& pass2_run, bool debug_print) const {
    pass2_run = false;
    double actual_max_t2p = (max_t2p >= 1e5 && !t.empty()) ? t.back() : max_t2p;
    double actual_max_fwhm = (max_fwhm >= 1e5 && !t.empty()) ? t.back() : max_fwhm;
    
    debug_print = debug_print || verbose;
    if (debug_print) {
        std::cout << "[DEBUG ROI] params_init: " << params_init[0] << ", " << params_init[1] << ", " << params_init[2] << ", " << params_init[3] << std::endl;
        std::cout << "  actual_max_t2p = " << actual_max_t2p << ", actual_max_fwhm = " << actual_max_fwhm << std::endl;
    }
    
    bool init_out_of_bounds = params_init[0] < min_amp || params_init[0] > max_amp ||
                              params_init[1] < min_t2p || params_init[1] > actual_max_t2p ||
                              params_init[2] < min_fwhm || params_init[2] > actual_max_fwhm;
                              
    bool pass1_success = false;
    std::vector<double> popt1 = {NAN, NAN, NAN, NAN};
    if (!init_out_of_bounds) {
        popt1 = run_nonlinear_fit_with_bounds(t, y, params_init, sd_base, min_amp, max_amp, min_t2p, actual_max_t2p, min_fwhm, actual_max_fwhm, pass1_success, debug_print);
    }
    
    if (debug_print) {
        std::cout << "  [DEBUG ROI] popt1: " << popt1[0] << ", " << popt1[1] << ", " << popt1[2] << ", " << popt1[3] << " (success=" << pass1_success << ")" << std::endl;
    }
    
    auto compute_rss = [&](const std::vector<double>& p) -> double {
        if (p.size() < 4 || std::isnan(p[0]) || std::isnan(p[1]) || std::isnan(p[2]) || std::isnan(p[3])) {
            return 1e30;
        }
        double rss = 0.0;
        for (size_t i = 0; i < t.size(); ++i) {
            double diff = y[i] - evaluate_gamma_model(t[i], p[0], p[1], p[2], p[3]);
            rss += diff * diff;
        }
        return rss;
    };
    
    bool near_bounds = std::isnan(popt1[0]) || std::isnan(popt1[1]) || std::isnan(popt1[2]) ||
                       is_near_bounds(popt1[0], min_amp, max_amp) ||
                       is_near_bounds(popt1[1], min_t2p, actual_max_t2p) ||
                       is_near_bounds(popt1[2], min_fwhm, actual_max_fwhm);
    
    bool trigger_pass2 = !pass1_success || near_bounds || popt1[2] > 20.0 || popt1[1] > 15.0;
    if (trigger_pass2) {
        double clamp_min_amp = 1.0;
        double clamp_max_amp = std::max(10.0 * params_init[0], 100.0);
        double clamp_min_t2p = 0.01;
        double clamp_max_t2p = 12.0;
        double clamp_min_fwhm = 0.1;
        double clamp_max_fwhm = 20.0;
        
        bool pass2_success = false;
        std::vector<double> popt2 = run_nonlinear_fit_with_bounds(t, y, params_init, sd_base, clamp_min_amp, clamp_max_amp, clamp_min_t2p, clamp_max_t2p, clamp_min_fwhm, clamp_max_fwhm, pass2_success, debug_print);
        
        if (debug_print) {
            std::cout << "  [DEBUG ROI] popt2: " << popt2[0] << ", " << popt2[1] << ", " << popt2[2] << ", " << popt2[3] << " (success=" << pass2_success << ")" << std::endl;
        }
        
        if (pass2_success) {
            double rss1 = compute_rss(popt1);
            double rss2 = compute_rss(popt2);
            bool use_pass2 = !pass1_success || near_bounds || (popt1[2] > 20.0 || popt1[1] > 15.0) || (rss2 < rss1);
            if (use_pass2) {
                success = true;
                pass2_run = true;
                return popt2;
            }
        }
    }
    
    success = pass1_success;
    return popt1;
}

bool BolusFitter::is_near_bounds(double val, double low, double high) {
    if (std::isnan(val)) return true;
    if (std::abs(val - low) < 1e-4) return true;
    if (low > 0.0 && val <= low * 1.01) return true;
    if (high > 0.0 && !std::isinf(high) && high < 99999.0) {
        if (std::abs(high - val) < 1e-4) return true;
        if (val >= high * 0.99) return true;
    }
    return false;
}

std::string BolusFitter::determine_qc_flag(double f_amp, double f_t2p, double f_fwhm, double f_m, double f_cnr,
                                           double min_amp, double max_amp, double min_t2p, double max_t2p,
                                           double min_fwhm, double max_fwhm, bool fit_success, bool pass2_run,
                                           double observed_peak_amp, double sd_base) {
    if (!fit_success || std::isnan(f_amp) || std::isnan(f_t2p) || std::isnan(f_fwhm) || std::isnan(f_m) || std::isnan(f_cnr)) {
        return "FAIL";
    }
    if (f_cnr < 3.0) {
        return "FAIL";
    }
    // Rule A: raw trace CNR < 3 means no discernible bolus above noise
    if (sd_base > 0.0 && observed_peak_amp > 0.0) {
        double raw_cnr = observed_peak_amp / sd_base;
        if (raw_cnr < 3.0) {
            return "FAIL";
        }
    }
    // Rule B: borderline CNR with FWHM at solver upper bound
    if (f_cnr < 5.0 && f_fwhm >= 20.0) {
        return "FAIL";
    }
    bool near_bounds = false;
    if (pass2_run) {
        near_bounds = is_near_bounds(f_amp, 1.0, max_amp) ||
                      is_near_bounds(f_t2p, 0.01, 12.0) ||
                      is_near_bounds(f_fwhm, 0.1, 20.0);
    } else {
        near_bounds = is_near_bounds(f_amp, min_amp, max_amp) ||
                      is_near_bounds(f_t2p, min_t2p, max_t2p) ||
                      is_near_bounds(f_fwhm, min_fwhm, max_fwhm);
    }
                       
    bool inside_pass_ranges = (f_fwhm >= 0.5 && f_fwhm <= 15.0) &&
                              (f_t2p >= 0.1 && f_t2p <= 10.0);

    // Goodness-of-fit check: if the fitted amplitude captures less than 40%
    // of the observed signal peak, the model is a poor fit even if params are
    // within bounds.  Only apply when the observed peak is meaningful (> 10).
    bool poor_fit = false;
    if (observed_peak_amp > 10.0 && f_amp > 0.0) {
        double peak_ratio = f_amp / observed_peak_amp;
        if (peak_ratio < 0.4) {
            poor_fit = true;
        }
    }
                              
    if (!near_bounds && f_cnr > 5.0 && inside_pass_ranges && !poor_fit) {
        return "PASS";
    }
    // Rule C: FWHM below physiological minimum for capillary transit
    if (f_fwhm < 2.0) {
        return "WARN";
    }
    return "WARN";
}

std::string BolusFitter::suggest_vessel_type(double ont, double t2p, double fwhm, double amp, const std::string& qc_flag) {
    if (qc_flag == "FAIL" || std::isnan(ont) || std::isnan(t2p)) {
        return "U";
    }
    double ttm = std::abs(t2p - ont);
    if (ont < 1.8 && ttm < 3.0) {
        return "A";
    }
    if (ont > 3.0 || ttm > 4.5) {
        return "V";
    }
    return "C";
}
