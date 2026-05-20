#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <future>
#include <memory>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <regex>
#include <map>

#include <tiffio.h>
#include <Eigen/Dense>
#include <unsupported/Eigen/NonLinearOptimization>
#include <unsupported/Eigen/NumericalDiff>

// ---------------------------------------------------------
// Helper Structures & Constants
// ---------------------------------------------------------
const double PI = 3.14159265358979323846;

struct ROI {
    int id;
    std::vector<std::pair<double, double>> poly;
};

struct FitRecord {
    int roi_id;
    double init_amp;
    double init_t2p;
    double init_fwhm;
    double init_m;
    double init_snr;
    double click_start;
    double click_onset;
    double click_peak;
    double click_end;
    double f_amp;
    double f_t2p;
    double f_fwhm;
    double f_m;
    double f_snr;
};

// ---------------------------------------------------------
// Math Helpers
// ---------------------------------------------------------
double compute_median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    size_t n = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + n, v.end());
    if (v.size() % 2 == 1) {
        return v[n];
    } else {
        auto max_it = std::max_element(v.begin(), v.begin() + n);
        return (*max_it + v[n]) / 2.0;
    }
}

double compute_std(const std::vector<double>& v, double mean) {
    if (v.size() <= 1) return 0.0;
    double sum_sq = 0.0;
    for (double x : v) {
        sum_sq += (x - mean) * (x - mean);
    }
    return std::sqrt(sum_sq / (v.size() - 1.0)); // ddof=1
}

int reflect_index(int idx, int n) {
    if (idx < 0) {
        return -idx;
    } else if (idx >= n) {
        return 2 * n - 2 - idx;
    }
    return idx;
}

std::vector<double> gaussian_filter1d(const std::vector<double>& tr, double sigma) {
    int n = tr.size();
    if (sigma <= 0.0 || n == 0) return tr;
    
    int radius = std::ceil(4.0 * sigma);
    std::vector<double> kernel(2 * radius + 1);
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        kernel[i + radius] = std::exp(- (i * i) / (2.0 * sigma * sigma));
        sum += kernel[i + radius];
    }
    for (double& val : kernel) val /= sum;
    
    std::vector<double> out(n, 0.0);
    for (int p = 0; p < n; ++p) {
        double val = 0.0;
        for (int i = -radius; i <= radius; ++i) {
            int idx = p + i;
            while (idx < 0 || idx >= n) {
                idx = reflect_index(idx, n);
            }
            val += tr[idx] * kernel[i + radius];
        }
        out[p] = val;
    }
    return out;
}

std::vector<double> gradient(const std::vector<double>& tr) {
    int n = tr.size();
    std::vector<double> grad(n, 0.0);
    if (n <= 1) return grad;
    grad[0] = tr[1] - tr[0];
    for (int i = 1; i < n - 1; ++i) {
        grad[i] = (tr[i+1] - tr[i-1]) / 2.0;
    }
    grad[n-1] = tr[n-1] - tr[n-2];
    return grad;
}

// ---------------------------------------------------------
// Cubic Spline Interpolation
// ---------------------------------------------------------
struct Spline {
    std::vector<double> x, y;
    std::vector<double> b, c, d;
    
    void build(const std::vector<double>& px, const std::vector<double>& py) {
        x = px;
        y = py;
        int n = x.size();
        if (n < 4) {
            b.assign(n, 0.0);
            c.assign(n, 0.0);
            d.assign(n, 0.0);
            return;
        }
        
        b.resize(n);
        c.resize(n);
        d.resize(n);
        
        std::vector<double> h(n - 1);
        for (int i = 0; i < n - 1; ++i) {
            h[i] = x[i+1] - x[i];
        }
        
        std::vector<double> rhs(n, 0.0);
        for (int i = 1; i < n - 1; ++i) {
            rhs[i] = (y[i+1] - y[i]) / h[i] - (y[i] - y[i-1]) / h[i-1];
        }
        
        int N_red = n - 2;
        std::vector<double> A_sub(N_red, 0.0);
        std::vector<double> A_diag(N_red, 0.0);
        std::vector<double> A_super(N_red, 0.0);
        std::vector<double> R_rhs(N_red, 0.0);
        
        A_diag[0] = h[0] * (h[0] + h[1]) / (3.0 * h[1]) + 2.0 * (h[0] + h[1]) / 3.0;
        A_super[0] = h[1] / 3.0 - (h[0] * h[0]) / (3.0 * h[1]);
        R_rhs[0] = rhs[1];
        
        for (int i = 2; i < n - 2; ++i) {
            int idx = i - 1;
            A_sub[idx] = h[i-1] / 3.0;
            A_diag[idx] = 2.0 * (h[i-1] + h[i]) / 3.0;
            A_super[idx] = h[i] / 3.0;
            R_rhs[idx] = rhs[i];
        }
        
        int idx_last = N_red - 1;
        A_sub[idx_last] = h[n-3] / 3.0 - (h[n-2] * h[n-2]) / (3.0 * h[n-3]);
        A_diag[idx_last] = 2.0 * (h[n-3] + h[n-2]) / 3.0 + h[n-2] * (h[n-3] + h[n-2]) / (3.0 * h[n-3]);
        R_rhs[idx_last] = rhs[n-2];
        
        std::vector<double> c_temp(N_red, 0.0);
        std::vector<double> d_temp(N_red, 0.0);
        
        c_temp[0] = A_super[0] / A_diag[0];
        d_temp[0] = R_rhs[0] / A_diag[0];
        
        for (int i = 1; i < N_red; ++i) {
            double denom = A_diag[i] - A_sub[i] * c_temp[i-1];
            c_temp[i] = A_super[i] / denom;
            d_temp[i] = (R_rhs[i] - A_sub[i] * d_temp[i-1]) / denom;
        }
        
        std::vector<double> c_red(N_red, 0.0);
        c_red[N_red - 1] = d_temp[N_red - 1];
        for (int i = N_red - 2; i >= 0; --i) {
            c_red[i] = d_temp[i] - c_temp[i] * c_red[i+1];
        }
        
        for (int i = 1; i < n - 1; ++i) {
            c[i] = c_red[i-1];
        }
        
        c[0] = (h[0] + h[1]) / h[1] * c[1] - h[0] / h[1] * c[2];
        c[n-1] = (h[n-3] + h[n-2]) / h[n-3] * c[n-2] - h[n-2] / h[n-3] * c[n-3];
        
        for (int i = 0; i < n - 1; ++i) {
            d[i] = (c[i+1] - c[i]) / (3.0 * h[i]);
            b[i] = (y[i+1] - y[i]) / h[i] - h[i] * (2.0 * c[i] + c[i+1]) / 3.0;
        }
        
        b[n-1] = 3.0 * d[n-2] * h[n-2] * h[n-2] + 2.0 * c[n-2] * h[n-2] + b[n-2];
        d[n-1] = 0.0;
    }
    
    double eval(double val) const {
        int n = x.size();
        if (n == 0) return 0.0;
        if (n < 4) return y[0];
        
        if (val <= x[0]) {
            double dx = val - x[0];
            return ((d[0] * dx + c[0]) * dx + b[0]) * dx + y[0];
        }
        if (val >= x[n-1]) {
            double dx = val - x[n-2];
            return ((d[n-2] * dx + c[n-2]) * dx + b[n-2]) * dx + y[n-2];
        }
        
        auto it = std::upper_bound(x.begin(), x.end(), val);
        int idx = std::distance(x.begin(), it) - 1;
        idx = std::max(0, std::min(idx, n - 2));
        
        double dx = val - x[idx];
        return ((d[idx] * dx + c[idx]) * dx + b[idx]) * dx + y[idx];
    }
};

// ---------------------------------------------------------
// Denoising Algorithm
// ---------------------------------------------------------
std::vector<double> denoise_trace(const std::vector<double>& trace, double denoise_sd = 2.0, int half_win = 5) {
    int n_pts = trace.size();
    std::vector<double> clean_trace = trace;
    
    for (int p = 0; p < n_pts; ++p) {
        int w_start = std::max(0, p - half_win);
        int w_end = std::min(n_pts, p + half_win + 1);
        
        std::vector<double> window;
        window.reserve(w_end - w_start);
        for (int i = w_start; i < w_end; ++i) {
            if (i != p) {
                window.push_back(trace[i]);
            }
        }
        
        if (window.empty()) continue;
        
        double local_median = compute_median(window);
        
        double mean = 0.0;
        for (double x : window) mean += x;
        mean /= window.size();
        double local_sd = compute_std(window, mean);
        
        if (std::abs(trace[p] - local_median) > denoise_sd * local_sd) {
            clean_trace[p] = local_median;
        }
    }
    return clean_trace;
}

// ---------------------------------------------------------
// Steepest Rise & Parameter Estimation
// ---------------------------------------------------------
struct AutoEstimateResults {
    std::vector<double> init_params; // [amp, t2p, fwhm, baseline]
    int start_idx;
    int end_idx;
    double sd_base;
    double click_start;
    double click_onset;
    double click_peak;
    double click_end;
};

AutoEstimateResults auto_estimate_params(const std::vector<double>& tr, const std::vector<double>& t_us, double fr, int up_f = 20) {
    int n = tr.size();
    
    int n_base_frames = std::min((int)std::round(2.0 * fr * up_f), (int)std::round(n * 0.1));
    n_base_frames = std::max(1, n_base_frames);
    
    std::vector<double> base_win(tr.begin(), tr.begin() + n_base_frames);
    double baseline = compute_median(base_win);
    
    double mean_base = 0.0;
    for (double x : base_win) mean_base += x;
    mean_base /= base_win.size();
    double sd_base = compute_std(base_win, mean_base);
    
    int ignore_points = std::min((int)(3.0 * fr * up_f), (int)(0.05 * n));
    int valid_end = n - ignore_points;
    
    std::vector<double> smoothed_rise = gaussian_filter1d(tr, 1.0 * fr * up_f);
    std::vector<double> deriv_rise = gradient(smoothed_rise);
    
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
    
    double thresh = baseline + std::max(3.0 * sd_base, 0.05 * amp);
    int start_idx = 0;
    for (int i = max_idx - 1; i >= 0; --i) {
        if (tr[i] < thresh) {
            start_idx = i;
            break;
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
        std::vector<double> deriv1 = gradient(smoothed_rise);
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
    std::vector<double> smoothed_end = gaussian_filter1d(tr, sigma_end);
    std::vector<double> deriv_end = gradient(smoothed_end);
    
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
    std::vector<double> smoothed_tr = gaussian_filter1d(tr, sigma_baseline);
    int n_end_frames = std::min((int)std::round(2.0 * fr * up_f), (int)std::round(valid_end * 0.1));
    n_end_frames = std::max(1, n_end_frames);
    
    std::vector<double> end_win(smoothed_tr.begin() + (valid_end - n_end_frames), smoothed_tr.begin() + valid_end);
    double end_baseline = compute_median(end_win);
    
    double mean_end = 0.0;
    for (double x : end_win) mean_end += x;
    mean_end /= end_win.size();
    double end_sd_base = compute_std(end_win, mean_end);
    
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
    
    return {
        {amp, t2p, fwhm, baseline},
        start_idx,
        end_idx,
        sd_base,
        t_us[0],
        t_start,
        t_us[max_idx],
        t_us[end_idx]
    };
}

// ---------------------------------------------------------
// Nonlinear Optimization Functor & Solver
// ---------------------------------------------------------
struct GammaFunctor {
    typedef double Scalar;
    enum {
        InputsAtCompileTime = Eigen::Dynamic,
        ValuesAtCompileTime = Eigen::Dynamic
    };
    typedef Eigen::VectorXd InputType;
    typedef Eigen::VectorXd ValueType;
    typedef Eigen::MatrixXd JacobianType;

    int inputs() const { return 4; }
    int values() const { return t.size(); }
    
    const std::vector<double>& t;
    const std::vector<double>& y;
    
    double m_init;
    double m_bound;
    
    bool use_cauchy;
    double f_scale;
    
    int operator()(const Eigen::VectorXd& x, Eigen::VectorXd& fvec) const {
        double a1 = 1e-6 + std::abs(x[0]);
        double peak1 = 1e-6 + std::abs(x[1]);
        double fwhm1 = 1e-6 + std::abs(x[2]);
        
        double L_m = m_init - m_bound;
        double U_m = m_init + m_bound;
        double m = L_m + (U_m - L_m) / (1.0 + std::exp(-x[3]));
        
        double alpha1 = ((peak1 * peak1) / (fwhm1 * fwhm1)) * 8.0 * std::log(2.0);
        double beta1 = ((fwhm1 * fwhm1) / peak1) / (8.0 * std::log(2.0));
        
        for (int i = 0; i < values(); ++i) {
            double ti = t[i];
            double model_val = m;
            if (ti > 0) {
                double base = ti / peak1;
                model_val = a1 * std::pow(base, alpha1) * std::exp(-(ti - peak1) / beta1) + m;
            }
            
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
        return 0;
    }
};

std::vector<double> run_nonlinear_fit(const std::vector<double>& t, const std::vector<double>& y, 
                                     const std::vector<double>& params_init, double sd_base, bool& success) {
    success = false;
    double m_init = params_init[3];
    double m_bound = std::max({0.5 * sd_base, 0.005 * m_init, 0.2});
    
    double L_m = m_init - m_bound;
    double U_m = m_init + m_bound;
    
    // Map initial guesses to search space
    Eigen::VectorXd x(4);
    x[0] = params_init[0] - 1e-6;
    x[1] = params_init[1] - 1e-6;
    x[2] = params_init[2] - 1e-6;
    
    double ratio = (U_m - L_m) / std::max(1e-9, m_init - L_m);
    double arg = std::max(1e-9, ratio - 1.0);
    x[3] = -std::log(arg);
    
    // Pass 1: Linear Least Squares
    GammaFunctor functor{t, y, m_init, m_bound, false, 1.0};
    Eigen::NumericalDiff<GammaFunctor> numDiff(functor);
    Eigen::LevenbergMarquardt<Eigen::NumericalDiff<GammaFunctor>, double> lm(numDiff);
    lm.parameters.maxfev = 2000;
    lm.parameters.xtol = 1e-10;
    lm.parameters.ftol = 1e-10;
    
    int info = lm.minimize(x);
    
    // Pass 1 solution
    double a1 = 1e-6 + std::abs(x[0]);
    double peak1 = 1e-6 + std::abs(x[1]);
    double fwhm1 = 1e-6 + std::abs(x[2]);
    double m = L_m + (U_m - L_m) / (1.0 + std::exp(-x[3]));
    
    // Calculate residuals and MAD
    double alpha1 = ((peak1 * peak1) / (fwhm1 * fwhm1)) * 8.0 * std::log(2.0);
    double beta1 = ((fwhm1 * fwhm1) / peak1) / (8.0 * std::log(2.0));
    
    std::vector<double> residuals(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        double ti = t[i];
        double model_val = m;
        if (ti > 0) {
            double base = ti / peak1;
            model_val = a1 * std::pow(base, alpha1) * std::exp(-(ti - peak1) / beta1) + m;
        }
        residuals[i] = y[i] - model_val;
    }
    
    double median_res = compute_median(residuals);
    std::vector<double> abs_res(residuals.size());
    for (size_t i = 0; i < residuals.size(); ++i) {
        abs_res[i] = std::abs(residuals[i] - median_res);
    }
    double mad = compute_median(abs_res) / 0.6745;
    double dynamic_f_scale = std::max(2.3849 * mad, 0.1);
    
    // Pass 2: Cauchy Robust Fit
    functor.use_cauchy = true;
    functor.f_scale = dynamic_f_scale;
    
    info = lm.minimize(x);
    if (info >= 1 && info <= 4) {
        success = true;
    }
    
    double final_a1 = 1e-6 + std::abs(x[0]);
    double final_peak1 = 1e-6 + std::abs(x[1]);
    double final_fwhm1 = 1e-6 + std::abs(x[2]);
    double final_m = L_m + (U_m - L_m) / (1.0 + std::exp(-x[3]));
    
    return {final_a1, final_peak1, final_fwhm1, final_m};
}

// ---------------------------------------------------------
// Scanline Polygon Fill (Rasterization)
// ---------------------------------------------------------
std::vector<int> get_mask_pixels(const std::vector<std::pair<double, double>>& poly, int width, int height) {
    std::vector<int> mask(width * height, 0);
    int n = poly.size();
    if (n < 3) return mask;
    
    for (int r = 0; r < height; ++r) {
        double y = (double)r;
        std::vector<double> intersections;
        for (int i = 0; i < n; ++i) {
            auto p1 = poly[i];
            auto p2 = poly[(i + 1) % n];
            if ((p1.second < y && p2.second >= y) || (p2.second < y && p1.second >= y)) {
                if (p2.second != p1.second) {
                    double x = p1.first + (y - p1.second) * (p2.first - p1.first) / (p2.second - p1.second);
                    intersections.push_back(x);
                }
            }
        }
        std::sort(intersections.begin(), intersections.end());
        for (size_t i = 0; i < intersections.size(); i += 2) {
            if (i + 1 >= intersections.size()) break;
            int x_start = std::max(0, (int)std::ceil(intersections[i]));
            int x_end = std::min(width - 1, (int)std::floor(intersections[i+1]));
            for (int c = x_start; c <= x_end; ++c) {
                mask[r * width + c] = 1;
            }
        }
    }
    return mask;
}

// ---------------------------------------------------------
// SVG Fit Visualizer Helpers
// ---------------------------------------------------------
struct NiceTicks {
    double step;
    std::vector<double> ticks;
};

inline NiceTicks get_nice_ticks(double min_val, double max_val, int max_ticks = 5) {
    NiceTicks nt;
    double range = max_val - min_val;
    if (range < 1e-6) {
        nt.step = 1.0;
        nt.ticks = {min_val};
        return nt;
    }
    double rough_step = range / static_cast<double>(max_ticks);
    double exponent = std::floor(std::log10(rough_step));
    double fraction = rough_step / std::pow(10.0, exponent);
    
    double nice_fraction;
    if (fraction < 1.5) nice_fraction = 1.0;
    else if (fraction < 3.0) nice_fraction = 2.0;
    else if (fraction < 7.0) nice_fraction = 5.0;
    else nice_fraction = 10.0;
    
    nt.step = nice_fraction * std::pow(10.0, exponent);
    
    double start_tick = std::ceil(min_val / nt.step) * nt.step;
    if (start_tick - nt.step >= min_val - 1e-9 * nt.step) {
        start_tick -= nt.step;
    }
    
    for (double tick = start_tick; tick <= max_val + 1e-9 * nt.step; tick += nt.step) {
        nt.ticks.push_back(tick);
    }
    return nt;
}

inline std::string format_tick(double val) {
    if (std::abs(val - std::round(val)) < 1e-7) {
        return std::to_string(static_cast<int>(std::round(val)));
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << val;
    std::string s = ss.str();
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

// ---------------------------------------------------------
// SVG Fit Visualizer
// ---------------------------------------------------------
void save_svg_plot(int roi_id, const std::string& tiff_path,
                   const std::vector<double>& tl_raw, const std::vector<double>& mfi_raw,
                   const std::vector<double>& mfi_denoised,
                   const std::vector<double>& tl_us, const std::vector<double>& y_us,
                   const FitRecord& rec, bool fit_success) {
    std::filesystem::path tiff_p(tiff_path);
    auto parent_dir = tiff_p.parent_path();
    auto stem = tiff_p.stem().string();
    auto plot_dir = parent_dir / "plots_cpp";
    
    // Create plots_cpp folder if not exists
    std::error_code ec;
    std::filesystem::create_directories(plot_dir, ec);
    
    auto svg_path = plot_dir / (stem + "_ROI_" + std::to_string(roi_id) + "_fit.svg");
    std::ofstream out(svg_path);
    if (!out.is_open()) return;

    // 1. Calculate boundaries
    double t_min = tl_raw.front();
    double t_max = tl_raw.back();
    
    double y_min = mfi_raw.front();
    double y_max = mfi_raw.front();
    for (double y : mfi_raw) {
        if (y < y_min) y_min = y;
        if (y > y_max) y_max = y;
    }
    for (double y : y_us) {
        if (y < y_min) y_min = y;
        if (y > y_max) y_max = y;
    }
    double y_range = y_max - y_min;
    if (y_range < 1e-6) y_range = 1.0;
    y_min -= 0.05 * y_range;
    y_max += 0.05 * y_range;
    y_range = y_max - y_min;

    // Canvas sizes
    const int W = 800;
    const int H = 500;
    const int margin_left = 75;
    const int margin_right = 35;
    const int margin_top = 60;
    const int margin_bottom = 65;
    const int plot_w = W - margin_left - margin_right;
    const int plot_h = H - margin_top - margin_bottom;

    auto X = [&](double t) {
        return margin_left + ((t - t_min) / (t_max - t_min)) * plot_w;
    };
    auto Y = [&](double y) {
        return margin_top + plot_h - ((y - y_min) / y_range) * plot_h;
    };

    out << std::fixed << std::setprecision(3);
    
    // 2. Start SVG document
    out << "<svg width=\"" << W << "\" height=\"" << H << "\" viewBox=\"0 0 " << W << " " << H 
        << "\" xmlns=\"http://www.w3.org/2000/svg\">\n";
        
    // Background rect
    out << "  <rect width=\"100%\" height=\"100%\" fill=\"#0d1117\" />\n";
    
    // Grid Lines (Horizontal - Nice Ticks)
    NiceTicks y_ticks = get_nice_ticks(y_min, y_max, 5);
    for (double y_val : y_ticks.ticks) {
        if (y_val < y_min || y_val > y_max) continue;
        double y_coord = Y(y_val);
        out << "  <line x1=\"" << margin_left << "\" y1=\"" << y_coord << "\" x2=\"" 
            << (W - margin_right) << "\" y2=\"" << y_coord << "\" stroke=\"#1f242c\" stroke-width=\"1\" />\n";
        out << "  <text x=\"" << (margin_left - 10) << "\" y=\"" << (y_coord + 4) 
            << "\" fill=\"#8b949e\" font-family=\"sans-serif\" font-size=\"11\" text-anchor=\"end\">" 
            << format_tick(y_val) << "</text>\n";
    }

    // Grid Lines (Vertical - Nice Ticks)
    NiceTicks x_ticks = get_nice_ticks(t_min, t_max, 6);
    for (double t_val : x_ticks.ticks) {
        if (t_val < t_min || t_val > t_max) continue;
        double x_coord = X(t_val);
        out << "  <line x1=\"" << x_coord << "\" y1=\"" << margin_top << "\" x2=\"" 
            << x_coord << "\" y2=\"" << (H - margin_bottom) << "\" stroke=\"#1f242c\" stroke-width=\"1\" />\n";
        out << "  <text x=\"" << x_coord << "\" y=\"" << (H - margin_bottom + 18) 
            << "\" fill=\"#8b949e\" font-family=\"sans-serif\" font-size=\"11\" text-anchor=\"middle\">" 
            << format_tick(t_val) << "s</text>\n";
    }

    // Axis titles
    out << "  <text x=\"" << (margin_left + plot_w / 2) << "\" y=\"" << (H - 15) 
        << "\" fill=\"#c9d1d9\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"13\" text-anchor=\"middle\">Time (seconds)</text>\n";
    out << "  <text x=\"" << 22 << "\" y=\"" << (margin_top + plot_h / 2) 
        << "\" fill=\"#c9d1d9\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"13\" text-anchor=\"middle\" transform=\"rotate(-90 " << 22 << " " << (margin_top + plot_h / 2) << ")\">Mean Fluorescence Intensity (MFI)</text>\n";

    // Bounding Box
    out << "  <rect x=\"" << margin_left << "\" y=\"" << margin_top << "\" width=\"" << plot_w 
        << "\" height=\"" << plot_h << "\" fill=\"none\" stroke=\"#30363d\" stroke-width=\"1.5\" />\n";

    // 3. Draw raw points
    for (size_t i = 0; i < tl_raw.size(); ++i) {
        out << "  <circle cx=\"" << X(tl_raw[i]) << "\" cy=\"" << Y(mfi_raw[i]) 
            << "\" r=\"2\" fill=\"#8b949e\" opacity=\"0.6\" />\n";
    }

    // 4. Draw denoised line
    out << "  <path d=\"M";
    for (size_t i = 0; i < tl_raw.size(); ++i) {
        out << " " << X(tl_raw[i]) << "," << Y(mfi_denoised[i]);
        if (i < tl_raw.size() - 1) out << " L";
    }
    out << "\" stroke=\"#58a6ff\" stroke-width=\"1.5\" fill=\"none\" opacity=\"0.7\" />\n";

    // 5. Draw upsampled spline
    out << "  <path d=\"M";
    for (size_t i = 0; i < tl_us.size(); ++i) {
        out << " " << X(tl_us[i]) << "," << Y(y_us[i]);
        if (i < tl_us.size() - 1) out << " L";
    }
    out << "\" stroke=\"#388bfd\" stroke-width=\"1.2\" stroke-dasharray=\"4,4\" fill=\"none\" opacity=\"0.8\" />\n";

    auto get_y_val_at_time = [&](double t, const std::vector<double>& times, const std::vector<double>& vals) -> double {
        if (std::isnan(t) || times.empty()) return NAN;
        double min_diff = 1e9;
        double best_val = vals.front();
        for (size_t i = 0; i < times.size(); ++i) {
            double diff = std::abs(times[i] - t);
            if (diff < min_diff) {
                min_diff = diff;
                best_val = vals[i];
            }
        }
        return best_val;
    };

    // 6. Draw markers (Circles instead of Lines)
    if (!std::isnan(rec.click_onset)) {
        double xo = X(rec.click_onset);
        double yo = Y(get_y_val_at_time(rec.click_onset, tl_us, y_us));
        out << "  <circle cx=\"" << xo << "\" cy=\"" << yo << "\" r=\"7.5\" fill=\"#39c5bb\" stroke=\"#0d1117\" stroke-width=\"2\" />\n";
        out << "  <text x=\"" << (xo + 10) << "\" y=\"" << (yo - 10) 
            << "\" fill=\"#39c5bb\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"12\">Onset</text>\n";
    }
    if (!std::isnan(rec.click_peak)) {
        double xp = X(rec.click_peak);
        double yp = Y(get_y_val_at_time(rec.click_peak, tl_us, y_us));
        out << "  <circle cx=\"" << xp << "\" cy=\"" << yp << "\" r=\"7.5\" fill=\"#d85fd3\" stroke=\"#0d1117\" stroke-width=\"2\" />\n";
        out << "  <text x=\"" << (xp + 10) << "\" y=\"" << (yp - 10) 
            << "\" fill=\"#d85fd3\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"12\">Peak</text>\n";
    }
    if (!std::isnan(rec.click_end)) {
        double xe = X(rec.click_end);
        double ye = Y(get_y_val_at_time(rec.click_end, tl_us, y_us));
        out << "  <circle cx=\"" << xe << "\" cy=\"" << ye << "\" r=\"7.5\" fill=\"#ff5555\" stroke=\"#0d1117\" stroke-width=\"2\" />\n";
        out << "  <text x=\"" << (xe + 10) << "\" y=\"" << (ye - 10) 
            << "\" fill=\"#ff5555\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"12\">End</text>\n";
    }

    // 7. Draw fitted curve
    if (fit_success && !std::isnan(rec.f_amp) && !std::isnan(rec.f_t2p) && !std::isnan(rec.f_fwhm) && !std::isnan(rec.f_m)) {
        double onset = rec.click_onset;
        double end = rec.click_end;
        double amp = rec.f_amp;
        double t2p = rec.f_t2p;
        double fwhm = rec.f_fwhm;
        double m = rec.f_m;

        double alpha = (t2p * t2p) / (fwhm * fwhm) * 8.0 * std::log(2.0);
        double beta = (fwhm * fwhm) / t2p * (1.0 / (8.0 * std::log(2.0)));
        
        out << "  <path d=\"M";
        bool first = true;
        const int steps = 250;
        for (int i = 0; i <= steps; ++i) {
            double t = t_min + i * ((end - t_min) / steps);
            double dt = t - onset;
            double val = m;
            if (dt > 0) {
                val = m + amp * std::pow(dt / t2p, alpha) * std::exp(-(dt - t2p) / beta);
            }
            if (first) {
                out << " " << X(t) << "," << Y(val);
                first = false;
            } else {
                out << " L " << X(t) << "," << Y(val);
            }
        }
        out << "\" stroke=\"#ff7b72\" stroke-width=\"3.5\" fill=\"none\" />\n";
    }

    // 8. Title
    out << "  <text x=\"" << margin_left << "\" y=\"" << (margin_top - 20) 
        << "\" fill=\"#f0f6fc\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"16\">" 
        << stem << " - ROI " << roi_id << " (C++ fit)</text>\n";

    // 9. Glassmorphism legend card
    double card_x = margin_left + 15;
    double card_y = margin_top + 15;
    double card_w = 180;
    double card_h = 145;
    out << "  <rect x=\"" << card_x << "\" y=\"" << card_y << "\" width=\"" << card_w 
        << "\" height=\"" << card_h << "\" rx=\"6\" fill=\"#161b22\" opacity=\"0.85\" stroke=\"#30363d\" stroke-width=\"1.5\" />\n";
    
    out << "  <text x=\"" << (card_x + 12) << "\" y=\"" << (card_y + 22) << "\" fill=\"#f0f6fc\" font-family=\"sans-serif\" font-size=\"12\" font-weight=\"bold\">Fit Results:</text>\n";
    if (fit_success) {
        out << "  <text x=\"" << (card_x + 12) << "\" y=\"" << (card_y + 45) << "\" fill=\"#79c0ff\" font-family=\"sans-serif\" font-size=\"12\">Amp: " << format_tick(rec.f_amp) << "</text>\n";
        out << "  <text x=\"" << (card_x + 12) << "\" y=\"" << (card_y + 68) << "\" fill=\"#79c0ff\" font-family=\"sans-serif\" font-size=\"12\">T2p: " << format_tick(rec.f_t2p) << " s</text>\n";
        out << "  <text x=\"" << (card_x + 12) << "\" y=\"" << (card_y + 91) << "\" fill=\"#79c0ff\" font-family=\"sans-serif\" font-size=\"12\">FWHM: " << format_tick(rec.f_fwhm) << " s</text>\n";
        out << "  <text x=\"" << (card_x + 12) << "\" y=\"" << (card_y + 114) << "\" fill=\"#79c0ff\" font-family=\"sans-serif\" font-size=\"12\">Base: " << format_tick(rec.f_m) << "</text>\n";
        out << "  <text x=\"" << (card_x + 12) << "\" y=\"" << (card_y + 137) << "\" fill=\"#79c0ff\" font-family=\"sans-serif\" font-size=\"12\">SNR: " << format_tick(rec.f_snr) << "</text>\n";
    } else {
        out << "  <text x=\"" << (card_x + 12) << "\" y=\"" << (card_y + 45) << "\" fill=\"#f85149\" font-family=\"sans-serif\" font-size=\"12\">FIT FAILED</text>\n";
    }

    out << "</svg>\n";
    out.close();
}

// ---------------------------------------------------------
// Pipeline Execution for a single ROI
// ---------------------------------------------------------
FitRecord process_single_roi(int roi_id, const std::vector<std::pair<double, double>>& poly,
                             const std::vector<std::vector<float>>& frames, int width, int height,
                             double fr, int up_f, const std::string& tiff_path, bool enable_plots) {
    // 1. Rasterize Mask
    std::vector<int> mask = get_mask_pixels(poly, width, height);
    
    // Calculate size and mean intensity
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
    
    // 2. Denoise
    std::vector<double> mfi_denoised = denoise_trace(mfi_raw);
    
    // 3. Spline upsample
    std::vector<double> tl_raw(mfi_raw.size());
    for (size_t i = 0; i < tl_raw.size(); ++i) tl_raw[i] = i / fr;
    
    std::vector<double> tl_us(mfi_raw.size() * up_f);
    for (size_t i = 0; i < tl_us.size(); ++i) tl_us[i] = i / (fr * up_f);
    
    Spline spline;
    spline.build(tl_raw, mfi_denoised);
    
    std::vector<double> y_us(tl_us.size());
    for (size_t i = 0; i < tl_us.size(); ++i) y_us[i] = spline.eval(tl_us[i]);
    
    // 4. Estimate Parameters
    AutoEstimateResults auto_res = auto_estimate_params(y_us, tl_us, fr, up_f);
    
    // 5. Fit
    int start_idx = auto_res.start_idx;
    int end_idx = auto_res.end_idx;
    
    std::vector<double> t_fit(end_idx - start_idx);
    std::vector<double> y_fit(end_idx - start_idx);
    for (int i = start_idx; i < end_idx; ++i) {
        t_fit[i - start_idx] = tl_us[i] - tl_us[start_idx];
        y_fit[i - start_idx] = y_us[i];
    }
    
    bool fit_success = false;
    std::vector<double> popt = run_nonlinear_fit(t_fit, y_fit, auto_res.init_params, auto_res.sd_base, fit_success);
    
    FitRecord rec;
    rec.roi_id = roi_id;
    rec.init_amp = auto_res.init_params[0];
    rec.init_t2p = auto_res.init_params[1];
    rec.init_fwhm = auto_res.init_params[2];
    rec.init_m = auto_res.init_params[3];
    rec.init_snr = (auto_res.sd_base > 0.0) ? (auto_res.init_params[0] / auto_res.sd_base) : NAN;
    rec.click_start = auto_res.click_start;
    rec.click_onset = auto_res.click_onset;
    rec.click_peak = auto_res.click_peak;
    rec.click_end = auto_res.click_end;
    
    if (fit_success) {
        rec.f_amp = popt[0];
        rec.f_t2p = popt[1];
        rec.f_fwhm = popt[2];
        rec.f_m = popt[3];
        rec.f_snr = (auto_res.sd_base > 0.0) ? (popt[0] / auto_res.sd_base) : NAN;
    } else {
        rec.f_amp = NAN;
        rec.f_t2p = NAN;
        rec.f_fwhm = NAN;
        rec.f_m = NAN;
        rec.f_snr = NAN;
    }
    
    // Save vector SVG plot of raw points, denoised spline, markers, and fitted curve
    if (enable_plots) {
        save_svg_plot(roi_id, tiff_path, tl_raw, mfi_raw, mfi_denoised, tl_us, y_us, rec, fit_success);
    }
    
    return rec;
}

// ---------------------------------------------------------
// Main Function
// ---------------------------------------------------------
bool process_dataset_file(const std::string& tiff_path, const std::string& rois_path, double fr, int up_f, const std::string& out_csv, bool enable_plots) {
    std::cout << "Starting C++ Bolus Tracking for: " << tiff_path << std::endl;
    
    // 1. Read TIFF Stack
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
    
    // 2. Read ROIs
    std::ifstream rois_file(rois_path);
    if (!rois_file.is_open()) {
        std::cerr << "Failed to open ROIs file: " << rois_path << std::endl;
        return false;
    }
    
    int n_rois = 0;
    rois_file >> n_rois;
    std::vector<ROI> rois(n_rois);
    for (int i = 0; i < n_rois; ++i) {
        int roi_id, n_pts;
        rois_file >> roi_id >> n_pts;
        rois[i].id = roi_id;
        rois[i].poly.resize(n_pts);
        for (int j = 0; j < n_pts; ++j) {
            rois_file >> rois[i].poly[j].first >> rois[i].poly[j].second;
        }
    }
    rois_file.close();
    std::cout << "Loaded " << n_rois << " ROIs" << std::endl;
    
    // 3. Process ROIs in Parallel
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<std::future<FitRecord>> futures;
    futures.reserve(n_rois);
    
    for (int i = 0; i < n_rois; ++i) {
        futures.push_back(std::async(std::launch::async, process_single_roi,
                                     rois[i].id + 1, rois[i].poly, std::ref(frames),
                                     (int)width, (int)height, fr, up_f, tiff_path, enable_plots));
    }
    
    std::vector<FitRecord> results;
    results.reserve(n_rois);
    for (auto& f : futures) {
        results.push_back(f.get());
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;
    std::cout << "Parallel fitting complete in " << diff.count() << " seconds!" << std::endl;
    
    // 4. Save CSV results
    std::ofstream out(out_csv);
    if (!out.is_open()) {
        std::cerr << "Failed to open output CSV: " << out_csv << std::endl;
        return false;
    }
    
    out << "ROI,InitAmp,InitT2p,InitFWHM,InitM,InitSNR,Click1_Start_T,Click2_Onset_T,Click3_Peak_T,Click4_End_T,"
        << "F_Amp,F_T2p,F_FWHM,F_M,F_SNR\n";
    
    for (const auto& rec : results) {
        out << rec.roi_id << ","
            << rec.init_amp << "," << rec.init_t2p << "," << rec.init_fwhm << "," << rec.init_m << "," << rec.init_snr << ","
            << rec.click_start << "," << rec.click_onset << "," << rec.click_peak << "," << rec.click_end << ","
            << rec.f_amp << "," << rec.f_t2p << "," << rec.f_fwhm << "," << rec.f_m << "," << rec.f_snr << "\n";
    }
    out.close();
    std::cout << "Saved C++ results to: " << out_csv << std::endl;
    return true;
}

double parse_frame_rate(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) return 0.0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("\"T Dimension\"") != std::string::npos) {
            size_t pos = line.find("\"T Dimension\"");
            pos = line.find("\"", pos + 13);
            if (pos == std::string::npos) continue;
            std::string val = line.substr(pos + 1);
            std::stringstream ss(val);
            double frames = 0, t_start = 0, t_end = 0;
            char comma = 0, dash = 0;
            ss >> frames >> comma >> t_start >> dash >> t_end;
            if (frames > 0 && t_end > t_start) {
                return std::round((frames / (t_end - t_start)) * 100.0) / 100.0;
            }
        }
    }
    return 0.0;
}

std::string extract_identifier(const std::string& filename) {
    std::regex re("(bolus\\d+[-_](baseline|co2))", std::regex_constants::icase);
    std::smatch m;
    if (std::regex_search(filename, m, re)) {
        std::string id = m.str(1);
        std::replace(id.begin(), id.end(), '-', '_');
        return id;
    }
    return "";
}

bool contains_ignored_pattern(const std::string& path) {
    std::vector<std::string> ignores = {"mips", "results", "shift_info", "max_"};
    for (const auto& pat : ignores) {
        if (path.find(pat) != std::string::npos) return true;
    }
    return false;
}

int main(int argc, char** argv) {
    bool enable_plots = false;
    bool folder_mode = false;
    std::string folder_path = "";
    std::vector<std::string> pos_args;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--plot") {
            enable_plots = true;
        } else if (arg == "--folder") {
            folder_mode = true;
            if (i + 1 < argc) {
                folder_path = argv[i + 1];
                i++; // skip next arg
            }
        } else {
            pos_args.push_back(arg);
        }
    }
    
    if (folder_mode) {
        if (folder_path.empty()) {
            std::cerr << "Error: --folder requires a path." << std::endl;
            return 1;
        }
        if (!std::filesystem::exists(folder_path)) {
            std::cerr << "Folder does not exist: " << folder_path << std::endl;
            return 1;
        }
        
        std::cout << "Pure C++ Pipeline - Scanning: " << folder_path << std::endl;
        if (enable_plots) {
            std::cout << "Plotting enabled. Fits will be saved to plots_cpp/ folder." << std::endl;
        } else {
            std::cout << "Plotting disabled. Only results CSVs will be generated." << std::endl;
        }
        
        // 1. Scan for all ROI and Metadata files first
        std::map<std::string, std::string> rois_map;
        std::map<std::string, std::string> meta_map;
        
        for (const auto& entry : std::filesystem::recursive_directory_iterator(folder_path)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                if (ext == ".txt") {
                    std::string filename = entry.path().filename().string();
                    if (filename.empty() || filename.front() == '.') continue;
                    
                    std::string identifier = extract_identifier(filename);
                    if (identifier.empty()) continue;
                    
                    std::transform(identifier.begin(), identifier.end(), identifier.begin(), ::tolower);
                    
                    std::string filename_lower = filename;
                    std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);
                    
                    if (filename_lower.find("_rois.txt") != std::string::npos || filename_lower.find("_rois_cpp.txt") != std::string::npos) {
                        rois_map[identifier] = entry.path().string();
                    } else if (filename_lower.find("_rois") == std::string::npos) {
                        meta_map[identifier] = entry.path().string();
                    }
                }
            }
        }
        
        // 2. Scan for all TIFF files
        std::vector<std::filesystem::path> tiff_files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(folder_path)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.empty() || filename.front() == '.') continue;
                
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".tif" || ext == ".tiff") {
                    std::string p_str = entry.path().string();
                    std::transform(p_str.begin(), p_str.end(), p_str.begin(), ::tolower);
                    if (!contains_ignored_pattern(p_str)) {
                        tiff_files.push_back(entry.path());
                    }
                }
            }
        }
        
        std::cout << "Found " << tiff_files.size() << " TIFF files to process." << std::endl;
        int processed_count = 0;
        
        for (const auto& tiff_path : tiff_files) {
            std::string filename = tiff_path.filename().string();
            std::string identifier = extract_identifier(filename);
            if (identifier.empty()) {
                std::cout << "Skipping non-bolus TIFF: " << filename << std::endl;
                continue;
            }
            
            std::string id_lower = identifier;
            std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(), ::tolower);
            
            if (rois_map.find(id_lower) == rois_map.end() || meta_map.find(id_lower) == meta_map.end()) {
                std::cerr << "Warning: Could not find matching rois.txt or metadata.txt for " << filename 
                          << " (rois: " << (rois_map.find(id_lower) == rois_map.end() ? "missing" : "found")
                          << ", meta: " << (meta_map.find(id_lower) == meta_map.end() ? "missing" : "found") << "). Skipping." << std::endl;
                continue;
            }
            
            std::string rois_file = rois_map[id_lower];
            std::string meta_file = meta_map[id_lower];
            
            double fr = parse_frame_rate(meta_file);
            if (fr <= 0.0) {
                std::cerr << "Warning: Failed to parse frame rate from " << meta_file << ". Skipping." << std::endl;
                continue;
            }
            
            std::string out_csv = tiff_path.parent_path().string() + "/" + tiff_path.stem().string() + "_results_cpp.csv";
            
            std::cout << "\n==================================================" << std::endl;
            std::cout << "Processing bolus: " << identifier << std::endl;
            std::cout << "TIFF: " << tiff_path.string() << std::endl;
            std::cout << "ROIs: " << rois_file << std::endl;
            std::cout << "Metadata: " << meta_file << " (Frame Rate: " << fr << " Hz)" << std::endl;
            std::cout << "Output: " << out_csv << std::endl;
            std::cout << "==================================================" << std::endl;
            
            process_dataset_file(tiff_path.string(), rois_file, fr, 20, out_csv, enable_plots);
            processed_count++;
        }
        
        std::cout << "\nPure C++ Processing Complete! Successfully processed " << processed_count << " datasets." << std::endl;
        return 0;
    } 
    
    if (pos_args.size() >= 5) {
        std::string tiff_path = pos_args[0];
        std::string rois_path = pos_args[1];
        double fr = std::stod(pos_args[2]);
        int up_f = std::stoi(pos_args[3]);
        std::string out_csv = pos_args[4];
        
        bool success = process_dataset_file(tiff_path, rois_path, fr, up_f, out_csv, enable_plots);
        return success ? 0 : 1;
    }
    
    std::cerr << "Usage for single file:\n  " << argv[0] << " <tiff_path> <rois_txt_path> <fr> <up_f> <out_csv_path> [--plot]\n"
              << "Usage for folder batch processing:\n  " << argv[0] << " --folder <path_to_folder> [--plot]" << std::endl;
    return 1;
}
