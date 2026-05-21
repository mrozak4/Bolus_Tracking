/**
 * @file bolus_gui.cpp
 * @brief Interactive C++ GUI Triage App for Bolus Tracking Pipeline.
 * 
 * Provides a cross-platform user interface using GLFW, Dear ImGui, and ImPlot.
 * Allows users to inspect fits, crop visualization, drag markers to manually fit,
 * and save results.
 */

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <limits>
#include <chrono>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h>
#include <tiffio.h>

#include "bolus_tracking_cpp.hpp"

// ============================================================================
// Data Structures & CSV Parser
// ============================================================================

/**
 * @brief Representation of a CSV record from the results file.
 */
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

/**
 * @brief Representation of an ROI's interactive state to save and restore GUI workflow progress.
 */
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


/**
 * @brief Representation of a TIFF stack metadata and frame buffers.
 */
struct TiffData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::vector<float>> frames;
};

/**
 * @brief Cache for precomputed traces per ROI.
 */
struct RoiCachedData {
    int roi_id = 0;
    std::vector<double> t_raw;
    std::vector<double> y_raw;
    std::vector<double> y_denoised;
    std::vector<double> t_us;
    std::vector<double> y_us;
    
    // Fit curve values for plotting
    std::vector<double> t_fit_plot;
    std::vector<double> y_fit_plot;
    
    double sd_base = 0.05;
    double drift_slope = 0.0;
};

// ============================================================================
// Helper Utilities
// ============================================================================

/**
 * @brief Parse the results CSV file into a vector of records.
 */
std::vector<CsvRecord> read_results_csv(const std::string& path) {
    std::vector<CsvRecord> records;
    std::ifstream file(path);
    if (!file.is_open()) return records;

    std::string header_line;
    if (!std::getline(file, header_line)) return records;

    std::vector<std::string> headers;
    std::stringstream hss(header_line);
    std::string hcell;
    while (std::getline(hss, hcell, ',')) {
        while (!hcell.empty() && (hcell.back() == '\r' || hcell.back() == '\n' || hcell.back() == ' ')) hcell.pop_back();
        while (!hcell.empty() && hcell.front() == ' ') hcell.erase(hcell.begin());
        headers.push_back(hcell);
    }

    auto get_col_idx = [&](const std::string& name) -> int {
        for (size_t i = 0; i < headers.size(); ++i) {
            if (headers[i] == name) return static_cast<int>(i);
        }
        return -1;
    };

    int idx_roi = get_col_idx("ROI");
    int idx_subj = get_col_idx("SubjNum");
    int idx_exp = get_col_idx("Exp");
    int idx_init_amp = get_col_idx("InitAmp");
    int idx_init_t2p = get_col_idx("InitT2p");
    int idx_init_fwhm = get_col_idx("InitFWHM");
    int idx_init_m = get_col_idx("InitM");
    int idx_init_snr = get_col_idx("InitSNR");
    int idx_init_cnr = get_col_idx("InitCNR");
    int idx_start = get_col_idx("Click1_Start_T");
    int idx_onset = get_col_idx("Click2_Onset_T");
    int idx_peak = get_col_idx("Click3_Peak_T");
    int idx_end = get_col_idx("Click4_End_T");
    int idx_f_amp = get_col_idx("F_Amp");
    int idx_f_t2p = get_col_idx("F_T2p");
    int idx_f_fwhm = get_col_idx("F_FWHM");
    int idx_f_m = get_col_idx("F_M");
    int idx_f_snr = get_col_idx("F_SNR");
    int idx_f_cnr = get_col_idx("F_CNR");
    int idx_auc = get_col_idx("AUC");
    int idx_aucn = get_col_idx("AUCn");
    int idx_ttlb = get_col_idx("TTlb");
    int idx_ttm = get_col_idx("TTm");
    int idx_tthb = get_col_idx("TThb");
    int idx_ont = get_col_idx("OnT");
    int idx_ont_sc = get_col_idx("OnTSc");
    int idx_roi_size = get_col_idx("ROISize");
    int idx_denoise = get_col_idx("Denoise_RMS");
    int idx_ves = get_col_idx("VesType");
    int idx_qc = get_col_idx("QC_Flag");
    int idx_source = get_col_idx("Fit_Source");

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::vector<std::string> cells;
        std::string cell;
        while (std::getline(ss, cell, ',')) {
            while (!cell.empty() && (cell.back() == '\r' || cell.back() == '\n')) cell.pop_back();
            cells.push_back(cell);
        }
        while (cells.size() < headers.size()) {
            cells.push_back("");
        }

        CsvRecord rec;
        auto parse_double = [&](int idx) -> double {
            if (idx >= 0 && idx < static_cast<int>(cells.size()) && !cells[idx].empty()) {
                try { return std::stod(cells[idx]); } catch (...) {}
            }
            return std::numeric_limits<double>::quiet_NaN();
        };
        auto parse_int = [&](int idx) -> int {
            if (idx >= 0 && idx < static_cast<int>(cells.size()) && !cells[idx].empty()) {
                try { return std::stoi(cells[idx]); } catch (...) {}
            }
            return 0;
        };
        auto parse_str = [&](int idx) -> std::string {
            if (idx >= 0 && idx < static_cast<int>(cells.size())) {
                return cells[idx];
            }
            return "";
        };

        if (idx_roi >= 0) rec.roi_id = parse_int(idx_roi);
        if (idx_subj >= 0) rec.subj_num = parse_int(idx_subj);
        if (idx_exp >= 0) rec.exp = parse_str(idx_exp);
        rec.init_amp = parse_double(idx_init_amp);
        rec.init_t2p = parse_double(idx_init_t2p);
        rec.init_fwhm = parse_double(idx_init_fwhm);
        rec.init_m = parse_double(idx_init_m);
        rec.init_snr = parse_double(idx_init_snr);
        rec.init_cnr = parse_double(idx_init_cnr);
        rec.click_start = parse_double(idx_start);
        rec.click_onset = parse_double(idx_onset);
        rec.click_peak = parse_double(idx_peak);
        rec.click_end = parse_double(idx_end);

        rec.f_amp = parse_double(idx_f_amp);
        rec.f_t2p = parse_double(idx_f_t2p);
        rec.f_fwhm = parse_double(idx_f_fwhm);
        rec.f_m = parse_double(idx_f_m);
        rec.f_snr = parse_double(idx_f_snr);
        rec.f_cnr = parse_double(idx_f_cnr);

        rec.auc = parse_double(idx_auc);
        rec.aucn = parse_double(idx_aucn);
        rec.ttlb = parse_double(idx_ttlb);
        rec.ttm = parse_double(idx_ttm);
        rec.tthb = parse_double(idx_tthb);
        rec.ont = parse_double(idx_ont);
        rec.ont_sc = parse_double(idx_ont_sc);

        if (idx_roi_size >= 0) rec.roi_size = parse_int(idx_roi_size);
        rec.denoise_rms = parse_double(idx_denoise);
        if (idx_ves >= 0) rec.ves_type = parse_str(idx_ves);
        if (idx_qc >= 0) rec.qc_flag = parse_str(idx_qc);
        if (idx_source >= 0) rec.fit_source = parse_str(idx_source);

        records.push_back(rec);
    }
    return records;
}

/**
 * @brief Write the updated records vector back to the results CSV.
 */
void save_results_csv(const std::string& path, const std::vector<CsvRecord>& records) {
    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "ROI,SubjNum,Exp,InitAmp,InitT2p,InitFWHM,InitM,InitSNR,InitCNR,"
           "Click1_Start_T,Click2_Onset_T,Click3_Peak_T,Click4_End_T,"
           "F_Amp,F_T2p,F_FWHM,F_M,F_SNR,F_CNR,AUC,AUCn,TTlb,TTm,TThb,OnT,OnTSc,ROISize,Denoise_RMS,VesType,QC_Flag,Fit_Source\n";

    for (const auto& rec : records) {
        auto format_double = [](double v) -> std::string {
            if (std::isnan(v)) return "";
            std::stringstream ss;
            ss << v;
            return ss.str();
        };

        out << rec.roi_id << ","
            << rec.subj_num << ","
            << rec.exp << ","
            << format_double(rec.init_amp) << ","
            << format_double(rec.init_t2p) << ","
            << format_double(rec.init_fwhm) << ","
            << format_double(rec.init_m) << ","
            << format_double(rec.init_snr) << ","
            << format_double(rec.init_cnr) << ","
            << format_double(rec.click_start) << ","
            << format_double(rec.click_onset) << ","
            << format_double(rec.click_peak) << ","
            << format_double(rec.click_end) << ","
            << format_double(rec.f_amp) << ","
            << format_double(rec.f_t2p) << ","
            << format_double(rec.f_fwhm) << ","
            << format_double(rec.f_m) << ","
            << format_double(rec.f_snr) << ","
            << format_double(rec.f_cnr) << ","
            << format_double(rec.auc) << ","
            << format_double(rec.aucn) << ","
            << format_double(rec.ttlb) << ","
            << format_double(rec.ttm) << ","
            << format_double(rec.tthb) << ","
            << format_double(rec.ont) << ","
            << format_double(rec.ont_sc) << ","
            << rec.roi_size << ","
            << format_double(rec.denoise_rms) << ","
            << rec.ves_type << ","
            << rec.qc_flag << ","
            << rec.fit_source << "\n";
    }
}

/**
 * @brief Search for the ROI text file in the same directory or subdirectories.
 */
std::string find_rois_txt_file(const std::string& tiff_path) {
    std::filesystem::path tp(tiff_path);
    std::filesystem::path parent = tp.parent_path();
    std::string stem = tp.stem().string();
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(parent)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            std::string l_stem = stem;
            std::transform(l_stem.begin(), l_stem.end(), l_stem.begin(), ::tolower);
            if (name.find(l_stem) != std::string::npos && name.find("rois") != std::string::npos && name.find(".txt") != std::string::npos) {
                return entry.path().string();
            }
        }
    }
    return "";
}

/**
 * @brief Search for the metadata text file in the same directory or subdirectories.
 */
std::string find_meta_txt_file(const std::string& tiff_path) {
    std::filesystem::path tp(tiff_path);
    std::filesystem::path parent = tp.parent_path();
    std::string stem = tp.stem().string();
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(parent)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            std::string l_stem = stem;
            std::transform(l_stem.begin(), l_stem.end(), l_stem.begin(), ::tolower);
            if (name.find(l_stem) != std::string::npos && name.find(".txt") != std::string::npos && name.find("rois") == std::string::npos) {
                return entry.path().string();
            }
        }
    }
    return "";
}

/**
 * @brief Load all frames of a multi-page TIFF stack.
 */
TiffData load_tiff(const std::string& path) {
    TiffData data;
    TIFF* tif = TIFFOpen(path.c_str(), "r");
    if (!tif) return data;
    
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &data.width);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &data.height);
    
    do {
        std::vector<float> frame(data.width * data.height);
        uint16_t bitspersample = 8;
        uint16_t sampleformat = 1;
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitspersample);
        TIFFGetField(tif, TIFFTAG_SAMPLEFORMAT, &sampleformat);
        
        tdata_t buf = _TIFFmalloc(TIFFScanlineSize(tif));
        for (uint32_t row = 0; row < data.height; row++) {
            TIFFReadScanline(tif, buf, row);
            for (uint32_t col = 0; col < data.width; col++) {
                float val = 0.0f;
                if (bitspersample == 16) {
                    val = static_cast<float>(((uint16_t*)buf)[col]);
                } else if (bitspersample == 8) {
                    val = static_cast<float>(((uint8_t*)buf)[col]);
                } else if (bitspersample == 32 && sampleformat == 3) {
                    val = ((float*)buf)[col];
                }
                frame[row * data.width + col] = val;
            }
        }
        _TIFFfree(buf);
        data.frames.push_back(frame);
    } while (TIFFReadDirectory(tif));
    
    TIFFClose(tif);
    return data;
}

/**
 * @brief Load all polygon ROIs from a text file.
 */
std::vector<ROI> load_rois_txt(const std::string& path) {
    std::vector<ROI> rois;
    std::ifstream rois_file(path);
    if (!rois_file.is_open()) return rois;
    
    int n_rois = 0;
    rois_file >> n_rois;
    rois.resize(n_rois);
    for (int i = 0; i < n_rois; ++i) {
        int roi_id, n_pts;
        rois_file >> roi_id >> n_pts;
        rois[i].id = roi_id;
        rois[i].poly.resize(n_pts);
        for (int j = 0; j < n_pts; ++j) {
            rois_file >> rois[i].poly[j].first >> rois[i].poly[j].second;
        }
    }
    return rois;
}

/**
 * @brief Parse camera frame rate from the Fluoview metadata file.
 */
double parse_frame_rate(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) return 1.0;
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
    return 1.0;
}

// ============================================================================
// File Browser Component
// ============================================================================

struct DirEntry {
    std::string name;
    bool is_dir;
};

class FileBrowser {
public:
    std::filesystem::path current_path;
    std::vector<DirEntry> entries;
    std::string selected_file;
    bool open = false;

    FileBrowser() {
        current_path = std::filesystem::current_path();
        refresh();
    }

    void refresh() {
        entries.clear();
        try {
            if (current_path.has_parent_path() && current_path != current_path.root_path()) {
                entries.push_back({"..", true});
            }
            
            for (const auto& entry : std::filesystem::directory_iterator(current_path)) {
                std::string name = entry.path().filename().string();
                if (name.empty() || name.front() == '.') continue;
                entries.push_back({name, entry.is_directory()});
            }
            
            std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
                if (a.is_dir != b.is_dir) return a.is_dir;
                return a.name < b.name;
            });
        } catch (...) {}
    }

    void draw(const char* title) {
        if (!open) return;
        ImGui::OpenPopup(title);
        ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(title, &open, 0)) {
            ImGui::Text("Current Folder: %s", current_path.string().c_str());
            
            char path_buf[1024];
            strncpy(path_buf, current_path.string().c_str(), sizeof(path_buf));
            if (ImGui::InputText("Path Selector", path_buf, sizeof(path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                std::filesystem::path p(path_buf);
                if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
                    current_path = p;
                    refresh();
                }
            }
            
            ImGui::BeginChild("FileListPane", ImVec2(0, 300), true);
            for (const auto& entry : entries) {
                if (entry.is_dir) {
                    if (ImGui::Selectable((entry.name + "/").c_str(), false)) {
                        if (entry.name == "..") {
                            current_path = current_path.parent_path();
                        } else {
                            current_path /= entry.name;
                        }
                        refresh();
                        break;
                    }
                } else {
                    std::string ext = std::filesystem::path(entry.name).extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".csv" || ext == ".tif" || ext == ".tiff") {
                        if (ImGui::Selectable(entry.name.c_str(), selected_file == entry.name)) {
                            selected_file = entry.name;
                        }
                    }
                }
            }
            ImGui::EndChild();
            
            if (ImGui::Button("Select Current Folder", ImVec2(180, 0))) {
                selected_file = "";
                open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (!selected_file.empty()) {
                if (ImGui::Button("Open Selected File", ImVec2(180, 0))) {
                    open = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close Dialog", ImVec2(120, 0))) {
                open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
};

// ============================================================================
// Main Application Class
// ============================================================================

class BolusApp {
private:
    GLFWwindow* m_window = nullptr;
    FileBrowser m_browser;
    
    // Dataset Paths
    std::string m_csv_path = "";
    std::string m_tiff_path = "";
    std::string m_rois_path = "";
    std::string m_meta_path = "";
    
    // Loaded data
    std::vector<CsvRecord> m_records;
    std::vector<RoiState> m_gui_roi_states;
    TiffData m_tiff;
    std::vector<ROI> m_rois;
    double m_fr = 1.0;
    int m_selected_roi_idx = -1;
    
    // Cache
    std::vector<RoiCachedData> m_cache;
    
    // UI Filter and Triage Queue
    bool m_filter_flagged_only = true;
    std::vector<int> m_triage_queue; // Indirection list into m_records
    int m_queue_pos = -1;
    
    // Active markers (time relative to full uncropped timescale)
    double m_onset_marker = 0.0;
    double m_peak_marker = 0.0;
    double m_end_marker = 0.0;
    double m_baseline_marker = 40.0;
    
    // Cropping ranges
    double m_crop_min = 0.0;
    double m_crop_max = 0.0;
    
    // Fitting instance and parameters
    BolusFitter m_fitter;
    QCSettings m_qc_settings;
    double m_drift_win = 15.0;
    int m_upsample_factor = 20;

public:
    BolusApp() : m_fitter(1e-6, 1023.0, 1e-6, 1e6, 0.5, 1e6) {}
    
    ~BolusApp() {
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        if (m_window) {
            glfwDestroyWindow(m_window);
            glfwTerminate();
        }
    }

    bool init() {
        if (!glfwInit()) return false;
        
#if defined(__APPLE__)
        const char* glsl_version = "#version 150";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
        const char* glsl_version = "#version 130";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
        
        m_window = glfwCreateWindow(1360, 780, "Bolus Tracking GUI - Triage App", NULL, NULL);
        if (!m_window) return false;
        
        glfwMakeContextCurrent(m_window);
        glfwSwapInterval(1);
        
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();
        
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        
        // Sleek Premium Theme
        ImGui::StyleColorsDark();
        auto& style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.WindowBorderSize = 0.0f;
        style.FrameBorderSize = 0.0f;
        style.ItemSpacing = ImVec2(10.0f, 8.0f);
        
        ImGui_ImplGlfw_InitForOpenGL(m_window, true);
        ImGui_ImplOpenGL3_Init(glsl_version);
        
        return true;
    }

    /**
     * @brief Run the interactive application event loop.
     */
    void run() {
        while (!glfwWindowShouldClose(m_window)) {
            glfwPollEvents();
            
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            
            draw_gui();
            
            ImGui::Render();
            int display_w, display_h;
            glfwGetFramebufferSize(m_window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(0.12f, 0.12f, 0.13f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            
            glfwSwapBuffers(m_window);
        }
        save_gui_state();
    }

public:
    /**
     * @brief Auto-resolves matching paths from results CSV.
     */
    bool load_dataset(const std::string& csv_path) {
        m_csv_path = csv_path;
        m_records = read_results_csv(csv_path);
        if (m_records.empty()) {
            std::cerr << "Error: Loaded empty CSV or failed to open: " << csv_path << std::endl;
            return false;
        }

        std::filesystem::path cp(csv_path);
        std::filesystem::path parent = cp.parent_path();
        std::string stem = cp.stem().string();
        
        // Strip _results or _results_cpp from stem to find original name
        size_t res_pos = stem.find("_results");
        if (res_pos != std::string::npos) {
            stem = stem.substr(0, res_pos);
        }
        
        m_tiff_path = (parent / (stem + ".tif")).string();
        if (!std::filesystem::exists(m_tiff_path)) {
            m_tiff_path = (parent / (stem + ".tiff")).string();
        }
        
        m_rois_path = find_rois_txt_file(m_tiff_path);
        m_meta_path = find_meta_txt_file(m_tiff_path);
        
        if (!std::filesystem::exists(m_tiff_path)) {
            std::cerr << "TIFF stack not found: " << m_tiff_path << std::endl;
            return false;
        }
        if (!std::filesystem::exists(m_rois_path)) {
            std::cerr << "ROI points txt file not found under: " << parent << std::endl;
            return false;
        }
        
        m_fr = parse_frame_rate(m_meta_path);
        m_tiff = load_tiff(m_tiff_path);
        m_rois = load_rois_txt(m_rois_path);
        
        if (m_tiff.frames.empty() || m_rois.empty()) {
            std::cerr << "Error: TIFF frame list or ROI list is empty." << std::endl;
            return false;
        }
        
        precompute_all_traces();
        
        // Initialize default GUI ROI states
        m_gui_roi_states.resize(m_records.size());
        for (size_t i = 0; i < m_records.size(); ++i) {
            const auto& rec = m_records[i];
            const auto& c = m_cache[i];
            auto& s = m_gui_roi_states[i];
            s.roi_id = rec.roi_id;
            s.crop_min = (!std::isnan(rec.click_start) && rec.click_start >= 0.0) ? rec.click_start : 0.0;
            s.crop_max = c.t_raw.empty() ? 120.0 : c.t_raw.back();
            s.onset = !std::isnan(rec.click_onset) ? rec.click_onset : (!std::isnan(rec.ont) ? rec.ont : s.crop_max * 0.35);
            s.peak = !std::isnan(rec.click_peak) ? rec.click_peak : (!std::isnan(rec.f_t2p) && !std::isnan(rec.ont) ? rec.ont + rec.f_t2p : s.onset + 4.0);
            s.end = !std::isnan(rec.click_end) ? rec.click_end : s.peak + 6.0;
            s.baseline = !std::isnan(rec.f_m) ? rec.f_m : (!std::isnan(rec.init_m) ? rec.init_m : c.y_denoised.front());
            s.qc_flag = rec.qc_flag;
            s.fit_source = rec.fit_source;
        }

        // Try loading gui state
        load_gui_state();
        
        build_triage_queue();
        
        // Select either the loaded last active index, or default to triage queue start
        if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records.size())) {
            select_record(m_selected_roi_idx);
        } else if (!m_triage_queue.empty()) {
            select_record(m_triage_queue[0]);
        } else {
            select_record(0);
        }
        
        return true;
    }

private:
    void save_gui_state() {
        if (m_csv_path.empty()) return;
        
        // Save current active ROI state before writing
        if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records.size())) {
            auto& s = m_gui_roi_states[m_selected_roi_idx];
            s.crop_min = m_crop_min;
            s.crop_max = m_crop_max;
            s.onset = m_onset_marker;
            s.peak = m_peak_marker;
            s.end = m_end_marker;
            s.baseline = m_baseline_marker;
            s.qc_flag = m_records[m_selected_roi_idx].qc_flag;
            s.fit_source = m_records[m_selected_roi_idx].fit_source;
        }
        
        std::string state_path = m_csv_path + ".gui_state";
        std::ofstream out(state_path);
        if (!out.is_open()) return;
        
        out << "# Bolus Tracking Studio GUI State File\n";
        out << "LastSelectedRoiIndex=" << m_selected_roi_idx << "\n";
        out << "FilterFlaggedOnly=" << (m_filter_flagged_only ? 1 : 0) << "\n";
        out << "# ROI,crop_min,crop_max,onset,peak,end,baseline,qc_flag,fit_source\n";
        for (const auto& s : m_gui_roi_states) {
            out << s.roi_id << ","
                << s.crop_min << ","
                << s.crop_max << ","
                << s.onset << ","
                << s.peak << ","
                << s.end << ","
                << s.baseline << ","
                << s.qc_flag << ","
                << s.fit_source << "\n";
        }
        std::cout << "Saved GUI workflow progress to: " << state_path << std::endl;
    }

    void load_gui_state() {
        if (m_csv_path.empty()) return;
        std::string state_path = m_csv_path + ".gui_state";
        std::ifstream in(state_path);
        if (!in.is_open()) return;
        
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            size_t eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = line.substr(0, eq_pos);
                std::string val = line.substr(eq_pos + 1);
                if (key == "LastSelectedRoiIndex") {
                    try { m_selected_roi_idx = std::stoi(val); } catch (...) {}
                } else if (key == "FilterFlaggedOnly") {
                    try { m_filter_flagged_only = (std::stoi(val) != 0); } catch (...) {}
                }
                continue;
            }
            
            // Parse CSV-style ROI state line
            std::stringstream ss(line);
            std::vector<std::string> cells;
            std::string cell;
            while (std::getline(ss, cell, ',')) {
                cells.push_back(cell);
            }
            if (cells.size() < 9) continue;
            
            try {
                int roi_id = std::stoi(cells[0]);
                for (size_t i = 0; i < m_gui_roi_states.size(); ++i) {
                    if (m_gui_roi_states[i].roi_id == roi_id) {
                        auto& s = m_gui_roi_states[i];
                        s.crop_min = std::stod(cells[1]);
                        s.crop_max = std::stod(cells[2]);
                        s.onset = std::stod(cells[3]);
                        s.peak = std::stod(cells[4]);
                        s.end = std::stod(cells[5]);
                        s.baseline = std::stod(cells[6]);
                        s.qc_flag = cells[7];
                        s.fit_source = cells[8];
                        
                        // Sync back to CsvRecord
                        auto& rec = m_records[i];
                        rec.click_start = s.crop_min;
                        rec.click_onset = s.onset;
                        rec.click_peak = s.peak;
                        rec.click_end = s.end;
                        rec.qc_flag = s.qc_flag;
                        rec.fit_source = s.fit_source;
                        break;
                    }
                }
            } catch (...) {}
        }
        std::cout << "Loaded GUI workflow progress from: " << state_path << std::endl;
    }

    /**
     * @brief Precompute raw signals, drift, denoised, and upsampled spline arrays for all ROIs.
     */
    void precompute_all_traces() {
        m_cache.resize(m_rois.size());
        
        for (size_t r = 0; r < m_rois.size(); ++r) {
            const auto& roi = m_rois[r];
            auto& c = m_cache[r];
            c.roi_id = roi.id;
            
            // 1. Rasterize
            std::vector<int> mask = ROIMaskRasterizer::get_mask_pixels(roi.poly, m_tiff.width, m_tiff.height);
            int mask_size = 0;
            for (int v : mask) mask_size += v;
            
            // 2. Average raw MFI
            c.y_raw.resize(m_tiff.frames.size(), 0.0);
            c.t_raw.resize(m_tiff.frames.size(), 0.0);
            for (size_t f = 0; f < m_tiff.frames.size(); ++f) {
                c.t_raw[f] = f / m_fr;
                if (mask_size > 0) {
                    double sum = 0.0;
                    for (int idx = 0; idx < m_tiff.width * m_tiff.height; ++idx) {
                        if (mask[idx]) sum += m_tiff.frames[f][idx];
                    }
                    c.y_raw[f] = sum / mask_size;
                }
            }
            
            // 3. Drift estimation
            double sum_t = 0.0, sum_y = 0.0, sum_tt = 0.0, sum_ty = 0.0;
            int count = 0;
            for (size_t i = 0; i < c.t_raw.size(); ++i) {
                if (c.t_raw[i] <= m_drift_win) {
                    sum_t += c.t_raw[i];
                    sum_y += c.y_raw[i];
                    sum_tt += c.t_raw[i] * c.t_raw[i];
                    sum_ty += c.t_raw[i] * c.y_raw[i];
                    count++;
                }
            }
            c.drift_slope = 0.0;
            if (count > 1) {
                double mean_t = sum_t / count;
                double mean_y = sum_y / count;
                double num = sum_ty - count * mean_t * mean_y;
                double den = sum_tt - count * mean_t * mean_t;
                if (std::abs(den) > 1e-9) c.drift_slope = num / den;
            }
            
            std::vector<double> detrended = c.y_raw;
            for (size_t i = 0; i < detrended.size(); ++i) {
                detrended[i] -= c.drift_slope * c.t_raw[i];
            }
            
            // 4. Denoise and Spline
            c.y_denoised = SignalProcessor::denoise_trace(detrended);
            c.t_us.resize(c.t_raw.size() * m_upsample_factor);
            for (size_t i = 0; i < c.t_us.size(); ++i) {
                c.t_us[i] = i / (m_fr * m_upsample_factor);
            }
            
            SplineInterpolator spline;
            spline.build(c.t_raw, c.y_denoised);
            c.y_us.resize(c.t_us.size());
            for (size_t i = 0; i < c.t_us.size(); ++i) {
                c.y_us[i] = spline.eval(c.t_us[i]);
            }
            
            // 5. Baseline SD
            int n_base = std::min((int)std::round(2.0 * m_fr * m_upsample_factor), (int)std::round(c.y_us.size() * 0.1));
            n_base = std::max(1, n_base);
            std::vector<double> base_win(c.y_us.begin(), c.y_us.begin() + n_base);
            double mean_base = 0.0;
            for (double x : base_win) mean_base += x;
            mean_base /= base_win.size();
            c.sd_base = SignalProcessor::compute_std(base_win, mean_base);
            if (c.sd_base <= 0.0) c.sd_base = 0.05;
            
            // 6. Precompute Fit Plot Curve (from CSV record parameters)
            precompute_fit_plot(r);
        }
    }

    /**
     * @brief Precompute plot coordinates for the active fit parameters of a specific cache index.
     */
    void precompute_fit_plot(size_t cache_idx) {
        auto& c = m_cache[cache_idx];
        const auto& rec = m_records[cache_idx];
        c.t_fit_plot.clear();
        c.y_fit_plot.clear();
        
        if (std::isnan(rec.f_amp) || std::isnan(rec.f_t2p) || std::isnan(rec.f_fwhm) || std::isnan(rec.f_m) || std::isnan(rec.ont)) {
            return;
        }
        
        double alpha = ((rec.f_t2p * rec.f_t2p) / (rec.f_fwhm * rec.f_fwhm)) * 8.0 * std::log(2.0);
        double beta = ((rec.f_fwhm * rec.f_fwhm) / rec.f_t2p) / (8.0 * std::log(2.0));
        
        c.t_fit_plot = c.t_us;
        c.y_fit_plot.resize(c.t_fit_plot.size());
        
        for (size_t i = 0; i < c.t_fit_plot.size(); ++i) {
            double t = c.t_fit_plot[i];
            double val = rec.f_m;
            if (t >= rec.ont) {
                double dt = t - rec.ont;
                val = rec.f_m + rec.f_amp * std::pow(dt / rec.f_t2p, alpha) * std::exp(-(dt - rec.f_t2p) / beta);
            }
            c.y_fit_plot[i] = val;
        }
    }

    /**
     * @brief Update the list indices that need manual review or have failed.
     */
    void build_triage_queue() {
        m_triage_queue.clear();
        for (size_t i = 0; i < m_records.size(); ++i) {
            if (m_filter_flagged_only) {
                if (m_records[i].qc_flag == "FAIL" || m_records[i].qc_flag == "WARN" || m_records[i].qc_flag == "REVIEW") {
                    m_triage_queue.push_back(i);
                }
            } else {
                m_triage_queue.push_back(i);
            }
        }
        
        // Find current position in queue
        m_queue_pos = -1;
        if (m_selected_roi_idx >= 0) {
            for (size_t q = 0; q < m_triage_queue.size(); ++q) {
                if (m_triage_queue[q] == m_selected_roi_idx) {
                    m_queue_pos = q;
                    break;
                }
            }
        }
    }

    /**
     * @brief Select record and populate interactive draggable markers.
     */
    void select_record(int idx) {
        if (idx < 0 || idx >= static_cast<int>(m_records.size())) return;
        
        // 1. Save currently active state to m_gui_roi_states and CsvRecord click times
        if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records.size())) {
            auto& s = m_gui_roi_states[m_selected_roi_idx];
            s.crop_min = m_crop_min;
            s.crop_max = m_crop_max;
            s.onset = m_onset_marker;
            s.peak = m_peak_marker;
            s.end = m_end_marker;
            s.baseline = m_baseline_marker;
            s.qc_flag = m_records[m_selected_roi_idx].qc_flag;
            s.fit_source = m_records[m_selected_roi_idx].fit_source;
            
            auto& old_rec = m_records[m_selected_roi_idx];
            old_rec.click_start = m_crop_min;
            old_rec.click_onset = m_onset_marker;
            old_rec.click_peak = m_peak_marker;
            old_rec.click_end = m_end_marker;
        }

        m_selected_roi_idx = idx;
        
        const auto& rec = m_records[idx];
        const auto& c = m_cache[idx];
        const auto& s = m_gui_roi_states[idx];
        
        // 2. Load markers from m_gui_roi_states
        m_crop_min = s.crop_min;
        m_crop_max = s.crop_max;
        m_onset_marker = s.onset;
        m_peak_marker = s.peak;
        m_end_marker = s.end;
        m_baseline_marker = s.baseline;
        
        // Limit markers inside trace bounds
        double max_t = c.t_raw.back();
        m_onset_marker = std::clamp(m_onset_marker, 0.0, max_t);
        m_peak_marker = std::clamp(m_peak_marker, m_onset_marker + 0.01, max_t);
        m_end_marker = std::clamp(m_end_marker, m_peak_marker + 0.01, max_t);
        
        // Find queue position
        m_queue_pos = -1;
        for (size_t q = 0; q < m_triage_queue.size(); ++q) {
            if (m_triage_queue[q] == idx) {
                m_queue_pos = q;
                break;
            }
        }
    }

    /**
     * @brief Run constrained non-linear fit using draggable visual marker bounds.
     */
    void run_fit_on_current_roi() {
        if (m_selected_roi_idx < 0) return;
        
        auto& rec = m_records[m_selected_roi_idx];
        auto& c = m_cache[m_selected_roi_idx];
        
        // 1. Map draggable marker times to the upsampled trace vector
        auto find_nearest_idx = [](const std::vector<double>& vec, double val) -> int {
            auto it = std::lower_bound(vec.begin(), vec.end(), val);
            if (it == vec.end()) return vec.size() - 1;
            if (it == vec.begin()) return 0;
            double d1 = *it - val;
            double d2 = val - *(it - 1);
            return (d1 < d2) ? std::distance(vec.begin(), it) : std::distance(vec.begin(), it - 1);
        };
        
        int start_idx = find_nearest_idx(c.t_us, m_onset_marker);
        int end_idx = find_nearest_idx(c.t_us, m_end_marker);
        int peak_idx = find_nearest_idx(c.t_us, m_peak_marker);
        
        if (end_idx <= start_idx + 5) {
            std::cerr << "Fit Window is too short!" << std::endl;
            return;
        }
        
        // 2. Prepare sub-vectors relative to the onset
        std::vector<double> t_fit(end_idx - start_idx);
        std::vector<double> y_fit(end_idx - start_idx);
        for (int i = start_idx; i < end_idx; ++i) {
            t_fit[i - start_idx] = c.t_us[i] - c.t_us[start_idx];
            y_fit[i - start_idx] = c.y_us[i];
        }
        
        // 3. Formulate manual guesses
        double guess_amp = c.y_us[peak_idx] - m_baseline_marker;
        if (guess_amp < 1e-4) guess_amp = 10.0;
        
        double guess_t2p = c.t_us[peak_idx] - c.t_us[start_idx];
        if (guess_t2p < 0.1) guess_t2p = 3.0;
        
        double guess_fwhm = (c.t_us[end_idx] - c.t_us[start_idx]) / 2.0;
        if (guess_fwhm < 0.1) guess_fwhm = 5.0;
        
        std::vector<double> init_params = {guess_amp, guess_t2p, guess_fwhm, m_baseline_marker};
        
        // 4. Run fit solver
        bool fit_success = false;
        std::vector<double> popt = m_fitter.run_nonlinear_fit(t_fit, y_fit, init_params, c.sd_base, fit_success);
        
        // 5. Update CsvRecord
        rec.click_start = m_crop_min;
        rec.click_onset = m_onset_marker;
        rec.click_peak = m_peak_marker;
        rec.click_end = m_end_marker;
        
        rec.init_amp = guess_amp;
        rec.init_t2p = guess_t2p;
        rec.init_fwhm = guess_fwhm;
        rec.init_m = m_baseline_marker;
        rec.init_cnr = guess_amp / c.sd_base;
        rec.init_snr = m_baseline_marker / c.sd_base;
        
        rec.fit_source = "manual";
        
        if (fit_success) {
            // Parity validation check
            if (popt[0] <= 1.0001e-6 || popt[0] >= 1023.0 * 0.9999 ||
                popt[1] <= 1.0001e-6 || popt[2] <= 0.5001) {
                fit_success = false;
                rec.qc_flag = "FAIL";
            } else {
                rec.f_amp = popt[0];
                rec.f_t2p = popt[1];
                rec.f_fwhm = popt[2];
                rec.f_m = popt[3];
                rec.f_cnr = popt[0] / c.sd_base;
                rec.f_snr = popt[3] / c.sd_base;
                
                // Recompute hemodynamic values
                std::vector<double> y_fit_model(t_fit.size());
                double alpha = ((popt[1] * popt[1]) / (popt[2] * popt[2])) * 8.0 * std::log(2.0);
                double beta = ((popt[2] * popt[2]) / popt[1]) / (8.0 * std::log(2.0));
                for (size_t i = 0; i < t_fit.size(); ++i) {
                    double t_val = t_fit[i];
                    double val = popt[3];
                    if (t_val > 0) {
                        val = popt[3] + popt[0] * std::pow(t_val / popt[1], alpha) * std::exp(-(t_val - popt[1]) / beta);
                    }
                    y_fit_model[i] = val;
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
                    if (val_n < 0.1) I.push_back(i);
                }
                int onset_idx = 0;
                if (!I.empty()) {
                    int last_idx = -1;
                    for (size_t k = 0; k + 1 < I.size(); ++k) {
                        if (I[k+1] - I[k] == 1) last_idx = k;
                    }
                    if (last_idx != -1) onset_idx = I[last_idx] + 1;
                    else onset_idx = I[0];
                }
                rec.ont = (double)onset_idx / (m_fr * m_upsample_factor);
                rec.ttm = std::abs(popt[1] - rec.ont);
                
                double sum_sq_resid = 0.0;
                for (size_t i = 0; i < y_fit.size(); ++i) {
                    double diff = y_fit[i] - y_fit_model[i];
                    sum_sq_resid += diff * diff;
                }
                double mse = (y_fit.size() > 4) ? (sum_sq_resid / (y_fit.size() - 4)) : 0.0;
                std::vector<double> se = m_fitter.get_parameter_se(t_fit, popt, mse);
                rec.ttlb = std::abs((popt[1] - 1.96 * se[1]) - rec.ont);
                rec.tthb = std::abs((popt[1] + 1.96 * se[1]) - rec.ont);
                
                // Determine QC Flag
                if (rec.f_cnr < m_qc_settings.cnr_fail || popt[2] > m_qc_settings.fwhm_fail || popt[1] > m_qc_settings.t2p_fail || popt[0] < m_qc_settings.amp_fail) {
                    rec.qc_flag = "FAIL";
                } else if (rec.f_cnr < m_qc_settings.cnr_min || popt[2] > m_qc_settings.fwhm_max || popt[1] > m_qc_settings.t2p_max) {
                    rec.qc_flag = "WARN";
                } else {
                    rec.qc_flag = "PASS";
                }
            }
        }
        
        if (!fit_success) {
            rec.f_amp = std::numeric_limits<double>::quiet_NaN();
            rec.f_t2p = std::numeric_limits<double>::quiet_NaN();
            rec.f_fwhm = std::numeric_limits<double>::quiet_NaN();
            rec.f_m = std::numeric_limits<double>::quiet_NaN();
            rec.f_cnr = std::numeric_limits<double>::quiet_NaN();
            rec.f_snr = std::numeric_limits<double>::quiet_NaN();
            rec.auc = std::numeric_limits<double>::quiet_NaN();
            rec.aucn = std::numeric_limits<double>::quiet_NaN();
            rec.ttlb = std::numeric_limits<double>::quiet_NaN();
            rec.ttm = std::numeric_limits<double>::quiet_NaN();
            rec.tthb = std::numeric_limits<double>::quiet_NaN();
            rec.ont = std::numeric_limits<double>::quiet_NaN();
            rec.qc_flag = "FAIL";
        }
        
        // Sync to m_gui_roi_states
        if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_gui_roi_states.size())) {
            auto& s = m_gui_roi_states[m_selected_roi_idx];
            s.crop_min = m_crop_min;
            s.crop_max = m_crop_max;
            s.onset = m_onset_marker;
            s.peak = m_peak_marker;
            s.end = m_end_marker;
            s.baseline = m_baseline_marker;
            s.qc_flag = rec.qc_flag;
            s.fit_source = rec.fit_source;
        }

        // Update curves and triage state
        precompute_fit_plot(m_selected_roi_idx);
        build_triage_queue();
    }

    /**
     * @brief Render the graphical panels.
     */
    void draw_gui() {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("MainPanel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        
        // Top Toolbar
        draw_top_bar();
        
        // Main Panels (Left: Sidebar, Right: Plot & Parameter Details)
        ImGui::Separator();
        
        float sidebar_w = 320.0f;
        ImGui::BeginChild("SidebarPane", ImVec2(sidebar_w, 0), true);
        draw_sidebar();
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        ImGui::BeginChild("PlotAndControlsPane", ImVec2(0, 0), false);
        draw_main_area();
        ImGui::EndChild();
        
        ImGui::End();
        
        // Draw file browser modal
        m_browser.draw("Open Folder or File");
    }

    void draw_top_bar() {
        ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "BOLUS TRACKING MANUAL TRIAGE APP");
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 360.0f);
        if (ImGui::Button("Load Subject Data", ImVec2(160, 24))) {
            m_browser.open = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Final CSV", ImVec2(160, 24))) {
            if (!m_csv_path.empty()) {
                save_results_csv(m_csv_path, m_records);
                save_gui_state();
                ImGui::OpenPopup("Save Success");
            }
        }
        
        if (ImGui::BeginPopupModal("Save Success", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Results written successfully to:\n%s", m_csv_path.c_str());
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void draw_sidebar() {
        ImGui::Text("Triage Sidebar");
        ImGui::Separator();
        
        if (ImGui::Checkbox("Show only problem cases (FAIL/WARN)", &m_filter_flagged_only)) {
            build_triage_queue();
            if (!m_triage_queue.empty()) {
                select_record(m_triage_queue[0]);
            }
        }
        
        ImGui::Text("Total Dataset ROIs: %d", (int)m_records.size());
        ImGui::Text("Active Filter Queue: %d", (int)m_triage_queue.size());
        ImGui::Separator();
        
        ImGui::BeginChild("ListScrollPane", ImVec2(0, 0), false);
        for (int q = 0; q < static_cast<int>(m_triage_queue.size()); ++q) {
            int idx = m_triage_queue[q];
            const auto& rec = m_records[idx];
            
            char label[64];
            snprintf(label, sizeof(label), "ROI %d", rec.roi_id);
            
            bool is_selected = (m_selected_roi_idx == idx);
            
            // Format state color tag
            ImVec4 status_color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            if (rec.qc_flag == "PASS") status_color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
            else if (rec.qc_flag == "WARN") status_color = ImVec4(1.0f, 0.7f, 0.0f, 1.0f);
            else if (rec.qc_flag == "FAIL") status_color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
            else if (rec.qc_flag == "REVIEW") status_color = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);
            
            ImGui::PushStyleColor(ImGuiCol_Text, status_color);
            if (ImGui::Selectable(label, is_selected)) {
                select_record(idx);
            }
            ImGui::PopStyleColor();
            
            ImGui::SameLine(180);
            ImGui::TextColored(status_color, "[%s]", rec.qc_flag.c_str());
            ImGui::SameLine(260);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", rec.fit_source.c_str());
        }
        ImGui::EndChild();
    }

    void draw_main_area() {
        if (m_selected_roi_idx < 0) {
            ImGui::Text("No subject folder or CSV file loaded yet. Use the top button to open a file.");
            
            // Check if file browser selected a file
            if (!m_browser.selected_file.empty()) {
                std::filesystem::path full_p = m_browser.current_path / m_browser.selected_file;
                if (std::filesystem::exists(full_p)) {
                    load_dataset(full_p.string());
                    m_browser.selected_file = "";
                }
            }
            return;
        }
        
        const auto& rec = m_records[m_selected_roi_idx];
        const auto& c = m_cache[m_selected_roi_idx];
        
        // Render plot
        ImGui::Text("MFI Time Series - ROI #%d (Size: %d px) | Status: %s (Source: %s)", rec.roi_id, rec.roi_size, rec.qc_flag.c_str(), rec.fit_source.c_str());
        
        // Draggable Baseline visual crop limits setup
        ImPlot::SetNextAxesLimits(m_crop_min, m_crop_max, 30.0, 300.0, ImGuiCond_Once);
        
        if (ImPlot::BeginPlot("Trace Fitting Plot", ImVec2(-1, 380))) {
            ImPlot::SetupAxes("Time (s)", "Signal (MFI)");
            
            // Limit the current axis limits to show cropped visual window on the fly
            ImPlot::SetupAxisLimits(ImAxis_X1, m_crop_min, m_crop_max, ImGuiCond_Always);
            
            // Draw visual curves
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
            ImPlot::PlotLine("Raw (Detrended)", c.t_raw.data(), c.y_raw.data(), c.t_raw.size());
            ImPlot::PlotLine("Denoised", c.t_raw.data(), c.y_denoised.data(), c.t_raw.size());
            
            if (!c.y_fit_plot.empty()) {
                ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.5f);
                ImPlot::PlotLine("Gamma Fit", c.t_fit_plot.data(), c.y_fit_plot.data(), c.t_fit_plot.size());
                ImPlot::PopStyleVar();
                ImPlot::PopStyleColor();
            }
            ImPlot::PopStyleVar();
            
            // Draggable Lines
            ImPlot::DragLineX(101, &m_onset_marker, ImVec4(0.2f, 0.8f, 0.2f, 1.0f), 2.0f); // Green Onset
            ImPlot::DragLineX(102, &m_peak_marker, ImVec4(1.0f, 0.7f, 0.0f, 1.0f), 2.0f);  // Yellow Peak
            ImPlot::DragLineX(103, &m_end_marker, ImVec4(1.0f, 0.2f, 0.2f, 1.0f), 2.0f);   // Red End
            
            ImPlot::DragLineY(104, &m_baseline_marker, ImVec4(0.8f, 0.4f, 0.8f, 1.0f), 2.0f); // Purple Baseline
            
            // Annotate Draggable Lines
            ImPlot::TagX(m_onset_marker, ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Onset");
            ImPlot::TagX(m_peak_marker, ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "Peak");
            ImPlot::TagX(m_end_marker, ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "End");
            ImPlot::TagY(m_baseline_marker, ImVec4(0.8f, 0.4f, 0.8f, 1.0f), "Base");
            
            ImPlot::EndPlot();
        }
        
        // Parameters & Controls Panel
        ImGui::BeginChild("ParamsPane", ImVec2(0, 0), true);
        
        // Navigation Buttons
        if (ImGui::Button("<< Previous Problem", ImVec2(180, 0))) {
            if (m_queue_pos > 0) {
                select_record(m_triage_queue[m_queue_pos - 1]);
            }
        }
        ImGui::SameLine();
        ImGui::Text("Triage Queue: %d / %d", m_queue_pos + 1, (int)m_triage_queue.size());
        ImGui::SameLine();
        if (ImGui::Button("Next Problem >>", ImVec2(180, 0))) {
            if (m_queue_pos >= 0 && m_queue_pos + 1 < static_cast<int>(m_triage_queue.size())) {
                select_record(m_triage_queue[m_queue_pos + 1]);
            }
        }
        
        ImGui::Separator();
        
        // Manual fitting and cropping
        ImGui::Columns(2, "ControlsGrid", false);
        ImGui::SetColumnWidth(0, 480.0f);
        
        ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "FITTING WINDOW & INTERACTIVE MARKERS");
        ImGui::SliderFloat("Onset Marker (s)", (float*)&m_onset_marker, 0.0f, (float)c.t_raw.back());
        ImGui::SliderFloat("Peak Marker (s)", (float*)&m_peak_marker, (float)m_onset_marker, (float)c.t_raw.back());
        ImGui::SliderFloat("End Marker (s)", (float*)&m_end_marker, (float)m_peak_marker, (float)c.t_raw.back());
        ImGui::SliderFloat("Baseline Value", (float*)&m_baseline_marker, 0.0f, 1000.0f);
        
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        if (ImGui::Button("Re-Fit Manual Window (LM)", ImVec2(240, 36))) {
            run_fit_on_current_roi();
        }
        ImGui::SameLine();
        if (ImGui::Button("Override PASS", ImVec2(140, 36))) {
            m_records[m_selected_roi_idx].qc_flag = "PASS";
            m_records[m_selected_roi_idx].fit_source = "override";
            if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_gui_roi_states.size())) {
                m_gui_roi_states[m_selected_roi_idx].qc_flag = "PASS";
                m_gui_roi_states[m_selected_roi_idx].fit_source = "override";
            }
            build_triage_queue();
        }
        
        ImGui::NextColumn();
        
        ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "VISUALIZATION CROP");
        ImGui::Text("Drag sliders to zoom into a specific sub-range of the bolus:");
        ImGui::SliderFloat("Crop Start (s)", (float*)&m_crop_min, 0.0f, (float)m_crop_max - 1.0f);
        ImGui::SliderFloat("Crop End (s)", (float*)&m_crop_max, (float)m_crop_min + 1.0f, (float)c.t_raw.back());
        
        if (ImGui::Button("Reset Visual Crop", ImVec2(180, 30))) {
            m_crop_min = 0.0;
            m_crop_max = c.t_raw.back();
        }
        ImGui::SameLine();
        if (ImGui::Button("Crop to Draggable Bounds", ImVec2(240, 30))) {
            m_crop_min = std::max(0.0, m_onset_marker - 5.0);
            m_crop_max = std::min(c.t_raw.back(), m_end_marker + 10.0);
        }
        
        ImGui::Columns(1);
        ImGui::Separator();
        
        // Active fit parameters table comparison
        ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "CURRENT HEMODYNAMIC PARAMETERS");
        if (ImGui::BeginTable("ParamsTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Variable");
            ImGui::TableSetupColumn("Amplitude");
            ImGui::TableSetupColumn("Time-to-Peak (T2p)");
            ImGui::TableSetupColumn("FWHM");
            ImGui::TableSetupColumn("Baseline");
            ImGui::TableSetupColumn("CNR");
            ImGui::TableSetupColumn("Onset (OnT)");
            ImGui::TableHeadersRow();
            
            auto display_val = [](double val) {
                if (std::isnan(val)) ImGui::Text("N/A");
                else ImGui::Text("%.4f", val);
            };
            
            // Fitted row
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("Fitted");
            ImGui::TableNextColumn(); display_val(rec.f_amp);
            ImGui::TableNextColumn(); display_val(rec.f_t2p);
            ImGui::TableNextColumn(); display_val(rec.f_fwhm);
            ImGui::TableNextColumn(); display_val(rec.f_m);
            ImGui::TableNextColumn(); display_val(rec.f_cnr);
            ImGui::TableNextColumn(); display_val(rec.ont);
            
            // Pre-guess row
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("Estimated (Init)");
            ImGui::TableNextColumn(); display_val(rec.init_amp);
            ImGui::TableNextColumn(); display_val(rec.init_t2p);
            ImGui::TableNextColumn(); display_val(rec.init_fwhm);
            ImGui::TableNextColumn(); display_val(rec.init_m);
            ImGui::TableNextColumn(); display_val(rec.init_cnr);
            ImGui::TableNextColumn(); display_val(rec.click_onset);
            
            ImGui::EndTable();
        }
        
        ImGui::EndChild();
    }
};

// ============================================================================
// Application Entry Point
// ============================================================================

int main(int argc, char** argv) {
    BolusApp app;
    if (!app.init()) {
        std::cerr << "Failed to initialize Bolus GUI App!" << std::endl;
        return -1;
    }
    
    // Automatically load dataset if passed on command line
    if (argc > 1) {
        std::filesystem::path path(argv[1]);
        if (std::filesystem::exists(path)) {
            std::string abs_path = std::filesystem::absolute(path).string();
            app.load_dataset(abs_path);
        }
    }
    
    app.run();
    return 0;
}
