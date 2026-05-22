#ifndef BOLUS_GUI_HPP
#define BOLUS_GUI_HPP

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cmath>
#include <limits>
#include <chrono>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "implot.h"
#include "bolus_tracking_cpp.hpp"

// ============================================================================
// Localization & Translation Structures
// ============================================================================

enum Language {
    LANG_EN,
    LANG_FR,
    LANG_DE_CH,
    LANG_JA,
    LANG_ZH_CN,
    LANG_KL,
    LANG_HT,
    LANG_DA,
    LANG_NL
};

struct Translation {
    std::string title_app;
    std::string section_markers;
    std::string section_crop;
    std::string section_params;
    std::string sidebar_title;
    std::string checkbox_flagged;
    std::string btn_save_csv;
    std::string btn_reset_all;
    std::string btn_load_state;
    std::string btn_save_state;
    std::string btn_refit;
    std::string btn_override;
    std::string btn_revert;
    std::string btn_reset_crop;
    std::string btn_crop_bounds;
    std::string label_onset;
    std::string label_peak;
    std::string label_end;
    std::string label_baseline;
    std::string text_slider_desc;
    std::string btn_ok;
    std::string btn_cancel;
    std::string modal_reset_title;
    std::string modal_reset_desc;
    std::string btn_reset_confirm;
    std::string modal_save_success;
    std::string modal_save_state_success;
    std::string modal_load_state_success;
    std::string text_active_roi;
    std::string text_qc_flag;
    std::string text_fit_source;
    std::string text_dataset_loaded;
    std::string text_roi_count;
    std::string text_flagged_count;
    std::string text_manual_count;
    std::string col_variable;
    std::string col_amplitude;
    std::string col_t2p;
    std::string col_fwhm;
    std::string col_baseline;
    std::string col_cnr;
    std::string col_onset;
    std::string plot_title;
    std::string plot_y_axis;
    std::string plot_x_axis;
    std::string plot_raw;
    std::string plot_denoised;
    std::string plot_fit;
    std::string current_folder;
    std::string path_selector;
    std::string btn_select_folder;
    std::string btn_open_file;
    std::string btn_close_dialog;
    std::string dialog_title;
    std::string text_total_rois;
    std::string text_active_queue;
    std::string text_triage_queue;
    std::string btn_next_problem;
    std::string btn_prev_problem;
    std::string text_no_data;
    std::string text_plot_status_header;
    std::string title_manual_override;
    std::string text_manual_override_desc;
    std::string btn_revert_loaded;
    std::string text_load_subject_data;
    std::string text_save_state_msg;
    std::string text_load_state_msg;
    std::string text_save_csv_msg;
    std::string tag_onset;
    std::string tag_peak;
    std::string tag_end;
    std::string tag_base;
    std::string btn_clear_data;
    
    // Denoising translations
    std::string section_denoise;
    std::string label_denoise_strength;
    std::string section_actions;
    
    // Status/Source translations (replaces inline ternaries)
    std::string qc_pass;
    std::string qc_warn;
    std::string qc_fail;
    std::string qc_review;
    std::string source_auto;
    std::string source_manual;
    std::string source_override;
    std::string source_prior;
    std::string label_fitted;
    std::string label_estimated_init;
    std::string label_filter;
    std::string filter_all;
    std::string filter_flagged;
    std::string filter_fail;
    std::string filter_warn;
    std::string filter_pass;
    std::string filter_review;
    std::string label_auto_fit;
    
    // New translation fields
    std::string text_sidebar_counts;
    std::string text_kinetics_title;
    std::string col_onset_scan;
    std::string col_tt_lower;
    std::string col_tt_peak;
    std::string col_tt_upper;
    std::string col_vessel_type;
    std::string text_visual_crop_range;
    std::string btn_prev_short;
    std::string btn_next_short;
};

// ============================================================================
// Data Structures
// ============================================================================

struct CsvRecord {
    int roi_id = 0;
    int subj_num = 0;
    std::string exp = "";
    double init_amp = 0.0;
    double init_t2p = 0.0;
    double init_fwhm = 0.0;
    double init_m = 0.0;
    double init_snr = 0.0;
    double init_cnr = 0.0;
    double click_start = 0.0;
    double click_onset = 0.0;
    double click_peak = 0.0;
    double click_end = 0.0;
    double f_amp = std::numeric_limits<double>::quiet_NaN();
    double f_t2p = std::numeric_limits<double>::quiet_NaN();
    double f_fwhm = std::numeric_limits<double>::quiet_NaN();
    double f_m = std::numeric_limits<double>::quiet_NaN();
    double f_snr = std::numeric_limits<double>::quiet_NaN();
    double f_cnr = std::numeric_limits<double>::quiet_NaN();
    double auc = std::numeric_limits<double>::quiet_NaN();
    double aucn = std::numeric_limits<double>::quiet_NaN();
    double ttlb = std::numeric_limits<double>::quiet_NaN();
    double ttm = std::numeric_limits<double>::quiet_NaN();
    double tthb = std::numeric_limits<double>::quiet_NaN();
    double ont = std::numeric_limits<double>::quiet_NaN();
    double ont_sc = std::numeric_limits<double>::quiet_NaN();
    int roi_size = 0;
    double denoise_rms = 0.0;
    std::string ves_type = "U";
    std::string qc_flag = "FAIL";
    std::string fit_source = "auto";
};

struct RoiState {
    int roi_id = -1;
    double crop_min = 0.0;
    double crop_max = 0.0;
    double onset = 0.0;
    double peak = 0.0;
    double end = 0.0;
    double baseline = 0.0;
    std::string qc_flag = "FAIL";
    std::string fit_source = "auto";
};

struct TiffData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::vector<float>> frames;
};

struct RoiCachedData {
    int roi_id = 0;
    std::vector<double> t_raw;
    std::vector<double> y_raw;
    std::vector<double> y_raw_detrended;
    std::vector<double> y_denoised;
    std::vector<double> t_us;
    std::vector<double> y_us;
    std::vector<double> t_fit_plot;
    std::vector<double> y_fit_plot;
    std::vector<double> t_fit_auto_plot;
    std::vector<double> y_fit_auto_plot;
    double sd_base = 0.05;
    double drift_slope = 0.0;
};

struct DirEntry {
    std::string name;
    bool is_dir;
};

// ============================================================================
// Helper Utilities & CSV Parser Decl
// ============================================================================

std::string get_resource_path(const std::string& rel_path);
bool is_valid_ttf(const std::string& path);
std::string to_klingon_piqad(const std::string& input);
std::string find_cjk_font();
std::vector<CsvRecord> read_results_csv(const std::string& path);
void save_results_csv(const std::string& path, const std::vector<CsvRecord>& records);
std::string find_rois_txt_file(const std::string& tiff_path);
std::string find_meta_txt_file(const std::string& tiff_path);
TiffData load_tiff(const std::string& path);
std::vector<ROI> load_rois_txt(const std::string& path);
double parse_frame_rate(const std::string& filepath);

// ============================================================================
// File Browser Component Decl
// ============================================================================

class FileBrowser {
public:
    std::filesystem::path current_path;
    std::vector<DirEntry> entries;
    std::string selected_file;
    bool open = false;
    Translation* tr = nullptr;

    FileBrowser();
    void refresh();
    void draw(const char* title);
};

// ============================================================================
// Main Application Class Decl
// ============================================================================

class BolusApp {
private:
    GLFWwindow* m_window = nullptr;
    Language m_lang = LANG_EN;
    Translation m_tr;
    FileBrowser m_browser;

    ImFont* m_font_regular = nullptr;
    ImFont* m_font_bold = nullptr;

    std::string m_csv_path;
    std::string m_tiff_path;
    std::string m_rois_path;
    std::string m_meta_path;

    double m_fr = 1.0;
    TiffData m_tiff;
    std::vector<ROI> m_rois;
    std::vector<CsvRecord> m_records;
    std::vector<CsvRecord> m_records_backup;
    std::vector<RoiCachedData> m_cache;

    std::vector<RoiState> m_gui_roi_states;
    std::vector<RoiState> m_gui_roi_states_backup;

    int m_selected_roi_idx = -1;
    int m_qc_filter_type = 1; // 0=All, 1=Flagged, 2=FAIL, 3=WARN, 4=PASS, 5=REVIEW
    std::vector<int> m_triage_queue;
    int m_queue_pos = -1;

    double m_crop_min = 0.0;
    double m_crop_max = 60.0;
    double m_onset_marker = 10.0;
    double m_peak_marker = 15.0;
    double m_end_marker = 25.0;
    double m_baseline_marker = 40.0;

    int m_upsample_factor = 20;
    BolusFitter m_fitter;
    QCSettings m_qc_settings;
    double m_drift_win = 15.0;
    float m_denoise_strength_factor = 1.0f;

    void update_locale();
    void build_triage_queue();
    void select_record(int idx);
    void run_fit_on_current_roi();
    void run_fit_on_record(int idx, bool is_auto);
    void precompute_all_traces();
    void precompute_single_trace(size_t r);
    void precompute_fit_plot(size_t cache_idx);
    void save_active_roi_svg();
    void save_gui_state();
    void load_gui_state();
    void clear_subject_data();

    // UI subsections
    void draw_gui();
    void draw_top_bar();
    void draw_sidebar();
    void draw_main_area();

public:
    BolusApp();
    ~BolusApp();
    bool init();
    void run();
    bool load_dataset(const std::string& csv_path);
};

#endif // BOLUS_GUI_HPP
