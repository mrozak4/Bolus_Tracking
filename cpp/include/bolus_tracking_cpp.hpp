#ifndef BOLUS_TRACKING_CPP_HPP
#define BOLUS_TRACKING_CPP_HPP

#include <vector>
#include <string>
#include <utility>
#include <filesystem>
#include <memory>
#include <Eigen/Dense>

// Common Structures
struct ROI {
    int id;
    std::vector<std::pair<double, double>> poly;
};

struct FitRecord {
    int roi_id;
    int subj_num;
    std::string exp;
    double init_amp;
    double init_t2p;
    double init_fwhm;
    double init_m;
    double init_snr;
    double init_cnr;
    double click_start;
    double click_onset;
    double click_peak;
    double click_end;
    double f_amp;
    double f_t2p;
    double f_fwhm;
    double f_m;
    double f_snr;
    double f_cnr;
    double denoise_rms;
    double raw_sd_base;
    
    // Legacy MATLAB fields
    double auc;
    double aucn;
    double ttlb;
    double ttm;
    double tthb;
    double ont;
    double ont_sc;
    int roi_size;
    std::string ves_type;
    std::string qc_flag;
    std::string fit_source;
    int stall_flag;
};

struct QCSettings {
    double cnr_min = 5.0;
    double fwhm_max = 15.0;
    double t2p_max = 10.0;
    double cnr_fail = 3.0;
    double fwhm_fail = 100.0;
    double t2p_fail = 50.0;
    double amp_fail = 1.0;
};

struct StallSettings {
    double ont_offset = 4.0;     // Heuristic A: onset > median + ont_offset
    double ont_mult = 3.0;       // Heuristic A: onset > ont_mult × median
    double t2p_mult = 3.0;       // Heuristic A: t2p > t2p_mult × median
    double t2p_abs = 15.0;       // Heuristic A: t2p > t2p_abs (absolute)
    double sd_base = 20.0;       // Heuristic B: raw_sd_base > sd_base
    double step_t2p = 0.6;       // Heuristic C: t2p < step_t2p
    double step_fwhm = 8.0;      // Heuristic C: fwhm > step_fwhm
};

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

struct NiceTicks {
    double step;
    std::vector<double> ticks;
};

// Classes

/**
 * @brief Class containing signal processing utility methods such as statistics and filtering.
 */
class SignalProcessor {
public:
    static double compute_median(std::vector<double> v);
    static double compute_std(const std::vector<double>& v, double mean);
    static int reflect_index(int idx, int n);
    static std::vector<double> gaussian_filter1d(const std::vector<double>& tr, double sigma);
    static std::vector<double> gradient(const std::vector<double>& tr);
    static std::vector<double> denoise_trace(const std::vector<double>& trace, double denoise_sd = 2.0, int half_win = 5);
};

/**
 * @brief Class representing a 1D cubic spline interpolator.
 */
class SplineInterpolator {
private:
    std::vector<double> x, y;
    std::vector<double> b, c, d;

public:
    void build(const std::vector<double>& px, const std::vector<double>& py);
    double eval(double val) const;
};

/**
 * @brief Evaluates the Gamma function model at a given time point.
 */
static inline double evaluate_gamma_model(double t, double amp, double t2p, double fwhm, double m) {
    if (t <= 0.0) return m;
    if (fwhm <= 1e-9 || t2p <= 1e-9) return m;
    double alpha = ((t2p * t2p) / (fwhm * fwhm)) * 8.0 * std::log(2.0);
    double beta = ((fwhm * fwhm) / t2p) / (8.0 * std::log(2.0));
    double L_val = alpha * std::log(t / t2p) - (t - t2p) / beta;
    if (L_val > -700.0) {
        return m + amp * std::exp(L_val);
    }
    return m;
}

/**
 * @brief Functor representing the non-linear Gamma function model for Eigen's Levenberg-Marquardt solver.
 */
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
    int values() const { return static_cast<int>(t.size()) + 4; }
    
    const std::vector<double>& t;
    const std::vector<double>& y;
    
    double m_init;
    double m_bound;
    
    bool use_cauchy;
    double f_scale;
    
    double min_amp;
    double max_amp;
    double min_t2p;
    double max_t2p;
    double min_fwhm;
    double max_fwhm;
    
    int operator()(const Eigen::VectorXd& x, Eigen::VectorXd& fvec) const;
};

/**
 * @brief Class encapsulating bolus initial parameter estimation and non-linear curve fitting.
 */
class BolusFitter {
public:
    double min_amp;
    double max_amp;
    double min_t2p;
    double max_t2p;
    double min_fwhm;
    double max_fwhm;
    bool verbose;

    BolusFitter(double min_amp = 1e-6, double max_amp = 1023.0,
                double min_t2p = 1e-6, double max_t2p = 1e6,
                double min_fwhm = 0.5, double max_fwhm = 1e6,
                bool verbose = false);

    AutoEstimateResults auto_estimate_params(const std::vector<double>& tr, const std::vector<double>& t_us, double fr, int up_f = 20, bool low_cnr = false) const;
    std::vector<double> run_nonlinear_fit(const std::vector<double>& t, const std::vector<double>& y,
                                         const std::vector<double>& params_init, double sd_base, bool& success, bool& pass2_run, bool debug_print = false) const;
    std::vector<double> run_nonlinear_fit_with_bounds(const std::vector<double>& t, const std::vector<double>& y,
                                                     const std::vector<double>& params_init, double sd_base,
                                                     double b_min_amp, double b_max_amp,
                                                     double b_min_t2p, double b_max_t2p,
                                                     double b_min_fwhm, double b_max_fwhm,
                                                     bool& success, bool debug_print = false) const;
    std::vector<double> get_parameter_se(const std::vector<double>& t, const std::vector<double>& popt, double mse) const;
    
    static bool is_near_bounds(double val, double low, double high);
    static std::string determine_qc_flag(double f_amp, double f_t2p, double f_fwhm, double f_m, double f_cnr,
                                         double min_amp, double max_amp, double min_t2p, double max_t2p,
                                         double min_fwhm, double max_fwhm, bool fit_success, bool pass2_run = false,
                                         double observed_peak_amp = 0.0, double sd_base = 0.0);
    static std::string suggest_vessel_type(double ont, double t2p, double fwhm, double amp, const std::string& qc_flag);
};

/**
 * @brief Class handling scanline rasterization of ROI polygons into pixel masks.
 */
class ROIMaskRasterizer {
public:
    static std::vector<int> get_mask_pixels(const std::vector<std::pair<double, double>>& poly, int width, int height);
    static std::vector<int> get_active_pixels(const std::vector<std::pair<double, double>>& poly, int width, int height);
};

/**
 * @brief Class managing visualization of the raw data, denoised signal, and fitted curve by exporting SVG plots.
 */
class BolusVisualizer {
public:
    static NiceTicks get_nice_ticks(double min_val, double max_val, int max_ticks = 5);
    static std::string format_tick(double val);
    static void save_svg_plot(int roi_id, const std::string& tiff_path,
                             const std::vector<double>& tl_raw, const std::vector<double>& mfi_raw,
                             const std::vector<double>& mfi_denoised,
                             const std::vector<double>& tl_us, const std::vector<double>& y_us,
                             const FitRecord& rec, bool fit_success, double k);
};

/**
 * @brief Class responsible for running the processing pipeline for a single dataset (TIFF file + ROI).
 */
class DatasetProcessor {
private:
    double drift_window;
    bool enable_plots;
    BolusFitter fitter;
    QCSettings qc_settings;
    StallSettings stall_settings;

public:
    DatasetProcessor(double drift_window = 15.0, bool enable_plots = false, const BolusFitter& fitter = BolusFitter(), const QCSettings& qc_settings = QCSettings(), const StallSettings& stall_settings = StallSettings());
    
    int parse_subject_number(const std::string& filepath) const;
    std::string parse_experiment(const std::string& filepath) const;
    
    FitRecord process_single_roi(int roi_id, const std::vector<std::pair<double, double>>& poly,
                                 const std::vector<std::vector<float>>& frames, int width, int height,
                                 double fr, int up_f, const std::string& tiff_path,
                                 double prior_t2p = -1.0, double prior_fwhm = -1.0) const;
                                 
    bool process_dataset_file(const std::string& tiff_path, const std::string& rois_path, double fr, int up_f, const std::string& out_csv) const;
};

/**
 * @brief Class responsible for executing folder-wide batch processing by finding file triplets.
 */
class BatchProcessor {
private:
    std::string folder_path;
    double drift_window;
    bool enable_plots;
    BolusFitter fitter;
    QCSettings qc_settings;
    StallSettings stall_settings;

    struct PathInfo {
        std::filesystem::path path;
        std::string identifier;
        std::string top_dir;
    };

public:
    BatchProcessor(const std::string& folder_path, double drift_window = 15.0, bool enable_plots = false, const BolusFitter& fitter = BolusFitter(), const QCSettings& qc_settings = QCSettings(), const StallSettings& stall_settings = StallSettings());
    
    double parse_frame_rate(const std::string& filepath) const;
    std::string extract_identifier(const std::string& filename) const;
    std::string get_top_relative_dir(const std::filesystem::path& file_path, const std::filesystem::path& base_folder) const;
    bool contains_ignored_pattern(const std::string& path) const;
    
    bool run() const;
    bool run_preflight_scan(bool& has_warnings, bool& has_errors) const;
    bool run_prepare(bool dry_run = true, bool force_overwrite = false) const;
};

/**
 * @brief Writes a vector of ROI polygons to a text file in the pipeline's expected format.
 */
bool write_rois_txt(const std::string& output_path, const std::vector<ROI>& rois);

#endif // BOLUS_TRACKING_CPP_HPP
