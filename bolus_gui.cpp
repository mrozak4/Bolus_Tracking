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
#include "bolus_gui.hpp"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#endif

std::string get_resource_path(const std::string& rel_path) {
    if (std::filesystem::exists(rel_path)) {
        return rel_path;
    }
#if defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        std::filesystem::path exe_path(path);
        std::filesystem::path bundle_res = exe_path.parent_path() / ".." / "Resources" / rel_path;
        if (std::filesystem::exists(bundle_res)) {
            return bundle_res.string();
        }
    }
#endif
    return rel_path;
}

bool is_valid_ttf(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    unsigned char magic[4] = {0};
    if (!file.read(reinterpret_cast<char*>(magic), 4)) {
        return false;
    }
    // Check TrueType/OpenType magic: 0x00 0x01 0x00 0x00, 'OTTO', or 'ttcf'
    bool is_ttf = (magic[0] == 0x00 && magic[1] == 0x01 && magic[2] == 0x00 && magic[3] == 0x00);
    bool is_otf = (magic[0] == 'O' && magic[1] == 'T' && magic[2] == 'T' && magic[3] == 'O');
    bool is_ttc = (magic[0] == 't' && magic[1] == 't' && magic[2] == 'c' && magic[3] == 'f');
    return is_ttf || is_otf || is_ttc;
}

void play_sound_cross_platform(const std::string& audio_path) {
#if defined(_WIN32)
    std::string win_cmd = "powershell -WindowStyle Hidden -Command \"Add-Type -AssemblyName PresentationCore; $player = New-Object system.windows.media.mediaplayer; $player.Open('" + audio_path + "'); $player.Play(); Start-Sleep -s 8\" &";
    std::system(win_cmd.c_str());
#elif defined(__APPLE__)
    std::string mac_cmd = "afplay -t 8 \"" + audio_path + "\" &";
    std::system(mac_cmd.c_str());
#else
    std::string lin_cmd = "(aplay -q \"" + audio_path + "\" || paplay \"" + audio_path + "\" || pw-play \"" + audio_path + "\" || play -q \"" + audio_path + "\" || mpg123 -q \"" + audio_path + "\" || ffplay -nodisp -autoexit -loglevel quiet \"" + audio_path + "\" || cvlc --play-and-exit \"" + audio_path + "\") > /dev/null 2>&1 &";
    std::system(lin_cmd.c_str());
#endif
}




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
    TIFFSetWarningHandler(nullptr);
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

FileBrowser::FileBrowser() {
    current_path = std::filesystem::current_path();
    refresh();
}

void FileBrowser::refresh() {
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

void FileBrowser::draw(const char* title) {
    if (!open) return;
    const char* actual_title = tr ? tr->dialog_title.c_str() : title;
    ImGui::OpenPopup(actual_title);
    ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(actual_title, &open, 0)) {
        ImGui::Text(tr ? (tr->current_folder + ": %s").c_str() : "Current Folder: %s", current_path.string().c_str());
        
        char path_buf[1024];
        strncpy(path_buf, current_path.string().c_str(), sizeof(path_buf));
        if (ImGui::InputText(tr ? tr->path_selector.c_str() : "Path Selector", path_buf, sizeof(path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
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
        
        if (ImGui::Button(tr ? tr->btn_select_folder.c_str() : "Select Current Folder", ImVec2(180, 0))) {
            selected_file = "";
            open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (!selected_file.empty()) {
            if (ImGui::Button(tr ? tr->btn_open_file.c_str() : "Open Selected File", ImVec2(180, 0))) {
                open = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr ? tr->btn_close_dialog.c_str() : "Close Dialog", ImVec2(120, 0))) {
            open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ============================================================================
// Custom range slider for visual crop selection
// ============================================================================
static bool RangeSlider(const char* id_str, double* v_min, double* v_max, double v_min_limit, double v_max_limit, const ImVec2& size) {
    ImGui::PushID(id_str);
    
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Add an invisible button to handle inputs
    ImGui::InvisibleButton("slider_bar", size);
    bool is_active = ImGui::IsItemActive();
    
    float width = size.x;
    float height = size.y;
    
    double range_limit = v_max_limit - v_min_limit;
    if (range_limit <= 0.0) range_limit = 1.0;
    
    // Normalize coordinates
    float t_min = (float)((*v_min - v_min_limit) / range_limit);
    float t_max = (float)((*v_max - v_min_limit) / range_limit);
    t_min = std::max(0.0f, std::min(1.0f, t_min));
    t_max = std::max(0.0f, std::min(1.0f, t_max));
    
    float x_min = pos.x + t_min * width;
    float x_max = pos.x + t_max * width;
    
    // Check if dragging left or right handle, or middle section
    static int dragging_handle = 0; // 0 = none, 1 = min handle, 2 = max handle, 3 = middle range
    static float start_t_min = 0.0f;
    static float start_t_max = 0.0f;
    static float drag_start_x = 0.0f;
    
    float handle_width = 12.0f;
    
    if (ImGui::IsItemActivated()) {
        ImVec2 mouse_pos = ImGui::GetIO().MousePos;
        float mx = mouse_pos.x;
        
        // Check distance to min/max handle
        float dist_min = std::abs(mx - x_min);
        float dist_max = std::abs(mx - x_max);
        
        drag_start_x = mx;
        start_t_min = t_min;
        start_t_max = t_max;
        
        if (dist_min < handle_width && dist_min <= dist_max) {
            dragging_handle = 1;
        } else if (dist_max < handle_width) {
            dragging_handle = 2;
        } else if (mx >= x_min && mx <= x_max) {
            dragging_handle = 3;
        } else {
            // Click outside - jump closest handle to mouse
            float click_t = (mx - pos.x) / width;
            click_t = std::max(0.0f, std::min(1.0f, click_t));
            if (std::abs(click_t - t_min) < std::abs(click_t - t_max)) {
                t_min = std::min(click_t, t_max - 0.01f);
                dragging_handle = 1;
            } else {
                t_max = std::max(click_t, t_min + 0.01f);
                dragging_handle = 2;
            }
            *v_min = v_min_limit + t_min * range_limit;
            *v_max = v_min_limit + t_max * range_limit;
            start_t_min = t_min;
            start_t_max = t_max;
        }
    }
    
    bool changed = false;
    if (is_active && dragging_handle != 0) {
        float dx = ImGui::GetIO().MousePos.x - drag_start_x;
        float dt = dx / width;
        
        if (dragging_handle == 1) {
            float new_t = start_t_min + dt;
            new_t = std::max(0.0f, std::min(start_t_max - 0.01f, new_t));
            *v_min = v_min_limit + new_t * range_limit;
            changed = true;
        } else if (dragging_handle == 2) {
            float new_t = start_t_max + dt;
            new_t = std::max(start_t_min + 0.01f, std::min(1.0f, new_t));
            *v_max = v_min_limit + new_t * range_limit;
            changed = true;
        } else if (dragging_handle == 3) {
            float new_t_min = start_t_min + dt;
            float new_t_max = start_t_max + dt;
            float len = start_t_max - start_t_min;
            if (new_t_min < 0.0f) {
                new_t_min = 0.0f;
                new_t_max = len;
            } else if (new_t_max > 1.0f) {
                new_t_max = 1.0f;
                new_t_min = 1.0f - len;
            }
            *v_min = v_min_limit + new_t_min * range_limit;
            *v_max = v_min_limit + new_t_max * range_limit;
            changed = true;
        }
    }
    
    if (!is_active) {
        dragging_handle = 0;
    }
    
    // Render slider background
    ImU32 bg_color = ImGui::GetColorU32(ImGuiCol_FrameBg);
    ImU32 active_track_color = ImGui::GetColorU32(ImVec4(0.38f, 0.46f, 0.42f, 0.60f)); // Sage green active track
    ImU32 border_color = ImGui::GetColorU32(ImGuiCol_Border);
    
    // Choose a high-contrast handle color that stands out from the blue/dark track
    ImU32 handle_color = ImGui::GetColorU32(ImVec4(0.85f, 0.82f, 0.75f, 1.0f)); // Warm sand/beige
    if (is_active && (dragging_handle == 1 || dragging_handle == 2)) {
        handle_color = ImGui::GetColorU32(ImVec4(0.88f, 0.45f, 0.18f, 1.0f)); // Burnt terracotta orange active
    } else {
        ImVec2 mouse_pos = ImGui::GetIO().MousePos;
        float mx = mouse_pos.x;
        float dist_min = std::abs(mx - x_min);
        float dist_max = std::abs(mx - x_max);
        if (ImGui::IsItemHovered()) {
            if (dist_min < handle_width || dist_max < handle_width) {
                handle_color = ImGui::GetColorU32(ImVec4(0.98f, 0.98f, 1.0f, 1.0f)); // Extremely bright white on hover
            }
        }
    }
    
    // Draw background track
    draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), bg_color, 4.0f);
    draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), border_color, 4.0f);
    
    // Draw active range track
    ImVec2 active_min(pos.x + t_min * width, pos.y);
    ImVec2 active_max(pos.x + t_max * width, pos.y + height);
    draw_list->AddRectFilled(active_min, active_max, active_track_color, 0.0f);
    
    // Draw handles (rounded pills protruding by 4px on top and bottom)
    float protrude = 4.0f;
    ImVec2 h1_min(pos.x + t_min * width - handle_width * 0.5f, pos.y - protrude);
    ImVec2 h1_max(pos.x + t_min * width + handle_width * 0.5f, pos.y + height + protrude);
    draw_list->AddRectFilled(h1_min, h1_max, handle_color, 4.0f);
    draw_list->AddRect(h1_min, h1_max, border_color, 4.0f);
    
    ImVec2 h2_min(pos.x + t_max * width - handle_width * 0.5f, pos.y - protrude);
    ImVec2 h2_max(pos.x + t_max * width + handle_width * 0.5f, pos.y + height + protrude);
    draw_list->AddRectFilled(h2_min, h2_max, handle_color, 4.0f);
    draw_list->AddRect(h2_min, h2_max, border_color, 4.0f);

    // Draw tactile grab ridges inside handles
    auto draw_ridges = [&](float x_center) {
        float cy = pos.y + height * 0.5f;
        for (int offset = -4; offset <= 4; offset += 4) {
            float ry = cy + offset;
            draw_list->AddLine(ImVec2(x_center - 3.0f, ry), ImVec2(x_center + 3.0f, ry), border_color, 1.0f);
        }
    };
    draw_ridges(pos.x + t_min * width);
    draw_ridges(pos.x + t_max * width);
    
    // Add text label overlay inside the slider
    char label_buf[128];
    snprintf(label_buf, sizeof(label_buf), "Visual Crop Range: %.1fs - %.1fs", *v_min, *v_max);
    ImVec2 text_size = ImGui::CalcTextSize(label_buf);
    ImVec2 text_pos(pos.x + (width - text_size.x) * 0.5f, pos.y + (height - text_size.y) * 0.5f);
    draw_list->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), label_buf);
    
    ImGui::PopID();
    return changed;
}

// ============================================================================
// Main Application Class
// ============================================================================

void BolusApp::update_locale() {
    if (m_lang == LANG_EN) {
        m_tr.title_app = "BOLUS TRACKING MANUAL TRIAGE APP";
        m_tr.section_markers = "FITTING WINDOW & INTERACTIVE MARKERS";
        m_tr.section_crop = "VISUALIZATION CROP CONTROLS";
        m_tr.section_params = "CURRENT HEMODYNAMIC PARAMETERS";
        m_tr.sidebar_title = "Triage Sidebar";
        m_tr.checkbox_flagged = "Show only problem cases (FAIL/WARN)";
        m_tr.btn_save_csv = "Save Final CSV";
        m_tr.btn_reset_all = "Reset All";
        m_tr.btn_load_state = "Load State";
        m_tr.btn_save_state = "Save State";
        m_tr.btn_refit = "Re-Fit (LM)";
        m_tr.btn_override = "Force PASS";
        m_tr.btn_revert = "Revert to Original";
        m_tr.btn_reset_crop = "Reset Crop";
        m_tr.btn_crop_bounds = "Crop to Markers";
        m_tr.label_onset = "Onset Marker (s)";
        m_tr.label_peak = "Peak Marker (s)";
        m_tr.label_end = "End Marker (s)";
        m_tr.label_baseline = "Baseline Value";
        m_tr.text_slider_desc = "Use the range slider below the plot to adjust the crop region.";
        m_tr.btn_ok = "OK";
        m_tr.btn_cancel = "Cancel";
        m_tr.modal_reset_title = "Reset All Changes?";
        m_tr.modal_reset_desc = "WARNING: This will discard ALL manual adjustments, overrides,\nand triage edits you have made in this session.\n\nAre you sure you want to proceed?";
        m_tr.btn_reset_confirm = "Yes, Reset All";
        m_tr.modal_save_success = "Save Success";
        m_tr.modal_save_state_success = "Save State Success";
        m_tr.modal_load_state_success = "Load State Success";
        m_tr.text_active_roi = "Active ROI:";
        m_tr.text_qc_flag = "QC Flag:";
        m_tr.text_fit_source = "Fit Source:";
        m_tr.text_dataset_loaded = "Dataset Loaded:";
        m_tr.text_roi_count = "ROI Count:";
        m_tr.text_flagged_count = "Flagged (FAIL/WARN):";
        m_tr.text_manual_count = "Manually Labelled:";
        m_tr.col_variable = "Variable";
        m_tr.col_amplitude = "Amplitude";
        m_tr.col_t2p = "Time-to-Peak (T2p)";
        m_tr.col_fwhm = "FWHM";
        m_tr.col_baseline = "Baseline";
        m_tr.col_cnr = "CNR";
        m_tr.col_onset = "Onset (OnT)";
        m_tr.plot_title = "Trace Fitting Plot";
        m_tr.plot_x_axis = "Time (s)";
        m_tr.plot_y_axis = "Signal (SU)";
        m_tr.plot_raw = "Raw (Detrended)";
        m_tr.plot_denoised = "Denoised";
        m_tr.plot_fit = "Gamma Fit";
        m_tr.current_folder = "Current Folder";
        m_tr.path_selector = "Path Selector";
        m_tr.btn_select_folder = "Select Current Folder";
        m_tr.btn_open_file = "Open Selected File";
        m_tr.btn_close_dialog = "Close Dialog";
        m_tr.dialog_title = "Open Folder or File";
        m_tr.text_total_rois = "Total Dataset ROIs: %d";
        m_tr.text_active_queue = "Active Filter Queue: %d";
        m_tr.text_triage_queue = "Triage Queue: %d / %d";
        m_tr.btn_next_problem = "Next Problem >>";
        m_tr.btn_prev_problem = "<< Previous Problem";
        m_tr.text_no_data = "No subject folder or CSV file loaded yet. Use the top button to open a subject data file.";
        m_tr.text_plot_status_header = "Signal Time Series (SU) - ROI #%d (Size: %d px) | Status: %s (Source: %s)";
        m_tr.title_manual_override = "MANUAL OVERRIDES & FIT WINDOW ADJUSTMENTS";
        m_tr.text_manual_override_desc = "Drag the Onset/Peak/End line markers directly on the plot, then click 'Re-Fit' below to manually optimize parameters.";
        m_tr.btn_revert_loaded = "Revert to Loaded Values";
        m_tr.text_load_subject_data = "Load Subject Data";
        m_tr.text_save_state_msg = "Analysis state paused & saved successfully to:\n%s.gui_state";
        m_tr.text_load_state_msg = "Analysis state resumed successfully from:\n%s.gui_state";
        m_tr.text_save_csv_msg = "Results written successfully to:\n%s";
        m_tr.tag_onset = "Onset: %.1fs";
        m_tr.tag_peak = "Peak: %.1fs";
        m_tr.tag_end = "End: %.1fs";
        m_tr.tag_base = "Base: %.1f";
        m_tr.btn_clear_data = "Clear Subject Data";
        m_tr.qc_pass = "PASS";
        m_tr.qc_warn = "WARN";
        m_tr.qc_fail = "FAIL";
        m_tr.qc_review = "REVIEW";
        m_tr.source_auto = "auto";
        m_tr.source_manual = "manual";
        m_tr.source_override = "override";
        m_tr.label_fitted = "Fitted";
        m_tr.label_estimated_init = "Estimated (Init)";
        m_tr.label_filter = "Filter:";
        m_tr.filter_all = "All";
        m_tr.filter_flagged = "Flagged (FAIL/WARN/REVIEW)";
        m_tr.filter_fail = "FAIL Only";
        m_tr.filter_warn = "WARN Only";
        m_tr.filter_pass = "PASS Only";
        m_tr.filter_review = "REVIEW Only";
        m_tr.label_auto_fit = "Original Auto Fit";
        m_tr.section_denoise = "DENOISING OPTIONS";
        m_tr.label_denoise_strength = "Denoising Strength";
        m_tr.section_actions = "FITTING ACTIONS";
    } else {
        m_tr.title_app = "SUIVI DE BOLUS - TRIAGE MANUEL";
        m_tr.section_markers = "FENÊTRE D'AJUSTEMENT ET MARQUEURS INTERACTIFS";
        m_tr.section_crop = "ROGNAGE ET AFFICHAGE";
        m_tr.section_params = "PARAMÈTRES HÉMODYNAMIQUES";
        m_tr.sidebar_title = "Volet de triage";
        m_tr.checkbox_flagged = "Afficher uniquement les cas à réviser (ÉCHEC/AVERT.)";
        m_tr.btn_save_csv = "Exporter les résultats (CSV)";
        m_tr.btn_reset_all = "Réinitialiser tout";
        m_tr.btn_load_state = "Charger l'état";
        m_tr.btn_save_state = "Enregistrer l'état";
        m_tr.btn_refit = "Réajuster (LM)";
        m_tr.btn_override = "Déroger (PASS)";
        m_tr.btn_revert = "Rétablir les estimations automatiques";
        m_tr.btn_reset_crop = "Réinitialiser le rognage";
        m_tr.btn_crop_bounds = "Rogner aux limites";
        m_tr.label_onset = "Début du bolus (s)";
        m_tr.label_peak = "Pic (s)";
        m_tr.label_end = "Fin (s)";
        m_tr.label_baseline = "Ligne de base";
        m_tr.text_slider_desc = "Utilisez la glissière de plage sous le graphique pour ajuster la zone de rognage.";
        m_tr.btn_ok = "OK";
        m_tr.btn_cancel = "Annuler";
        m_tr.modal_reset_title = "Réinitialiser toutes les modifications ?";
        m_tr.modal_reset_desc = "AVERTISSEMENT : cette opération annulera toutes les modifications,\nvalidations et dérogations manuelles de cette session.\n\nSouhaitez-vous continuer ?";
        m_tr.btn_reset_confirm = "Oui, réinitialiser tout";
        m_tr.modal_save_success = "Enregistrement réussi";
        m_tr.modal_save_state_success = "État enregistré avec succès";
        m_tr.modal_load_state_success = "État chargé avec succès";
        m_tr.text_active_roi = "ROI active :";
        m_tr.text_qc_flag = "Statut de contrôle de qualité (CQ) :";
        m_tr.text_fit_source = "Source de l'ajustement :";
        m_tr.text_dataset_loaded = "Jeu de données chargé :";
        m_tr.text_roi_count = "Nombre de ROI :";
        m_tr.text_flagged_count = "Signalés (ÉCHEC/AVERT.) :";
        m_tr.text_manual_count = "Ajustés manuellement :";
        m_tr.col_variable = "Variable";
        m_tr.col_amplitude = "Amplitude";
        m_tr.col_t2p = "Temps au pic (TAP)";
        m_tr.col_fwhm = "Largeur à mi-hauteur (LMH)";
        m_tr.col_baseline = "Ligne de base";
        m_tr.col_cnr = "Rapport contraste-bruit (RCB)";
        m_tr.col_onset = "Temps de début (TD)";
        m_tr.plot_title = "Graphique de modélisation";
        m_tr.plot_x_axis = "Temps (s)";
        m_tr.plot_y_axis = "Intensité (UA)";
        m_tr.plot_raw = "Signal brut (sans tendance)";
        m_tr.plot_denoised = "Signal débruité";
        m_tr.plot_fit = "Courbe d'ajustement Gamma";
        m_tr.current_folder = "Dossier actuel";
        m_tr.path_selector = "Sélecteur de chemin";
        m_tr.btn_select_folder = "Sélectionner ce dossier";
        m_tr.btn_open_file = "Ouvrir le fichier sélectionné";
        m_tr.btn_close_dialog = "Fermer";
        m_tr.dialog_title = "Ouvrir dossier ou fichier";
        m_tr.text_total_rois = "Total ROI du jeu de données : %d";
        m_tr.text_active_queue = "File de filtrage active : %d";
        m_tr.text_triage_queue = "File de triage : %d / %d";
        m_tr.btn_next_problem = "Suivant >>";
        m_tr.btn_prev_problem = "<< Précédent";
        m_tr.text_no_data = "Aucun jeu de données chargé. Utilisez le bouton ci-dessus pour charger un dossier ou un fichier CSV.";
        m_tr.text_plot_status_header = "Signal (UA) - ROI #%d (%d px) | Statut : %s (Source : %s)";
        m_tr.title_manual_override = "AJUSTEMENTS ET DÉROGATIONS MANUELLES";
        m_tr.text_manual_override_desc = "Glissez les marqueurs de début, de pic et de fin directement sur le graphique, puis cliquez sur 'Réajuster (LM)'.";
        m_tr.btn_revert_loaded = "Rétablir les valeurs chargées";
        m_tr.text_load_subject_data = "Charger les données";
        m_tr.text_save_state_msg = "L'état de l'analyse a été sauvegardé avec succès dans :\n%s.gui_state";
        m_tr.text_load_state_msg = "L'état de l'analyse a été restauré avec succès depuis :\n%s.gui_state";
        m_tr.text_save_csv_msg = "Les résultats ont été enregistrés avec succès dans :\n%s";
        m_tr.tag_onset = "Début : %.1fs";
        m_tr.tag_peak = "Pic : %.1fs";
        m_tr.tag_end = "Fin : %.1fs";
        m_tr.tag_base = "Base : %.1f";
        m_tr.btn_clear_data = "Effacer les données";
        m_tr.qc_pass = "CONFORME";
        m_tr.qc_warn = "AVERT.";
        m_tr.qc_fail = "ÉCHEC";
        m_tr.qc_review = "À RÉVISER";
        m_tr.source_auto = "auto";
        m_tr.source_manual = "manuel";
        m_tr.source_override = "dérogé";
        m_tr.label_fitted = "Modélisé";
        m_tr.label_estimated_init = "Estimé (initial)";
        m_tr.label_filter = "Filtre :";
        m_tr.filter_all = "Tout";
        m_tr.filter_flagged = "À réviser (ÉCHEC/AVERT./À RÉVISER)";
        m_tr.filter_fail = "ÉCHEC uniquement";
        m_tr.filter_warn = "AVERT. uniquement";
        m_tr.filter_pass = "CONFORME uniquement";
        m_tr.filter_review = "À RÉVISER uniquement";
        m_tr.label_auto_fit = "Ajustement auto initial";
        m_tr.section_denoise = "OPTIONS DE DÉBRUITAGE";
        m_tr.label_denoise_strength = "Force du débruitage";
        m_tr.section_actions = "ACTIONS D'AJUSTEMENT";
    }
}
BolusApp::BolusApp() : m_fitter(1e-6, 1023.0, 1e-6, 1e6, 0.5, 1e6), m_denoise_strength_factor(1.0f) {}

BolusApp::~BolusApp() {
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (m_window) {
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
}

bool BolusApp::init() {
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
    
    m_window = glfwCreateWindow(1600, 950, "Bolus Tracking GUI - Triage App", NULL, NULL);
    if (!m_window) return false;
    
    glfwMakeContextCurrent(m_window);
    glfwMaximizeWindow(m_window);
    glfwSwapInterval(1);
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Sleek Premium Mid-Century Modern Theme
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 14.0f;
    style.ChildRounding = 12.0f;
    style.FrameRounding = 10.0f;
    style.GrabRounding = 8.0f;
    style.PopupRounding = 12.0f;
    style.ScrollbarRounding = 10.0f;
    style.TabRounding = 8.0f;
        
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.ItemSpacing = ImVec2(12.0f, 10.0f);
        style.FramePadding = ImVec2(8.0f, 6.0f);
        style.WindowPadding = ImVec2(16.0f, 16.0f);
        
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.94f, 0.90f, 1.00f); // Warm cream text
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.60f, 0.58f, 0.55f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.18f, 0.18f, 0.17f, 1.00f); // Warm charcoal base
        colors[ImGuiCol_ChildBg]                = ImVec4(0.22f, 0.22f, 0.20f, 0.95f); // Panel backdrop (dark wood tone-ish)
        colors[ImGuiCol_PopupBg]                = ImVec4(0.20f, 0.20f, 0.19f, 0.98f);
        colors[ImGuiCol_Border]                 = ImVec4(0.35f, 0.32f, 0.28f, 0.50f); // Muted warm brown/bronze border
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.26f, 0.25f, 0.23f, 1.00f); // Sandbox input fields
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.32f, 0.30f, 0.28f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.38f, 0.35f, 0.32f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.28f, 0.25f, 0.22f, 1.00f); // Mustard-accented header space
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.32f, 0.28f, 0.24f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.20f, 0.18f, 0.16f, 1.00f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.22f, 0.22f, 0.20f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.18f, 0.18f, 0.17f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.40f, 0.38f, 0.34f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.50f, 0.46f, 0.42f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.60f, 0.55f, 0.50f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.88f, 0.55f, 0.25f, 1.00f); // Burnt orange checks
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.50f, 0.58f, 0.45f, 1.00f); // Sage green grab
        colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.88f, 0.55f, 0.25f, 1.00f); // Burnt orange highlight
        colors[ImGuiCol_Button]                 = ImVec4(0.38f, 0.42f, 0.35f, 1.00f); // Muted avocado / sage button
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.46f, 0.52f, 0.42f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.88f, 0.55f, 0.25f, 1.00f); // Burnt orange active clicks
        colors[ImGuiCol_Header]                 = ImVec4(0.35f, 0.38f, 0.32f, 1.00f); // Header lists
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.42f, 0.46f, 0.38f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.50f, 0.55f, 0.45f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.35f, 0.32f, 0.28f, 0.50f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.88f, 0.55f, 0.25f, 0.78f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.38f, 0.42f, 0.35f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.38f, 0.42f, 0.35f, 0.67f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.88f, 0.55f, 0.25f, 0.95f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.28f, 0.30f, 0.26f, 0.86f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.38f, 0.42f, 0.35f, 0.80f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.38f, 0.42f, 0.35f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.20f, 0.22f, 0.18f, 0.97f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.28f, 0.30f, 0.26f, 1.00f);
        colors[ImGuiCol_PlotLines]              = ImVec4(0.85f, 0.80f, 0.70f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.95f, 0.65f, 0.35f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.26f, 0.26f, 0.24f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.35f, 0.35f, 0.32f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.28f, 0.28f, 0.26f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.88f, 0.55f, 0.25f, 0.35f);
        colors[ImGuiCol_DragDropTarget]         = ImVec4(0.88f, 0.55f, 0.25f, 0.90f);
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.12f, 0.12f, 0.11f, 0.60f);
        
        // Load custom high-quality MCM Outfit fonts
        ImFontConfig font_config;
        font_config.OversampleH = 3;
        font_config.OversampleV = 3;
        font_config.PixelSnapH = true;
        
        std::string font_reg_path = get_resource_path("resources/fonts/Outfit-Medium.ttf");
        std::string font_bold_path = get_resource_path("resources/fonts/Outfit-Bold.ttf");
        
        if (is_valid_ttf(font_reg_path)) {
            m_font_regular = io.Fonts->AddFontFromFileTTF(font_reg_path.c_str(), 16.0f, &font_config);
        }
        if (is_valid_ttf(font_bold_path)) {
            m_font_bold = io.Fonts->AddFontFromFileTTF(font_bold_path.c_str(), 18.0f, &font_config);
        }
        
        if (!m_font_regular) {
            m_font_regular = io.Fonts->AddFontDefault();
        }
        if (!m_font_bold) {
            m_font_bold = m_font_regular;
        }


        // Initialize translations
        update_locale();
        m_browser.tr = &m_tr;

        ImGui_ImplGlfw_InitForOpenGL(m_window, true);
        ImGui_ImplOpenGL3_Init(glsl_version);
        
        return true;
    }

    /**
     * @brief Run the interactive application event loop.
     */
void BolusApp::run() {
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

bool BolusApp::load_dataset(const std::string& csv_path) {
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
        
        if (m_records.size() != m_cache.size()) {
            std::cerr << "Warning: Mismatch between CSV record count (" << m_records.size()
                      << ") and ROI cache count (" << m_cache.size() << "). Aligning to minimum." << std::endl;
            size_t min_sz = std::min(m_records.size(), m_cache.size());
            m_records.resize(min_sz);
            m_cache.resize(min_sz);
        }
        
        // Reconstruct missing interactive markers if NaN
        for (size_t i = 0; i < m_records.size(); ++i) {
            auto& rec = m_records[i];
            const auto& c = m_cache[i];
            if (std::isnan(rec.click_onset) || std::isnan(rec.click_peak) || std::isnan(rec.click_end) || std::isnan(rec.click_start)) {
                if (!c.y_us.empty()) {
                    AutoEstimateResults auto_res = m_fitter.auto_estimate_params(c.y_us, c.t_us, m_fr, m_upsample_factor);
                    if (std::isnan(rec.click_start)) rec.click_start = auto_res.click_start;
                    if (std::isnan(rec.click_onset)) rec.click_onset = auto_res.click_onset;
                    if (std::isnan(rec.click_peak)) rec.click_peak = auto_res.click_peak;
                    if (std::isnan(rec.click_end)) rec.click_end = auto_res.click_end;
                }
            }
            // Recompute fit curve now that we have the reconstructed click_onset
            precompute_fit_plot(i);
        }
        
        // Initialize default GUI ROI states
        m_gui_roi_states.resize(m_records.size());
        for (size_t i = 0; i < m_records.size(); ++i) {
            const auto& rec = m_records[i];
            const auto& c = m_cache[i];
            auto& s = m_gui_roi_states[i];
            s.roi_id = rec.roi_id;
            s.crop_min = (!std::isnan(rec.click_start) && rec.click_start >= 0.0) ? rec.click_start : 0.0;
            s.crop_max = c.t_raw.empty() ? 120.0 : c.t_raw.back();
            s.onset = !std::isnan(rec.click_onset) ? rec.click_onset : s.crop_max * 0.35;
            s.peak = !std::isnan(rec.click_peak) ? rec.click_peak : (!std::isnan(rec.f_t2p) && !std::isnan(rec.click_onset) ? rec.click_onset + rec.f_t2p : s.onset + 4.0);
            s.end = !std::isnan(rec.click_end) ? rec.click_end : s.peak + 6.0;
            s.baseline = !std::isnan(rec.f_m) ? rec.f_m : (!std::isnan(rec.init_m) ? rec.init_m : c.y_denoised.front());
            s.qc_flag = rec.qc_flag;
            s.fit_source = rec.fit_source;
        }

        // Backup the pristine loaded CSV and initial GUI state (before loading user modifications)
        std::vector<CsvRecord> original_loaded_records = m_records;
        std::vector<RoiState> original_loaded_states = m_gui_roi_states;

        // Temporarily clear selection index while pre-calculating pristine auto-fits
        int backup_selected_roi_idx = m_selected_roi_idx;
        m_selected_roi_idx = -1;
        for (size_t i = 0; i < m_records.size(); ++i) {
            run_fit_on_record(i, true);
        }
        m_records_backup = m_records;
        m_gui_roi_states_backup = m_gui_roi_states;

        // Restore originally loaded CSV values
        m_records = original_loaded_records;
        m_gui_roi_states = original_loaded_states;
        m_selected_roi_idx = backup_selected_roi_idx;

        // Try loading gui state
        load_gui_state();
        
        build_triage_queue();
        
        // Select either the loaded last active index, or default to triage queue start
        if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records.size())) {
            int target_idx = m_selected_roi_idx;
            m_selected_roi_idx = -1; // Bypass saving current modified state
            select_record(target_idx);
        } else if (!m_triage_queue.empty()) {
            m_selected_roi_idx = -1; // Bypass saving current modified state
            select_record(m_triage_queue[0]);
        } else {
            m_selected_roi_idx = -1; // Bypass saving current modified state
            select_record(0);
        }
        
        return true;
    }

void BolusApp::save_gui_state() {
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
        out << "FilterFlaggedOnly=" << (m_qc_filter_type == 1 ? 1 : 0) << "\n";
        out << "QcFilterType=" << m_qc_filter_type << "\n";
        out << "DenoiseStrengthFactor=" << m_denoise_strength_factor << "\n";
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

void BolusApp::load_gui_state() {
    if (m_csv_path.empty()) return;
    std::string state_path = m_csv_path + ".gui_state";
    std::ifstream in(state_path);
    if (!in.is_open()) return;
    
    int loaded_selected_roi_idx = -1;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = line.substr(0, eq_pos);
            std::string val = line.substr(eq_pos + 1);
            if (key == "LastSelectedRoiIndex") {
                try { loaded_selected_roi_idx = std::stoi(val); } catch (...) {}
            } else if (key == "FilterFlaggedOnly") {
                try {
                    int val_int = std::stoi(val);
                    if (val_int != 0) {
                        m_qc_filter_type = 1;
                    } else {
                        m_qc_filter_type = 0;
                    }
                } catch (...) {}
            } else if (key == "QcFilterType") {
                try { m_qc_filter_type = std::stoi(val); } catch (...) {}
            } else if (key == "DenoiseStrengthFactor") {
                try { m_denoise_strength_factor = std::stof(val); } catch (...) {}
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
    // Re-apply loaded denoising strength factor to all traces and fit curves
    precompute_all_traces();

    // Temporarily clear selection index so run_fit_on_record operates purely on loaded state arrays cleanly
    m_selected_roi_idx = -1;
    for (size_t i = 0; i < m_gui_roi_states.size(); ++i) {
        if (m_gui_roi_states[i].fit_source == "manual") {
            run_fit_on_record(i, false);
        } else if (m_gui_roi_states[i].fit_source == "override") {
            m_records[i].qc_flag = "PASS";
            m_records[i].fit_source = "override";
            m_gui_roi_states[i].qc_flag = "PASS";
            precompute_fit_plot(i);
        } else {
            precompute_fit_plot(i);
        }
    }
    m_selected_roi_idx = loaded_selected_roi_idx;
}

    /**
     * @brief Precompute raw signals, drift, denoised, and upsampled spline arrays for all ROIs.
     */
void BolusApp::precompute_all_traces() {
        m_cache.resize(m_rois.size());
        for (size_t r = 0; r < m_rois.size(); ++r) {
            precompute_single_trace(r);
        }
    }

void BolusApp::precompute_single_trace(size_t r) {
        if (r >= m_rois.size()) return;
        if (m_cache.size() <= r) m_cache.resize(m_rois.size());
        
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
        c.y_raw_detrended = detrended;
        
        // Calculate raw trace CNR
        int n_base = std::min((int)std::round(2.0 * m_fr), (int)std::round(detrended.size() * 0.1));
        n_base = std::max(2, n_base);
        std::vector<double> raw_base_win(detrended.begin(), detrended.begin() + n_base);
        double raw_baseline = SignalProcessor::compute_median(raw_base_win);
        double sum_raw_base = 0.0;
        for (double val : raw_base_win) sum_raw_base += val;
        double mean_raw_base = sum_raw_base / raw_base_win.size();
        double raw_sd_base = SignalProcessor::compute_std(raw_base_win, mean_raw_base);
        
        double raw_max_val = -1e9;
        for (double val : detrended) {
            if (val > raw_max_val) raw_max_val = val;
        }
        double raw_amp = raw_max_val - raw_baseline;
        double raw_cnr = (raw_sd_base > 0.0) ? (raw_amp / raw_sd_base) : 0.0;
        
        double denoise_thresh = 2.0;
        int denoise_half_win = 5;
        if (raw_cnr < 4.0) {
            denoise_thresh = 1.5;
            denoise_half_win = 7;
        } else if (raw_cnr >= 15.0) {
            denoise_thresh = 3.0;
            denoise_half_win = 3;
        } else if (raw_cnr >= 8.0) {
            denoise_thresh = 2.5;
            denoise_half_win = 5;
        }
        
        // Adjust threshold and half-window size based on GUI denoising strength multiplier
        if (m_denoise_strength_factor > 0.01f) {
            denoise_thresh = denoise_thresh / static_cast<double>(m_denoise_strength_factor);
            denoise_half_win = std::max(1, static_cast<int>(std::round(denoise_half_win * m_denoise_strength_factor)));
        }
        
        // 4. Denoise and Spline
        c.y_denoised = SignalProcessor::denoise_trace(detrended, denoise_thresh, denoise_half_win);
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
        int n_base_us = std::min((int)std::round(2.0 * m_fr * m_upsample_factor), (int)std::round(c.y_us.size() * 0.1));
        n_base_us = std::max(1, n_base_us);
        std::vector<double> base_win(c.y_us.begin(), c.y_us.begin() + n_base_us);
        double mean_base = 0.0;
        for (double x : base_win) mean_base += x;
        mean_base /= base_win.size();
        c.sd_base = SignalProcessor::compute_std(base_win, mean_base);
        if (c.sd_base <= 0.0) c.sd_base = 0.05;
        
        // 6. Precompute Fit Plot Curve (from CSV record parameters)
        precompute_fit_plot(r);
    }

    /**
     * @brief Precompute plot coordinates for the active fit parameters of a specific cache index.
     */
void BolusApp::precompute_fit_plot(size_t cache_idx) {
        auto& c = m_cache[cache_idx];
        const auto& rec = m_records[cache_idx];
        c.t_fit_plot.clear();
        c.y_fit_plot.clear();
        c.t_fit_auto_plot.clear();
        c.y_fit_auto_plot.clear();
        
        if (std::isnan(rec.f_amp) || std::isnan(rec.f_t2p) || std::isnan(rec.f_fwhm) || std::isnan(rec.f_m) || std::isnan(rec.ont)) {
            return;
        }
        
        double alpha = ((rec.f_t2p * rec.f_t2p) / (rec.f_fwhm * rec.f_fwhm)) * 8.0 * std::log(2.0);
        double beta = ((rec.f_fwhm * rec.f_fwhm) / rec.f_t2p) / (8.0 * std::log(2.0));
        
        c.t_fit_plot = c.t_us;
        c.y_fit_plot.resize(c.t_fit_plot.size());
        
        double onset_t = !std::isnan(rec.click_onset) ? rec.click_onset : 0.0;
        double end_t = !std::isnan(rec.click_end) ? rec.click_end : c.t_us.back();
        for (size_t i = 0; i < c.t_fit_plot.size(); ++i) {
            double t = c.t_fit_plot[i];
            if (t > end_t) {
                c.y_fit_plot[i] = std::numeric_limits<double>::quiet_NaN();
                continue;
            }
            double val = rec.f_m;
            if (t >= onset_t) {
                double dt = t - onset_t;
                val = rec.f_m + rec.f_amp * std::pow(dt / rec.f_t2p, alpha) * std::exp(-(dt - rec.f_t2p) / beta);
            }
            c.y_fit_plot[i] = val;
        }

        // Compute original auto fit curve
        const CsvRecord* rec_auto_ptr = nullptr;
        if (cache_idx < m_records_backup.size()) {
            rec_auto_ptr = &m_records_backup[cache_idx];
        } else {
            rec_auto_ptr = &rec;
        }
        
        if (rec_auto_ptr) {
            const auto& rec_auto = *rec_auto_ptr;
            if (!std::isnan(rec_auto.f_amp) && !std::isnan(rec_auto.f_t2p) && !std::isnan(rec_auto.f_fwhm) && !std::isnan(rec_auto.f_m) && !std::isnan(rec_auto.ont)) {
                double alpha_auto = ((rec_auto.f_t2p * rec_auto.f_t2p) / (rec_auto.f_fwhm * rec_auto.f_fwhm)) * 8.0 * std::log(2.0);
                double beta_auto = ((rec_auto.f_fwhm * rec_auto.f_fwhm) / rec_auto.f_t2p) / (8.0 * std::log(2.0));
                
                c.t_fit_auto_plot = c.t_us;
                c.y_fit_auto_plot.resize(c.t_fit_auto_plot.size());
                
                double onset_t_auto = !std::isnan(rec_auto.click_onset) ? rec_auto.click_onset : 0.0;
                double end_t_auto = !std::isnan(rec_auto.click_end) ? rec_auto.click_end : c.t_us.back();
                
                for (size_t i = 0; i < c.t_fit_auto_plot.size(); ++i) {
                    double t = c.t_fit_auto_plot[i];
                    if (t > end_t_auto) {
                        c.y_fit_auto_plot[i] = std::numeric_limits<double>::quiet_NaN();
                        continue;
                    }
                    double val = rec_auto.f_m;
                    if (t >= onset_t_auto) {
                        double dt = t - onset_t_auto;
                        val = rec_auto.f_m + rec_auto.f_amp * std::pow(dt / rec_auto.f_t2p, alpha_auto) * std::exp(-(dt - rec_auto.f_t2p) / beta_auto);
                    }
                    c.y_fit_auto_plot[i] = val;
                }
            }
        }
    }

    /**
     * @brief Update the list indices that need manual review or have failed.
     */
void BolusApp::build_triage_queue() {
        m_triage_queue.clear();
        for (size_t i = 0; i < m_records.size(); ++i) {
            bool matches = false;
            if (m_qc_filter_type == 0) { // All
                matches = true;
            } else if (m_qc_filter_type == 1) { // Flagged (FAIL/WARN/REVIEW)
                matches = (m_records[i].qc_flag == "FAIL" || m_records[i].qc_flag == "WARN" || m_records[i].qc_flag == "REVIEW");
            } else if (m_qc_filter_type == 2) { // FAIL Only
                matches = (m_records[i].qc_flag == "FAIL");
            } else if (m_qc_filter_type == 3) { // WARN Only
                matches = (m_records[i].qc_flag == "WARN");
            } else if (m_qc_filter_type == 4) { // PASS Only
                matches = (m_records[i].qc_flag == "PASS");
            } else if (m_qc_filter_type == 5) { // REVIEW Only
                matches = (m_records[i].qc_flag == "REVIEW");
            }
            if (matches) {
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
void BolusApp::select_record(int idx) {
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
void BolusApp::run_fit_on_current_roi() {
        if (m_selected_roi_idx < 0) return;
        run_fit_on_record(m_selected_roi_idx, false);
}

void BolusApp::run_fit_on_record(int idx, bool is_auto) {
        if (idx < 0 || idx >= static_cast<int>(m_records.size())) return;
        
        auto& rec = m_records[idx];
        auto& c = m_cache[idx];
        auto& s = m_gui_roi_states[idx];
        
        double crop_min = 0.0;
        double crop_max = c.t_raw.empty() ? 120.0 : c.t_raw.back();
        double onset = 0.0;
        double peak = 0.0;
        double end = 0.0;
        double baseline = 0.0;
        
        if (is_auto) {
            if (!c.y_us.empty()) {
                AutoEstimateResults auto_res = m_fitter.auto_estimate_params(c.y_us, c.t_us, m_fr, m_upsample_factor);
                crop_min = auto_res.click_start;
                onset = auto_res.click_onset;
                peak = auto_res.click_peak;
                end = auto_res.click_end;
                baseline = auto_res.init_params[3];
            } else {
                crop_min = 0.0;
                onset = crop_max * 0.35;
                peak = onset + 4.0;
                end = peak + 6.0;
                baseline = 0.0;
            }
        } else {
            if (idx == m_selected_roi_idx) {
                crop_min = m_crop_min;
                crop_max = m_crop_max;
                onset = m_onset_marker;
                peak = m_peak_marker;
                end = m_end_marker;
                baseline = m_baseline_marker;
            } else {
                crop_min = s.crop_min;
                crop_max = s.crop_max;
                onset = s.onset;
                peak = s.peak;
                end = s.end;
                baseline = s.baseline;
            }
        }
        
        // Map visual marker times to the upsampled trace vector
        auto find_nearest_idx = [](const std::vector<double>& vec, double val) -> int {
            auto it = std::lower_bound(vec.begin(), vec.end(), val);
            if (it == vec.end()) return vec.size() - 1;
            if (it == vec.begin()) return 0;
            double d1 = *it - val;
            double d2 = val - *(it - 1);
            return (d1 < d2) ? std::distance(vec.begin(), it) : std::distance(vec.begin(), it - 1);
        };
        
        int start_idx = find_nearest_idx(c.t_us, onset);
        int end_idx = find_nearest_idx(c.t_us, end);
        int peak_idx = find_nearest_idx(c.t_us, peak);
        
        if (end_idx <= start_idx + 5) {
            std::cerr << "Fit Window is too short!" << std::endl;
            return;
        }
        
        // Prepare sub-vectors relative to the onset
        std::vector<double> t_fit(end_idx - start_idx);
        std::vector<double> y_fit(end_idx - start_idx);
        for (int i = start_idx; i < end_idx; ++i) {
            t_fit[i - start_idx] = c.t_us[i] - c.t_us[start_idx];
            y_fit[i - start_idx] = c.y_us[i];
        }
        
        // Formulate guesses
        double guess_amp = c.y_us[peak_idx] - baseline;
        if (guess_amp < 1e-4) guess_amp = 10.0;
        
        double guess_t2p = c.t_us[peak_idx] - c.t_us[start_idx];
        if (guess_t2p < 0.1) guess_t2p = 3.0;
        
        double guess_fwhm = (c.t_us[end_idx] - c.t_us[start_idx]) / 2.0;
        if (guess_fwhm < 0.1) guess_fwhm = 5.0;
        
        std::vector<double> init_params = {guess_amp, guess_t2p, guess_fwhm, baseline};
        
        // Run fit solver
        bool fit_success = false;
        bool pass2_run = false;
        std::vector<double> popt = m_fitter.run_nonlinear_fit(t_fit, y_fit, init_params, c.sd_base, fit_success, pass2_run);
        
        // Update CsvRecord
        rec.click_start = crop_min;
        rec.click_onset = onset;
        rec.click_peak = peak;
        rec.click_end = end;
        
        rec.init_amp = guess_amp;
        rec.init_t2p = guess_t2p;
        rec.init_fwhm = guess_fwhm;
        rec.init_m = baseline;
        rec.init_cnr = guess_amp / c.sd_base;
        rec.init_snr = baseline / c.sd_base;
        
        rec.fit_source = is_auto ? "auto" : "manual";
        
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
                
                double shift = 0.0;
                if (idx >= 0 && idx < static_cast<int>(m_records_backup.size())) {
                    shift = m_records_backup[idx].ont - m_records_backup[idx].ont_sc;
                }
                if (std::isnan(shift) || std::isinf(shift)) shift = 0.0;
                rec.ont_sc = rec.ont - shift;
                
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
                double actual_max_t2p = (m_fitter.max_t2p >= 1e5 && !t_fit.empty()) ? t_fit.back() : m_fitter.max_t2p;
                double actual_max_fwhm = (m_fitter.max_fwhm >= 1e5 && !t_fit.empty()) ? t_fit.back() : m_fitter.max_fwhm;
                
                rec.qc_flag = BolusFitter::determine_qc_flag(
                    popt[0], popt[1], popt[2], popt[3], rec.f_cnr,
                    m_fitter.min_amp, m_fitter.max_amp, m_fitter.min_t2p, actual_max_t2p,
                    m_fitter.min_fwhm, actual_max_fwhm, fit_success, pass2_run
                );
                
                if (std::isnan(rec.auc) || std::isnan(rec.aucn) || std::isnan(rec.ttlb) || 
                    std::isnan(rec.ttm) || std::isnan(rec.tthb) || std::isnan(rec.ont)) {
                    rec.qc_flag = "FAIL";
                }
                
                rec.ves_type = BolusFitter::suggest_vessel_type(rec.ont, rec.f_t2p, rec.f_fwhm, rec.f_amp, rec.qc_flag);
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
        s.crop_min = crop_min;
        s.crop_max = crop_max;
        s.onset = onset;
        s.peak = peak;
        s.end = end;
        s.baseline = baseline;
        s.qc_flag = rec.qc_flag;
        s.fit_source = rec.fit_source;
        
        // If this is the active GUI record, sync GUI variables as well
        if (idx == m_selected_roi_idx) {
            m_crop_min = crop_min;
            m_crop_max = crop_max;
            m_onset_marker = onset;
            m_peak_marker = peak;
            m_end_marker = end;
            m_baseline_marker = baseline;
        }
        
        precompute_fit_plot(idx);
        build_triage_queue();
        save_active_roi_svg();
}

void BolusApp::save_active_roi_svg() {
        if (m_selected_roi_idx < 0 || m_selected_roi_idx >= static_cast<int>(m_records.size())) return;
        const auto& rec = m_records[m_selected_roi_idx];
        const auto& c = m_cache[m_selected_roi_idx];
        
        FitRecord frec;
        frec.roi_id = rec.roi_id;
        frec.subj_num = rec.subj_num;
        frec.exp = rec.exp;
        frec.init_amp = rec.init_amp;
        frec.init_t2p = rec.init_t2p;
        frec.init_fwhm = rec.init_fwhm;
        frec.init_m = rec.init_m;
        frec.init_snr = rec.init_snr;
        frec.init_cnr = rec.init_cnr;
        frec.click_start = rec.click_start;
        frec.click_onset = rec.click_onset;
        frec.click_peak = rec.click_peak;
        frec.click_end = rec.click_end;
        frec.f_amp = rec.f_amp;
        frec.f_t2p = rec.f_t2p;
        frec.f_fwhm = rec.f_fwhm;
        frec.f_m = rec.f_m;
        frec.f_snr = rec.f_snr;
        frec.f_cnr = rec.f_cnr;
        frec.denoise_rms = rec.denoise_rms;
        frec.auc = rec.auc;
        frec.aucn = rec.aucn;
        frec.ttlb = rec.ttlb;
        frec.ttm = rec.ttm;
        frec.tthb = rec.tthb;
        frec.ont = rec.ont;
        frec.ont_sc = rec.ont_sc;
        frec.roi_size = rec.roi_size;
        frec.ves_type = rec.ves_type;
        frec.qc_flag = rec.qc_flag;
        frec.fit_source = rec.fit_source;
        
        bool fit_success = !std::isnan(frec.f_amp) && !std::isnan(frec.f_t2p) && !std::isnan(frec.f_fwhm) && !std::isnan(frec.f_m);
        BolusVisualizer::save_svg_plot(frec.roi_id, m_tiff_path, c.t_raw, c.y_raw, c.y_denoised, c.t_us, c.y_us, frec, fit_success, c.drift_slope);
    }

    /**
     * @brief Render the graphical panels.
     */
void BolusApp::draw_gui() {
        ImGui::PushFont(m_font_regular);
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("MainPanel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        
        // Top Toolbar
        draw_top_bar();
        
        // Main Panels (Left: Sidebar, Right: Plot & Parameter Details)
        ImGui::Separator();
        
        float sidebar_w = 380.0f;
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
        ImGui::PopFont();
    }

void BolusApp::draw_top_bar() {
    ImGui::PushFont(m_font_bold);
    ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.title_app.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 930.0f);
    
    if (m_lang == LANG_EN) {
        if (ImGui::Button("FR (Québec)", ImVec2(105, 24))) {
            m_lang = LANG_FR;
            update_locale();
        }
    } else {
        if (ImGui::Button("EN (Canada)", ImVec2(105, 24))) {
            m_lang = LANG_EN;
            update_locale();
        }
    }
    ImGui::SameLine();
    
    if (ImGui::Button(m_tr.text_load_subject_data.c_str(), ImVec2(145, 24))) {
        m_browser.open = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(m_tr.btn_clear_data.c_str(), ImVec2(145, 24))) {
        clear_subject_data();
    }
    ImGui::SameLine();
    if (ImGui::Button(m_tr.btn_save_state.c_str(), ImVec2(100, 24))) {
        if (!m_csv_path.empty()) {
            // Sync current workspace parameters to the GUI state array before saving
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
            save_gui_state();
            ImGui::OpenPopup(m_tr.modal_save_state_success.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(m_tr.btn_load_state.c_str(), ImVec2(100, 24))) {
        if (!m_csv_path.empty()) {
            load_gui_state();
            if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records.size())) {
                int target_idx = m_selected_roi_idx;
                m_selected_roi_idx = -1; // Bypass saving current modified state
                select_record(target_idx);
            }
            ImGui::OpenPopup(m_tr.modal_load_state_success.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(m_tr.btn_reset_all.c_str(), ImVec2(100, 24))) {
        if (!m_csv_path.empty()) {
            ImGui::OpenPopup(m_tr.modal_reset_title.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(m_tr.btn_save_csv.c_str(), ImVec2(120, 24))) {
        if (!m_csv_path.empty()) {
            save_results_csv(m_csv_path, m_records);
            save_gui_state();
            std::string audio_path = get_resource_path("resources/hallelujah.mp3");
            play_sound_cross_platform(audio_path);
            ImGui::OpenPopup(m_tr.modal_save_success.c_str());
        }
    }
        
        if (ImGui::BeginPopupModal(m_tr.modal_save_success.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(m_tr.text_save_csv_msg.c_str(), m_csv_path.c_str());
            ImGui::Separator();
            if (ImGui::Button(m_tr.btn_ok.c_str(), ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal(m_tr.modal_save_state_success.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(m_tr.text_save_state_msg.c_str(), m_csv_path.c_str());
            ImGui::Separator();
            if (ImGui::Button(m_tr.btn_ok.c_str(), ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal(m_tr.modal_load_state_success.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(m_tr.text_load_state_msg.c_str(), m_csv_path.c_str());
            ImGui::Separator();
            if (ImGui::Button(m_tr.btn_ok.c_str(), ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal(m_tr.modal_reset_title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", m_tr.modal_reset_desc.c_str());
            ImGui::Separator();
            if (ImGui::Button(m_tr.btn_reset_confirm.c_str(), ImVec2(120, 0))) {
                int active_idx = m_selected_roi_idx;
                m_selected_roi_idx = -1; // Bypass saving current modified state
                m_records = m_records_backup;
                m_gui_roi_states = m_gui_roi_states_backup;
                for (size_t i = 0; i < m_gui_roi_states.size(); ++i) {
                    precompute_fit_plot(i);
                }
                if (active_idx >= 0 && active_idx < static_cast<int>(m_records.size())) {
                    select_record(active_idx);
                } else {
                    m_selected_roi_idx = -1;
                }
                build_triage_queue();
                save_gui_state();
                save_active_roi_svg();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(m_tr.btn_cancel.c_str(), ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

void BolusApp::draw_sidebar() {
    ImGui::PushFont(m_font_bold);
    ImGui::Text("%s", m_tr.sidebar_title.c_str());
    ImGui::PopFont();
    ImGui::Separator();
    
    const char* items[] = {
        m_tr.filter_all.c_str(),
        m_tr.filter_flagged.c_str(),
        m_tr.filter_fail.c_str(),
        m_tr.filter_warn.c_str(),
        m_tr.filter_pass.c_str(),
        m_tr.filter_review.c_str()
    };
    ImGui::Text("%s", m_tr.label_filter.c_str());
    ImGui::SameLine();
    ImGui::PushItemWidth(-10.0f);
    if (ImGui::Combo("##QcFilterCombo", &m_qc_filter_type, items, IM_ARRAYSIZE(items))) {
        build_triage_queue();
        if (!m_triage_queue.empty()) {
            bool current_still_valid = false;
            for (int idx : m_triage_queue) {
                if (idx == m_selected_roi_idx) {
                    current_still_valid = true;
                    break;
                }
            }
            if (!current_still_valid) {
                select_record(m_triage_queue[0]);
            }
        }
    }
    ImGui::PopItemWidth();
    
    int manual_count = 0;
    for (const auto& r : m_records) {
        if (r.fit_source != "auto") manual_count++;
    }
    if (m_lang == LANG_FR) {
        ImGui::Text("ROI : %d  |  Actives : %d  |  Manuelles : %d", (int)m_records.size(), (int)m_triage_queue.size(), manual_count);
    } else {
        ImGui::Text("ROIs: %d  |  Active: %d  |  Manual: %d", (int)m_records.size(), (int)m_triage_queue.size(), manual_count);
    }
    ImGui::Separator();
    
    ImGui::BeginChild("ListScrollPane", ImVec2(0, 0), false);
    if (ImGui::BeginTable("SidebarListTable", 3, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("ROI", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
        
        for (int q = 0; q < static_cast<int>(m_triage_queue.size()); ++q) {
            int idx = m_triage_queue[q];
            const auto& rec = m_records[idx];
            
            char label[64];
            if (rec.fit_source != "auto") {
                snprintf(label, sizeof(label), "ROI %d *", rec.roi_id);
            } else {
                snprintf(label, sizeof(label), "ROI %d", rec.roi_id);
            }
            
            bool is_selected = (m_selected_roi_idx == idx);
            
            // Format state color tag
            ImVec4 status_color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            if (rec.qc_flag == "PASS") status_color = ImVec4(0.55f, 0.62f, 0.45f, 1.0f);
            else if (rec.qc_flag == "WARN") status_color = ImVec4(0.92f, 0.72f, 0.30f, 1.0f);
            else if (rec.qc_flag == "FAIL") status_color = ImVec4(0.80f, 0.32f, 0.22f, 1.0f);
            else if (rec.qc_flag == "REVIEW") status_color = ImVec4(0.37f, 0.54f, 0.54f, 1.0f);
            
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            
            ImGui::PushStyleColor(ImGuiCol_Text, status_color);
            if (ImGui::Selectable(label, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                select_record(idx);
            }
            ImGui::PopStyleColor();
            
            ImGui::TableNextColumn();
            std::string disp_flag;
            if (rec.qc_flag == "PASS") disp_flag = m_tr.qc_pass;
            else if (rec.qc_flag == "WARN") disp_flag = m_tr.qc_warn;
            else if (rec.qc_flag == "FAIL") disp_flag = m_tr.qc_fail;
            else if (rec.qc_flag == "REVIEW") disp_flag = m_tr.qc_review;
            else disp_flag = rec.qc_flag;
            
            ImGui::TextColored(status_color, "[%s]", disp_flag.c_str());
            
            ImGui::TableNextColumn();
            std::string disp_source;
            if (rec.fit_source == "auto") disp_source = m_tr.source_auto;
            else if (rec.fit_source == "manual") disp_source = m_tr.source_manual;
            else if (rec.fit_source == "override") disp_source = m_tr.source_override;
            else disp_source = rec.fit_source;
            
            if (rec.fit_source != "auto") {
                // Highlight manually updated fits in terracotta
                ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", disp_source.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", disp_source.c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void BolusApp::draw_main_area() {
    if (m_selected_roi_idx < 0) {
        ImGui::Text("%s", m_tr.text_no_data.c_str());
        
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
    
    // Render plot header in a beautiful rounded pane
    std::string disp_flag;
    if (rec.qc_flag == "PASS") disp_flag = m_tr.qc_pass;
    else if (rec.qc_flag == "WARN") disp_flag = m_tr.qc_warn;
    else if (rec.qc_flag == "FAIL") disp_flag = m_tr.qc_fail;
    else if (rec.qc_flag == "REVIEW") disp_flag = m_tr.qc_review;
    else disp_flag = rec.qc_flag;

    std::string disp_source;
    if (rec.fit_source == "auto") disp_source = m_tr.source_auto;
    else if (rec.fit_source == "manual") disp_source = m_tr.source_manual;
    else if (rec.fit_source == "override") disp_source = m_tr.source_override;
    else disp_source = rec.fit_source;
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
    ImGui::BeginChild("PlotHeaderPane", ImVec2(0, 48), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    
    float avail_w = ImGui::GetContentRegionAvail().x;
    float nav_btn_w = m_lang == LANG_FR ? 100.0f : 90.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    char queue_text[64];
    snprintf(queue_text, sizeof(queue_text), "%d / %d", m_queue_pos + 1, (int)m_triage_queue.size());
    float text_w = ImGui::CalcTextSize(queue_text).x;
    float total_buttons_w = nav_btn_w * 2.0f + text_w + spacing * 2.0f;
    
    if (ImGui::BeginTable("PlotHeaderTable", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("HeaderText", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("HeaderNav", ImGuiTableColumnFlags_WidthFixed, total_buttons_w + 10.0f);
        
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        ImGui::Indent(4.0f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 10.0f);
        ImGui::Text(m_tr.text_plot_status_header.c_str(), rec.roi_id, rec.roi_size, disp_flag.c_str(), disp_source.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Unindent(4.0f);
        
        ImGui::TableNextColumn();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
        if (ImGui::Button(m_lang == LANG_FR ? "< Précédent" : "< Previous", ImVec2(nav_btn_w, 22))) {
            if (m_queue_pos > 0) {
                select_record(m_triage_queue[m_queue_pos - 1]);
            }
        }
        ImGui::SameLine();
        ImGui::Text("%s", queue_text);
        ImGui::SameLine();
        if (ImGui::Button(m_lang == LANG_FR ? "Suivant >" : "Next >", ImVec2(nav_btn_w, 22))) {
            if (m_queue_pos >= 0 && m_queue_pos + 1 < static_cast<int>(m_triage_queue.size())) {
                select_record(m_triage_queue[m_queue_pos + 1]);
            }
        }
        ImGui::PopStyleVar();
        
        ImGui::EndTable();
    }
    ImGui::EndChild();
        
        // Calculate Y limits with a 10% buffer based on visible traces in the crop range
        double visible_y_min = std::numeric_limits<double>::max();
        double visible_y_max = -std::numeric_limits<double>::max();
        
        for (size_t i = 0; i < c.t_raw.size(); ++i) {
            double t = c.t_raw[i];
            if (t >= m_crop_min && t <= m_crop_max) {
                if (c.y_raw_detrended[i] < visible_y_min) visible_y_min = c.y_raw_detrended[i];
                if (c.y_raw_detrended[i] > visible_y_max) visible_y_max = c.y_raw_detrended[i];
                
                if (c.y_denoised[i] < visible_y_min) visible_y_min = c.y_denoised[i];
                if (c.y_denoised[i] > visible_y_max) visible_y_max = c.y_denoised[i];
            }
        }
        
        if (!c.y_fit_plot.empty()) {
            for (size_t i = 0; i < c.t_fit_plot.size(); ++i) {
                double t = c.t_fit_plot[i];
                if (t >= m_crop_min && t <= m_crop_max) {
                    if (c.y_fit_plot[i] < visible_y_min) visible_y_min = c.y_fit_plot[i];
                    if (c.y_fit_plot[i] > visible_y_max) visible_y_max = c.y_fit_plot[i];
                }
            }
        }
        if (rec.fit_source != "auto" && !c.y_fit_auto_plot.empty()) {
            for (size_t i = 0; i < c.t_fit_auto_plot.size(); ++i) {
                double t = c.t_fit_auto_plot[i];
                if (t >= m_crop_min && t <= m_crop_max) {
                    if (c.y_fit_auto_plot[i] < visible_y_min) visible_y_min = c.y_fit_auto_plot[i];
                    if (c.y_fit_auto_plot[i] > visible_y_max) visible_y_max = c.y_fit_auto_plot[i];
                }
            }
        }
        
        if (visible_y_min > visible_y_max) {
            visible_y_min = 0.0;
            visible_y_max = 100.0;
        }
        
        double y_range = visible_y_max - visible_y_min;
        if (y_range <= 0.0) y_range = 1.0;
        
        double y_limit_min = visible_y_min - 0.15 * y_range;
        double y_limit_max = visible_y_max + 0.15 * y_range;
        
        // Sanitization to prevent crashes in ImPlot assertions (e.g. on NaNs, Inf, or empty ranges)
        if (std::isnan(m_crop_min) || std::isinf(m_crop_min)) m_crop_min = 0.0;
        if (std::isnan(m_crop_max) || std::isinf(m_crop_max)) m_crop_max = c.t_raw.empty() ? 120.0 : c.t_raw.back();
        if (std::isnan(m_crop_max) || std::isinf(m_crop_max)) m_crop_max = 120.0;
        if (m_crop_min >= m_crop_max) m_crop_max = m_crop_min + 1.0;

        if (std::isnan(y_limit_min) || std::isinf(y_limit_min)) y_limit_min = 0.0;
        if (std::isnan(y_limit_max) || std::isinf(y_limit_max)) y_limit_max = 100.0;
        if (y_limit_min >= y_limit_max) y_limit_max = y_limit_min + 1.0;

        // Draggable markers sanitization
        if (std::isnan(m_onset_marker) || std::isinf(m_onset_marker)) m_onset_marker = m_crop_min + 0.35 * (m_crop_max - m_crop_min);
        if (std::isnan(m_peak_marker) || std::isinf(m_peak_marker)) m_peak_marker = m_onset_marker + 4.0;
        if (std::isnan(m_end_marker) || std::isinf(m_end_marker)) m_end_marker = m_peak_marker + 6.0;
        if (std::isnan(m_baseline_marker) || std::isinf(m_baseline_marker)) m_baseline_marker = (y_limit_min + y_limit_max) / 2.0;

        // Ensure order constraints even on NaNs/Infs
        m_onset_marker = std::clamp(m_onset_marker, m_crop_min, m_crop_max);
        m_peak_marker = std::clamp(m_peak_marker, m_onset_marker + 0.01, m_crop_max);
        m_end_marker = std::clamp(m_end_marker, m_peak_marker + 0.01, m_crop_max);

        // Draggable Baseline visual crop limits setup
        ImPlot::SetNextAxesLimits(m_crop_min, m_crop_max, y_limit_min, y_limit_max, ImGuiCond_Always);
        
        ImVec2 plot_pos(0.0f, 0.0f);
        ImVec2 plot_size(0.0f, 0.0f);
        
        // Calculate dynamic plot height based on available window height to fit everything else
        float avail_h = ImGui::GetContentRegionAvail().y;
        float plot_h = avail_h - 460.0f; // Reserved for ParamsPane and RangeSlider/Header
        if (plot_h < 250.0f) plot_h = 250.0f;
        if (plot_h > 650.0f) plot_h = 650.0f;

        if (ImPlot::BeginPlot(m_tr.plot_title.c_str(), ImVec2(-1, plot_h))) {
            ImPlot::SetupAxes(m_tr.plot_x_axis.c_str(), m_tr.plot_y_axis.c_str());
            
            // Limit the current axis limits to show cropped visual window on the fly
            ImPlot::SetupAxisLimits(ImAxis_X1, m_crop_min, m_crop_max, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, y_limit_min, y_limit_max, ImGuiCond_Always);

            // Query plot area screen coordinates and dimensions after setup to avoid locking setup early
            plot_pos = ImPlot::GetPlotPos();
            plot_size = ImPlot::GetPlotSize();
            
            // Draw visual curves
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.85f, 0.78f, 0.62f, 0.70f)); // Warm gold/brass
            ImPlot::PlotLine(m_tr.plot_raw.c_str(), c.t_raw.data(), c.y_raw_detrended.data(), c.t_raw.size());
            ImPlot::PopStyleColor();
            
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.37f, 0.64f, 0.64f, 1.0f)); // Muted Sage/Teal
            ImPlot::PlotLine(m_tr.plot_denoised.c_str(), c.t_raw.data(), c.y_denoised.data(), c.t_raw.size());
            ImPlot::PopStyleColor();
            
            if (!c.y_fit_plot.empty()) {
                if (rec.fit_source != "auto" && !c.y_fit_auto_plot.empty()) {
                    ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.27f, 0.51f, 0.71f, 0.5f)); // Steel blue, semi-transparent
                    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
                    ImPlot::PlotLine(m_tr.label_auto_fit.c_str(), c.t_fit_auto_plot.data(), c.y_fit_auto_plot.data(), c.t_fit_auto_plot.size());
                    ImPlot::PopStyleVar();
                    ImPlot::PopStyleColor();
                }
                
                ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.88f, 0.45f, 0.18f, 1.0f)); // Terracotta orange
                ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.5f);
                ImPlot::PlotLine(m_tr.plot_fit.c_str(), c.t_fit_plot.data(), c.y_fit_plot.data(), c.t_fit_plot.size());
                ImPlot::PopStyleVar();
                ImPlot::PopStyleColor();
            }
            ImPlot::PopStyleVar();
            
            // Draggable Lines
            ImPlot::DragLineX(101, &m_onset_marker, ImVec4(0.55f, 0.62f, 0.45f, 1.0f), 2.0f); // Green Onset
            ImPlot::DragLineX(102, &m_peak_marker, ImVec4(0.92f, 0.72f, 0.30f, 1.0f), 2.0f);  // Yellow Peak
            ImPlot::DragLineX(103, &m_end_marker, ImVec4(0.80f, 0.32f, 0.22f, 1.0f), 2.0f);   // Red End
            
            ImPlot::DragLineY(104, &m_baseline_marker, ImVec4(0.68f, 0.48f, 0.68f, 1.0f), 2.0f); // Purple Baseline
            
            // Enforce ordering and bounds constraints on markers immediately
            double max_t = c.t_raw.empty() ? 120.0 : c.t_raw.back();
            if (std::isnan(max_t) || std::isinf(max_t)) max_t = 120.0;
            const double min_gap = 0.1; // 100 ms minimum gap
            m_onset_marker = std::clamp(m_onset_marker, 0.0, max_t - 2.0 * min_gap);
            m_peak_marker = std::clamp(m_peak_marker, m_onset_marker + min_gap, max_t - min_gap);
            m_end_marker = std::clamp(m_end_marker, m_peak_marker + min_gap, max_t);
            
            // Annotate Draggable Lines with formatted numeric values
            ImPlot::TagX(m_onset_marker, ImVec4(0.55f, 0.62f, 0.45f, 1.0f), m_tr.tag_onset.c_str(), m_onset_marker);
            ImPlot::TagX(m_peak_marker, ImVec4(0.92f, 0.72f, 0.30f, 1.0f), m_tr.tag_peak.c_str(), m_peak_marker);
            ImPlot::TagX(m_end_marker, ImVec4(0.80f, 0.32f, 0.22f, 1.0f), m_tr.tag_end.c_str(), m_end_marker);
            ImPlot::TagY(m_baseline_marker, ImVec4(0.68f, 0.48f, 0.68f, 1.0f), m_tr.tag_base.c_str(), m_baseline_marker);
            
            ImPlot::EndPlot();
        }
        
        // Render unified RangeSlider right below the plot, aligned exactly with the plot width
        {
            if (plot_size.x > 0.0f) {
                ImVec2 cursor_pos = ImGui::GetCursorPos();
                ImGui::SetCursorPosX(plot_pos.x - ImGui::GetWindowPos().x);
                double limit_max = c.t_raw.empty() ? 120.0 : c.t_raw.back();
                RangeSlider("PlotCropSlider", &m_crop_min, &m_crop_max, 0.0, limit_max, ImVec2(plot_size.x, 24.0f));
                ImGui::SetCursorPosX(cursor_pos.x);
            } else {
                float avail_w = ImGui::GetContentRegionAvail().x;
                double limit_max = c.t_raw.empty() ? 120.0 : c.t_raw.back();
                RangeSlider("PlotCropSlider", &m_crop_min, &m_crop_max, 0.0, limit_max, ImVec2(avail_w, 24.0f));
            }
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        
        // Parameters & Controls Panel
        ImGui::BeginChild("ParamsPane", ImVec2(0, 0), true);
        
        // Manual fitting and cropping
        ImGui::Columns(3, "ControlsGrid", false);
        ImGui::SetColumnWidth(0, 450.0f);
        ImGui::SetColumnWidth(1, 380.0f);
        // Column 2 takes the remainder
        
        ImGui::PushFont(m_font_bold);
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.section_markers.c_str());
        ImGui::PopFont();
        double min_t = 0.0;
        double max_t = c.t_raw.back();
        double base_min = 0.0;
        double base_max = 1000.0;
        
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.55f, 0.62f, 0.45f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.65f, 0.72f, 0.55f, 1.0f));
        ImGui::SliderScalar(m_tr.label_onset.c_str(), ImGuiDataType_Double, &m_onset_marker, &min_t, &max_t, "%.1f");
        ImGui::PopStyleColor(2);

        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.92f, 0.72f, 0.30f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.00f, 0.82f, 0.40f, 1.0f));
        ImGui::SliderScalar(m_tr.label_peak.c_str(), ImGuiDataType_Double, &m_peak_marker, &m_onset_marker, &max_t, "%.1f");
        ImGui::PopStyleColor(2);

        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.80f, 0.32f, 0.22f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.90f, 0.42f, 0.32f, 1.0f));
        ImGui::SliderScalar(m_tr.label_end.c_str(), ImGuiDataType_Double, &m_end_marker, &m_peak_marker, &max_t, "%.1f");
        ImGui::PopStyleColor(2);

        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.68f, 0.48f, 0.68f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.78f, 0.58f, 0.78f, 1.0f));
        ImGui::SliderScalar(m_tr.label_baseline.c_str(), ImGuiDataType_Double, &m_baseline_marker, &base_min, &base_max, "%.1f");
        ImGui::PopStyleColor(2);
        
        ImGui::NextColumn();
        
        ImGui::PushFont(m_font_bold);
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.section_crop.c_str());
        ImGui::PopFont();
        ImGui::Text("%s", m_tr.text_slider_desc.c_str());
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (ImGui::Button(m_tr.btn_reset_crop.c_str(), ImVec2(150, 26))) {
            m_crop_min = 0.0;
            m_crop_max = c.t_raw.back();
        }
        ImGui::SameLine();
        if (ImGui::Button(m_tr.btn_crop_bounds.c_str(), ImVec2(150, 26))) {
            m_crop_min = std::max(0.0, m_onset_marker - 5.0);
            m_crop_max = std::min(c.t_raw.back(), m_end_marker + 10.0);
        }
        
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::PushFont(m_font_bold);
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.section_denoise.c_str());
        ImGui::PopFont();
        
        ImGui::PushItemWidth(180.0f);
        if (ImGui::SliderFloat(m_tr.label_denoise_strength.c_str(), &m_denoise_strength_factor, 0.5f, 3.0f, "%.2fx")) {
            if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records.size())) {
                precompute_single_trace(m_selected_roi_idx);
                select_record(m_selected_roi_idx);
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            precompute_all_traces();
        }
        ImGui::PopItemWidth();
        
        ImGui::NextColumn();
        
        ImGui::PushFont(m_font_bold);
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.section_actions.c_str());
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        
        float btn_w = ImGui::GetContentRegionAvail().x - 8.0f;
        if (ImGui::Button(m_tr.btn_refit.c_str(), ImVec2(btn_w, 26))) {
            run_fit_on_current_roi();
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (ImGui::Button(m_tr.btn_override.c_str(), ImVec2(btn_w, 26))) {
            m_records[m_selected_roi_idx].qc_flag = "PASS";
            m_records[m_selected_roi_idx].fit_source = "override";
            if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_gui_roi_states.size())) {
                m_gui_roi_states[m_selected_roi_idx].qc_flag = "PASS";
                m_gui_roi_states[m_selected_roi_idx].fit_source = "override";
            }
            build_triage_queue();
            save_active_roi_svg();
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (ImGui::Button(m_tr.btn_revert.c_str(), ImVec2(btn_w, 26))) {
            if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records_backup.size())) {
                int idx = m_selected_roi_idx;
                m_records[idx] = m_records_backup[idx];
                m_gui_roi_states[idx] = m_gui_roi_states_backup[idx];
                m_selected_roi_idx = -1; // Bypass saving current modified state
                select_record(idx);
                precompute_fit_plot(idx);
                build_triage_queue();
                save_active_roi_svg();
            }
        }
        
        ImGui::Columns(1);
        ImGui::Separator();
        
        // Active fit parameters table comparison
        ImGui::PushFont(m_font_bold);
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.section_params.c_str());
        ImGui::PopFont();
        if (ImGui::BeginTable("ParamsTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn(m_tr.col_variable.c_str());
            ImGui::TableSetupColumn(m_tr.col_amplitude.c_str());
            ImGui::TableSetupColumn(m_tr.col_t2p.c_str());
            ImGui::TableSetupColumn(m_tr.col_fwhm.c_str());
            ImGui::TableSetupColumn(m_tr.col_baseline.c_str());
            ImGui::TableSetupColumn(m_tr.col_cnr.c_str());
            ImGui::TableSetupColumn(m_tr.col_onset.c_str());
            ImGui::TableHeadersRow();
            
            auto display_val = [](double val) {
                if (std::isnan(val)) ImGui::Text("N/A");
                else ImGui::Text("%.4f", val);
            };
            
            // Fitted row
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", m_tr.label_fitted.c_str());
            ImGui::TableNextColumn(); display_val(rec.f_amp);
            ImGui::TableNextColumn(); display_val(rec.f_t2p);
            ImGui::TableNextColumn(); display_val(rec.f_fwhm);
            ImGui::TableNextColumn(); display_val(rec.f_m);
            ImGui::TableNextColumn(); display_val(rec.f_cnr);
            ImGui::TableNextColumn(); display_val(rec.ont);
            
            // Pre-guess row
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", m_tr.label_estimated_init.c_str());
            ImGui::TableNextColumn(); display_val(rec.init_amp);
            ImGui::TableNextColumn(); display_val(rec.init_t2p);
            ImGui::TableNextColumn(); display_val(rec.init_fwhm);
            ImGui::TableNextColumn(); display_val(rec.init_m);
            ImGui::TableNextColumn(); display_val(rec.init_cnr);
            ImGui::TableNextColumn(); display_val(rec.click_onset);
            
            ImGui::EndTable();
        }
        
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::PushFont(m_font_bold);
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), m_lang == LANG_FR ? "CINÉTIQUE DU BOLUS ET CLASSIFICATION" : "BOLUS KINETICS & CLASSIFICATION");
        ImGui::PopFont();
        if (ImGui::BeginTable("KineticsTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("AUC");
            ImGui::TableSetupColumn("AUCn");
            ImGui::TableSetupColumn(m_lang == LANG_FR ? "Début scan (s)" : "Onset Scan (s)");
            ImGui::TableSetupColumn(m_lang == LANG_FR ? "TT Inf (s)" : "TT Lower (s)");
            ImGui::TableSetupColumn(m_lang == LANG_FR ? "TT Pic (s)" : "TT Peak (s)");
            ImGui::TableSetupColumn(m_lang == LANG_FR ? "TT Sup (s)" : "TT Upper (s)");
            ImGui::TableSetupColumn(m_lang == LANG_FR ? "Type vaisseau" : "Vessel Type");
            ImGui::TableHeadersRow();
            
            auto display_val = [](double val) {
                if (std::isnan(val)) ImGui::Text("N/A");
                else ImGui::Text("%.4f", val);
            };
            
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); display_val(rec.auc);
            ImGui::TableNextColumn(); display_val(rec.aucn);
            ImGui::TableNextColumn(); display_val(rec.ont_sc);
            ImGui::TableNextColumn(); display_val(rec.ttlb);
            ImGui::TableNextColumn(); display_val(rec.ttm);
            ImGui::TableNextColumn(); display_val(rec.tthb);
            
            ImGui::TableNextColumn();
            ImGui::Text("%s", rec.ves_type.c_str());
            
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

void BolusApp::clear_subject_data() {
    m_csv_path.clear();
    m_tiff_path.clear();
    m_rois_path.clear();
    m_meta_path.clear();
    m_records.clear();
    m_records_backup.clear();
    m_cache.clear();
    m_gui_roi_states.clear();
    m_gui_roi_states_backup.clear();
    m_selected_roi_idx = -1;
    m_triage_queue.clear();
    m_queue_pos = -1;
    m_rois.clear();
    m_tiff = TiffData();
    m_denoise_strength_factor = 1.0f;
}

// ============================================================================
// Application Entry Point
// ============================================================================

int main(int argc, char** argv) {
    TIFFSetWarningHandler(nullptr);
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
