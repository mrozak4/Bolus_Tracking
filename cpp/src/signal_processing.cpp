#include "bolus_tracking_cpp.hpp"

#include <cmath>
#include <algorithm>
#include <vector>

// ---------------------------------------------------------
// SignalProcessor Implementation
// ---------------------------------------------------------

/**
 * @brief Computes the median of a vector of doubles.
 * @param v The vector of values.
 * @return The median value.
 */
double SignalProcessor::compute_median(std::vector<double> v) {
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

/**
 * @brief Computes the sample standard deviation (ddof=1) of a vector of doubles.
 * @param v The vector of values.
 * @param mean The precomputed mean of the values.
 * @return The standard deviation.
 */
double SignalProcessor::compute_std(const std::vector<double>& v, double mean) {
    if (v.size() <= 1) return 0.0;
    double sum_sq = 0.0;
    for (double x : v) {
        sum_sq += (x - mean) * (x - mean);
    }
    return std::sqrt(sum_sq / (v.size() - 1.0)); // ddof=1
}

/**
 * @brief Reflects an out-of-bounds index for boundary padding (symmetric boundary).
 * @param idx The index to reflect.
 * @param n The size of the sequence.
 * @return The reflected in-bounds index.
 */
int SignalProcessor::reflect_index(int idx, int n) {
    if (idx < 0) {
        return -idx;
    } else if (idx >= n) {
        return 2 * n - 2 - idx;
    }
    return idx;
}

/**
 * @brief Applies a 1D Gaussian filter to a trace.
 * @param tr The input trace.
 * @param sigma The standard deviation of the Gaussian filter.
 * @return The filtered trace.
 */
std::vector<double> SignalProcessor::gaussian_filter1d(const std::vector<double>& tr, double sigma) {
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

/**
 * @brief Computes the central difference gradient for a 1D trace.
 * @param tr The input trace.
 * @return The gradient trace.
 */
std::vector<double> SignalProcessor::gradient(const std::vector<double>& tr) {
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

/**
 * @brief Replaces outliers in a 1D trace with their local median.
 * @param trace The input raw trace.
 * @param denoise_sd Standard deviation threshold factor for defining an outlier.
 * @param half_win Half window width for local statistics.
 * @return The denoised trace.
 */
std::vector<double> SignalProcessor::denoise_trace(const std::vector<double>& trace, double denoise_sd, int half_win) {
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
// SplineInterpolator Implementation
// ---------------------------------------------------------

/**
 * @brief Constructs a cubic spline representation for 1D interpolation.
 * @param px The input independent variable values (sorted).
 * @param py The input dependent variable values.
 */
void SplineInterpolator::build(const std::vector<double>& px, const std::vector<double>& py) {
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

/**
 * @brief Evaluates the cubic spline interpolation at a specific value.
 * @param val The position to evaluate.
 * @return The interpolated value.
 */
double SplineInterpolator::eval(double val) const {
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
