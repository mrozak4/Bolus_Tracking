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
    int values() const { return t.size(); }
    
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
private:
    double min_amp;
    double max_amp;
    double min_t2p;
    double max_t2p;
    double min_fwhm;
    double max_fwhm;

public:
    BolusFitter(double min_amp = 1e-6, double max_amp = 1023.0,
                double min_t2p = 1e-6, double max_t2p = 1e6,
                double min_fwhm = 0.5, double max_fwhm = 1e6);

    AutoEstimateResults auto_estimate_params(const std::vector<double>& tr, const std::vector<double>& t_us, double fr, int up_f = 20, bool low_cnr = false) const;
    std::vector<double> run_nonlinear_fit(const std::vector<double>& t, const std::vector<double>& y,
                                         const std::vector<double>& params_init, double sd_base, bool& success) const;
    std::vector<double> get_parameter_se(const std::vector<double>& t, const std::vector<double>& popt, double mse) const;
};

/**
 * @brief Class handling scanline rasterization of ROI polygons into pixel masks.
 */
class ROIMaskRasterizer {
public:
    static std::vector<int> get_mask_pixels(const std::vector<std::pair<double, double>>& poly, int width, int height);
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

public:
    DatasetProcessor(double drift_window = 15.0, bool enable_plots = false, const BolusFitter& fitter = BolusFitter(), const QCSettings& qc_settings = QCSettings());
    
    int parse_subject_number(const std::string& filepath) const;
    std::string parse_experiment(const std::string& filepath) const;
    
    FitRecord process_single_roi(int roi_id, const std::vector<std::pair<double, double>>& poly,
                                 const std::vector<std::vector<float>>& frames, int width, int height,
                                 double fr, int up_f, const std::string& tiff_path) const;
                                 
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

    struct PathInfo {
        std::filesystem::path path;
        std::string identifier;
        std::string top_dir;
    };

public:
    BatchProcessor(const std::string& folder_path, double drift_window = 15.0, bool enable_plots = false, const BolusFitter& fitter = BolusFitter(), const QCSettings& qc_settings = QCSettings());
    
    double parse_frame_rate(const std::string& filepath) const;
    std::string extract_identifier(const std::string& filename) const;
    std::string get_top_relative_dir(const std::filesystem::path& file_path, const std::filesystem::path& base_folder) const;
    bool contains_ignored_pattern(const std::string& path) const;
    
    bool run() const;
};

#endif // BOLUS_TRACKING_CPP_HPP
