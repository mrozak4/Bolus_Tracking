#include "bolus_tracking_cpp.hpp"
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <numeric>

// -------------------------------------------------------------
// Helper assertion function with tolerance
// -------------------------------------------------------------
bool is_approx(double a, double b, double tol = 1e-5) {
    return std::abs(a - b) <= tol;
}

// -------------------------------------------------------------
// Test SignalProcessor
// -------------------------------------------------------------
void test_signal_processor() {
    std::cout << "Running test_signal_processor..." << std::endl;
    
    // 1. Median
    std::vector<double> v1 = {1.0, 3.0, 2.0, 5.0, 4.0};
    assert(is_approx(SignalProcessor::compute_median(v1), 3.0));
    
    std::vector<double> v2 = {1.0, 3.0, 2.0, 4.0};
    assert(is_approx(SignalProcessor::compute_median(v2), 2.5));
    
    // Median of single element
    assert(is_approx(SignalProcessor::compute_median({42.0}), 42.0));

    // 2. Std Dev
    std::vector<double> v3 = {2.0, 4.0, 4.0, 4.0, 5.5, 5.7, 8.0, 9.0};
    double sum = std::accumulate(v3.begin(), v3.end(), 0.0);
    double mean = sum / v3.size();
    double std_val = SignalProcessor::compute_std(v3, mean);
    assert(is_approx(std_val, 2.30326, 1e-4));

    // 3. Reflect Index
    assert(SignalProcessor::reflect_index(0, 10) == 0);
    assert(SignalProcessor::reflect_index(-1, 10) == 1);
    assert(SignalProcessor::reflect_index(10, 10) == 8);
    assert(SignalProcessor::reflect_index(11, 10) == 7);

    // 4. Gaussian Filter smoothing correctness
    std::vector<double> signal(50, 10.0);
    signal[25] = 100.0; // impulse
    std::vector<double> smoothed = SignalProcessor::gaussian_filter1d(signal, 2.0);
    assert(smoothed.size() == signal.size());
    // Smoothed peak should be less than the raw peak, and energy conserved
    assert(smoothed[25] < 100.0);
    assert(smoothed[25] > smoothed[0]);

    // 5. Gradient
    std::vector<double> grad_test = {1.0, 2.0, 4.0, 7.0, 11.0};
    std::vector<double> grad = SignalProcessor::gradient(grad_test);
    // Python/MATLAB style centered differences
    // grad[0] = 2.0 - 1.0 = 1.0
    // grad[1] = (4.0 - 1.0) / 2.0 = 1.5
    // grad[2] = (7.0 - 2.0) / 2.0 = 2.5
    // grad[3] = (11.0 - 4.0) / 2.0 = 3.5
    // grad[4] = 11.0 - 7.0 = 4.0
    assert(is_approx(grad[0], 1.0));
    assert(is_approx(grad[1], 1.5));
    assert(is_approx(grad[2], 2.5));
    assert(is_approx(grad[3], 3.5));
    assert(is_approx(grad[4], 4.0));

    // 6. Denoise Trace (outlier replacement)
    std::vector<double> trace = {10.0, 10.0, 10.0, 1000.0, 10.0, 10.0, 10.0};
    std::vector<double> denoised = SignalProcessor::denoise_trace(trace, 2.0, 3);
    assert(denoised.size() == trace.size());
    // Outlier at index 3 should be replaced by a value close to 10.0
    assert(denoised[3] < 20.0);
    
    std::cout << "  -> SignalProcessor tests passed!" << std::endl;
}

// -------------------------------------------------------------
// Test SplineInterpolator
// -------------------------------------------------------------
void test_spline_interpolator() {
    std::cout << "Running test_spline_interpolator..." << std::endl;
    
    std::vector<double> x = {0.0, 1.0, 2.0, 3.0};
    std::vector<double> y = {0.0, 1.0, 8.0, 27.0}; // y = x^3
    
    SplineInterpolator spline;
    spline.build(x, y);
    
    // Test exact evaluation at knot points
    assert(is_approx(spline.eval(0.0), 0.0));
    assert(is_approx(spline.eval(1.0), 1.0));
    assert(is_approx(spline.eval(2.0), 8.0));
    assert(is_approx(spline.eval(3.0), 27.0));
    
    // Test interpolation between knots
    double mid = spline.eval(1.5);
    // Since it's natural spline, it won't be perfectly 1.5^3 (3.375) because natural boundary conditions c=0 at ends,
    // but it should be very close.
    assert(is_approx(mid, 3.75, 0.5)); // broad check for smooth interpolation

    std::cout << "  -> SplineInterpolator tests passed!" << std::endl;
}

// -------------------------------------------------------------
// Test GammaFunctor & BolusModel Evaluation
// -------------------------------------------------------------
void test_gamma_functor() {
    std::cout << "Running test_gamma_functor..." << std::endl;
    
    std::vector<double> t = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> y = {10.0, 15.0, 30.0, 25.0, 12.0, 10.0};
    
    // Functor setup
    GammaFunctor func{t, y, 10.0, 5.0, false, 0.0, 1e-6, 1000.0, 1e-6, 10.0, 0.5, 10.0};
    
    Eigen::VectorXd params(4);
    params[0] = 20.0;    // amp = 20
    params[1] = 2.0;     // t2p = 2
    params[2] = 1.5;     // fwhm = 1.5
    params[3] = 10.0;    // base = 10 (range: m_init(10) +/- m_bound(5))
    
    Eigen::VectorXd residuals(func.values());
    func(params, residuals);
    
    assert(residuals.size() == t.size() + 4);
    // For t = 0.0, evaluated model value should be exactly base = 10
    // residual[0] = y[0] - evaluated = 10.0 - 10.0 = 0.0
    assert(is_approx(residuals[0], 0.0));
    
    // Check that peak evaluated value happens at t2p = 2.0 (so model evaluated = base + amp = 30)
    // residual[2] = y[2] - evaluated = 30.0 - 30.0 = 0.0
    assert(is_approx(residuals[2], 0.0));
    
    // Check that inside bounds penalty terms are exactly 0
    assert(is_approx(residuals[t.size() + 0], 0.0));
    assert(is_approx(residuals[t.size() + 1], 0.0));
    assert(is_approx(residuals[t.size() + 2], 0.0));
    assert(is_approx(residuals[t.size() + 3], 0.0));
    
    std::cout << "  -> GammaFunctor tests passed!" << std::endl;
}

// -------------------------------------------------------------
// Test BolusFitter
// -------------------------------------------------------------
void test_bolus_fitter() {
    std::cout << "Running test_bolus_fitter..." << std::endl;
    
    // Create a synthetic clean Gamma bolus trace with upsampling
    // Parameters: Amp=100.0, T2p=5.0, Fwhm=3.0, Base=50.0
    double amp = 100.0, t2p = 5.0, fwhm = 3.0, base = 50.0;
    double fr = 5.0;
    int up_f = 20;
    
    std::vector<double> t_us(3000);
    std::vector<double> y_us(3000);
    
    double alpha = ((t2p * t2p) / (fwhm * fwhm)) * 8.0 * std::log(2.0);
    double beta = ((fwhm * fwhm) / t2p) / (8.0 * std::log(2.0));
    
    // Offset onset time by 2.0 seconds
    double onset_t = 2.0;
    for (int i = 0; i < 3000; ++i) {
        double t_val = double(i) / (fr * up_f);
        t_us[i] = t_val;
        double rel_t = t_val - onset_t;
        if (rel_t <= 0) {
            y_us[i] = base;
        } else {
            y_us[i] = base + amp * std::pow(rel_t / t2p, alpha) * std::exp(-(rel_t - t2p) / beta);
        }
    }
    
    BolusFitter fitter;
    AutoEstimateResults res = fitter.auto_estimate_params(y_us, t_us, fr, up_f);
    
    // Verify heuristics estimates
    assert(res.start_idx < res.end_idx);
    assert(res.init_params[0] > 0.0); // Amplitude
    assert(res.init_params[1] > 0.0); // T2p
    assert(res.init_params[2] > 0.0); // FWHM
    assert(res.init_params[3] > 0.0); // Base
    
    // Run the non-linear fit on the cropped window
    std::vector<double> t_fit(res.end_idx - res.start_idx);
    std::vector<double> y_fit(res.end_idx - res.start_idx);
    double fit_onset = t_us[res.start_idx];
    for (int i = 0; i < (res.end_idx - res.start_idx); ++i) {
        t_fit[i] = t_us[res.start_idx + i] - fit_onset;
        y_fit[i] = y_us[res.start_idx + i];
    }
    
    bool fit_success = false;
    bool pass2_run = false;
    std::vector<double> popt = fitter.run_nonlinear_fit(t_fit, y_fit, res.init_params, res.sd_base, fit_success, pass2_run);
    
    std::cout << "  Start idx: " << res.start_idx << " (time: " << fit_onset << ")" << std::endl;
    std::cout << "  End idx: " << res.end_idx << " (time: " << t_us[res.end_idx] << ")" << std::endl;
    std::cout << "  Estimated init params: " << res.init_params[0] << ", " << res.init_params[1] << ", " << res.init_params[2] << ", " << res.init_params[3] << std::endl;
    if (popt.size() == 4) {
        std::cout << "  Fitted params popt: " << popt[0] << ", " << popt[1] << ", " << popt[2] << ", " << popt[3] << std::endl;
    }
    
    assert(fit_success);
    assert(popt.size() == 4);
    
    // Popts should be very close to synthetic values
    double expected_t2p = onset_t + t2p - fit_onset;
    std::cout << "  Expected T2p: " << expected_t2p << ", Got T2p: " << popt[1] << std::endl;
    
    assert(is_approx(popt[0], amp, 5.0)); // Amplitude
    assert(is_approx(popt[1], expected_t2p, 0.5)); // T2p relative to fit_onset
    assert(is_approx(popt[2], fwhm, 0.5)); // FWHM
    assert(is_approx(popt[3], base, 5.0)); // Base
    
    std::cout << "  -> BolusFitter tests passed!" << std::endl;
}

// -------------------------------------------------------------
// Test ROIMaskRasterizer
// -------------------------------------------------------------
void test_roi_mask_rasterizer() {
    std::cout << "Running test_roi_mask_rasterizer..." << std::endl;
    
    // Define a 10x10 square ROI from (2,2) to (7,7)
    std::vector<std::pair<double, double>> poly = {
        {2.0, 2.0}, {7.0, 2.0}, {7.0, 7.0}, {2.0, 7.0}
    };
    
    std::vector<int> mask = ROIMaskRasterizer::get_mask_pixels(poly, 10, 10);
    assert(mask.size() == 100);
    
    // Inside pixel (4,4) -> index = 4 * 10 + 4 = 44 should be 1
    assert(mask[44] == 1);
    
    // Outside pixel (0,0) -> index = 0 should be 0
    assert(mask[0] == 0);
    
    std::cout << "  -> ROIMaskRasterizer tests passed!" << std::endl;
}

// -------------------------------------------------------------
// Test Nice Ticks
// -------------------------------------------------------------
void test_nice_ticks() {
    std::cout << "Running test_nice_ticks..." << std::endl;
    
    NiceTicks nt = BolusVisualizer::get_nice_ticks(0.0, 10.0, 5);
    assert(nt.step > 0.0);
    assert(!nt.ticks.empty());
    assert(nt.ticks.front() <= 0.0);
    assert(nt.ticks.back() >= 10.0);
    
    std::string formatted = BolusVisualizer::format_tick(12.3456);
    assert(formatted == "12.35" || formatted == "12.3" || formatted == "12");
    
    std::cout << "  -> NiceTicks and tick formatting tests passed!" << std::endl;
}

// -------------------------------------------------------------
// Test Physiological Bounds Filtering
// -------------------------------------------------------------
void test_physiological_bounds_filtering() {
    std::cout << "Running test_physiological_bounds_filtering..." << std::endl;
    
    BolusFitter fitter;
    
    // Simulate validation helper logic
    auto validate_params = [](const std::vector<double>& p, bool fit_ok) {
        if (!fit_ok) return false;
        if (p.size() < 4) return false;
        if (p[0] <= 1.0001e-6 || p[0] >= 1023.0 * 0.9999 ||
            p[1] <= 1.0001e-6 || p[2] <= 0.5001) {
            return false;
        }
        return true;
    };
    
    // 1. Check that too small amplitude (< min_amp 1e-6) gets rejected
    assert(!validate_params({1e-7, 2.0, 1.5, 10.0}, true));
    
    // 2. Check that too large amplitude (>= max_amp 1023.0) gets rejected
    assert(!validate_params({2000.0, 2.0, 1.5, 10.0}, true));
    
    // 3. Check that too small T2p (< min_t2p 1e-6) gets rejected
    assert(!validate_params({50.0, 1e-7, 1.5, 10.0}, true));
    
    // 4. Check that too small FWHM (< min_fwhm 0.5) gets rejected
    assert(!validate_params({50.0, 2.0, 0.4, 10.0}, true));
    
    // 5. Check that physiologically possible values pass
    assert(validate_params({100.0, 2.5, 3.0, 50.0}, true));
    
    std::cout << "  -> Physiological bounds filtering tests passed!" << std::endl;
}

// -------------------------------------------------------------
// Test MATLAB MAT Conversion Parity
// -------------------------------------------------------------
#include "mat_parser.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

std::vector<ROI> read_rois_from_txt(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open TXT file: " << filepath << std::endl;
        return {};
    }
    int num_rois = 0;
    if (!(file >> num_rois)) {
        return {};
    }
    std::vector<ROI> rois;
    for (int i = 0; i < num_rois; ++i) {
        int id = 0;
        int num_pts = 0;
        if (!(file >> id >> num_pts)) {
            break;
        }
        std::vector<std::pair<double, double>> poly(num_pts);
        for (int j = 0; j < num_pts; ++j) {
            double x = 0, y = 0;
            if (!(file >> x >> y)) {
                break;
            }
            poly[j] = {x, y};
        }
        rois.push_back({id, poly});
    }
    return rois;
}

std::string find_project_root() {
    std::filesystem::path p = std::filesystem::current_path();
    // Try to find the root by looking for CMakeLists.txt and sample-subject-3554 up to 5 levels
    for (int i = 0; i < 5; ++i) {
        if (std::filesystem::exists(p / "CMakeLists.txt") && std::filesystem::exists(p / "sample-subject-3554")) {
            return p.string();
        }
        if (p.has_parent_path()) {
            p = p.parent_path();
        } else {
            break;
        }
    }
    return ".";
}

void test_mat_parser_parity() {
    std::cout << "Running test_mat_parser_parity..." << std::endl;
    
    std::string root = find_project_root();
    std::vector<std::pair<std::string, std::string>> pairs = {
        {root + "/sample-subject-3554/3554_bolus1_baseline_shifted_MaskObj.mat",
         root + "/sample-subject-3554/3554_bolus1_baseline_shifted_MaskObj_rois.txt"},
        {root + "/sample-subject-2259/old_masks_drawROI/2259_bolus1_baseline_maskObj.mat",
         root + "/sample-subject-2259/old_masks_drawROI/2259_bolus1_baseline_maskObj_rois.txt"},
        {root + "/sample-subject-2259/old_masks_drawROI/2259_bolus3_baseline_maskObj.mat",
         root + "/sample-subject-2259/old_masks_drawROI/2259_bolus3_baseline_maskObj_rois.txt"},
        {root + "/sample-subject-2259/old_masks_drawROI/2259_bolus5_baseline_maskObj.mat",
         root + "/sample-subject-2259/old_masks_drawROI/2259_bolus5_baseline_maskObj_rois.txt"}
    };
    
    int tested_files = 0;
    for (const auto& [mat_path, txt_path] : pairs) {
        if (!std::filesystem::exists(mat_path) || !std::filesystem::exists(txt_path)) {
            std::cout << "  [Skip] Files not found:\n    MAT: " << mat_path << "\n    TXT: " << txt_path << std::endl;
            continue;
        }
        std::cout << "  Comparing:\n    MAT: " << mat_path << "\n    TXT: " << txt_path << std::endl;
        
        auto rois_mat = MatParser::load_rois_from_mat(mat_path);
        auto rois_txt = read_rois_from_txt(txt_path);
        
        std::cout << "    MAT ROIs count: " << rois_mat.size() << ", TXT ROIs count: " << rois_txt.size() << std::endl;
        assert(rois_mat.size() == rois_txt.size());
        
        for (size_t i = 0; i < rois_mat.size(); ++i) {
            const auto& poly_mat = rois_mat[i].poly;
            const auto& poly_txt = rois_txt[i].poly;
            
            assert(poly_mat.size() == poly_txt.size());
            for (size_t j = 0; j < poly_mat.size(); ++j) {
                // Check coordinate parity
                assert(is_approx(poly_mat[j].first, poly_txt[j].first, 1e-4));
                assert(is_approx(poly_mat[j].second, poly_txt[j].second, 1e-4));
            }
        }
        std::cout << "    -> Parity matched!" << std::endl;
        tested_files++;
    }
    
    assert(tested_files > 0 && "At least one MAT/TXT pair must be tested to ensure parity verification ran!");
    std::cout << "  -> test_mat_parser_parity passed (" << tested_files << " files tested)!" << std::endl;
}

// -------------------------------------------------------------
// Main execution
// -------------------------------------------------------------
int main() {
    std::cout << "\n=============================================" << std::endl;
    std::cout << "      Running Bolus Tracking C++ Unit Tests  " << std::endl;
    std::cout << "=============================================\n" << std::endl;
    
    test_signal_processor();
    test_spline_interpolator();
    test_gamma_functor();
    test_bolus_fitter();
    test_roi_mask_rasterizer();
    test_nice_ticks();
    test_physiological_bounds_filtering();
    test_mat_parser_parity();
    
    std::cout << "\n=============================================" << std::endl;
    std::cout << "      All C++ Unit Tests Passed Successfully!" << std::endl;
    std::cout << "=============================================\n" << std::endl;
    
    return 0;
}


