/**
 * @file bolus_server.cpp
 * @brief JSON IPC Server for the Electron GUI.
 *
 * Reads JSON commands from stdin (one per line), executes them using
 * the existing C++ pipeline code, and writes JSON responses to stdout.
 * This binary is spawned as a child process by the Electron main process.
 *
 * Protocol:
 *   Request:  {"id": 1, "action": "load_tiff", "params": {"path": "/data/bolus1.tif"}}
 *   Response: {"id": 1, "ok": true, "data": {...}}
 *   Error:    {"id": 1, "ok": false, "error": "message"}
 */

#include "bolus_tracking_cpp.hpp"
#include "mat_parser.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <iomanip>
#include <unordered_map>
#include <map>

#include <nlohmann/json.hpp>
#include <tiffio.h>

using json = nlohmann::json;

// ============================================================================
// GUI Data Structures (mirrored from bolus_gui.hpp for standalone use)
// ============================================================================

struct TiffData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::vector<float>> frames;
    std::vector<float> mip;
};

struct CsvRecord {
    int roi_id = 0;
    int subj_num = 0;
    std::string exp = "";
    double init_amp = 0.0, init_t2p = 0.0, init_fwhm = 0.0, init_m = 0.0;
    double init_snr = 0.0, init_cnr = 0.0;
    double click_start = 0.0, click_onset = 0.0, click_peak = 0.0, click_end = 0.0;
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
    double raw_sd_base = 0.0;
    int stall_flag = 0;
    std::string ves_type = "U";
    std::string qc_flag = "FAIL";
    std::string fit_source = "auto";
};

// ============================================================================
// TIFF Loading (reused from bolus_gui.cpp)
// ============================================================================

static TiffData load_tiff(const std::string& path) {
    TIFFSetWarningHandler(nullptr);
    TiffData data;
    TIFF* tif = TIFFOpen(path.c_str(), "r");
    if (!tif) return data;

    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &data.width);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &data.height);

    tsize_t scanline_size = TIFFScanlineSize(tif);
    tdata_t buf = _TIFFmalloc(scanline_size);
    if (!buf) { TIFFClose(tif); return data; }

    do {
        std::vector<float> frame(data.width * data.height);
        uint16_t bitspersample = 8;
        uint16_t sampleformat = 1;
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitspersample);
        TIFFGetField(tif, TIFFTAG_SAMPLEFORMAT, &sampleformat);

        tsize_t current_scanline_size = TIFFScanlineSize(tif);
        if (current_scanline_size > scanline_size) {
            _TIFFfree(buf);
            scanline_size = current_scanline_size;
            buf = _TIFFmalloc(scanline_size);
            if (!buf) { TIFFClose(tif); return data; }
        }

        if (bitspersample == 16) {
            for (uint32_t row = 0; row < data.height; row++) {
                TIFFReadScanline(tif, buf, row);
                uint16_t* row_ptr = reinterpret_cast<uint16_t*>(buf);
                for (uint32_t col = 0; col < data.width; col++) {
                    frame[row * data.width + col] = static_cast<float>(row_ptr[col]);
                }
            }
        } else if (bitspersample == 8) {
            for (uint32_t row = 0; row < data.height; row++) {
                TIFFReadScanline(tif, buf, row);
                uint8_t* row_ptr = reinterpret_cast<uint8_t*>(buf);
                for (uint32_t col = 0; col < data.width; col++) {
                    frame[row * data.width + col] = static_cast<float>(row_ptr[col]);
                }
            }
        } else if (bitspersample == 32 && sampleformat == 3) {
            for (uint32_t row = 0; row < data.height; row++) {
                TIFFReadScanline(tif, buf, row);
                float* row_ptr = reinterpret_cast<float*>(buf);
                for (uint32_t col = 0; col < data.width; col++) {
                    frame[row * data.width + col] = row_ptr[col];
                }
            }
        }
        data.frames.push_back(frame);
    } while (TIFFReadDirectory(tif));

    _TIFFfree(buf);
    TIFFClose(tif);

    if (!data.frames.empty()) {
        size_t num_pixels = data.width * data.height;
        data.mip.assign(num_pixels, 0.0f);
        for (const auto& frame : data.frames) {
            for (size_t i = 0; i < num_pixels; ++i) {
                data.mip[i] += frame[i];
            }
        }
        for (size_t i = 0; i < num_pixels; ++i) {
            data.mip[i] /= data.frames.size();
        }
    }
    return data;
}

// ============================================================================
// ROI Loading
// ============================================================================

static std::vector<ROI> load_rois_txt(const std::string& path) {
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

// ============================================================================
// CSV Reading/Writing
// ============================================================================

static std::vector<CsvRecord> read_results_csv(const std::string& path) {
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

    int idx_roi = get_col_idx("ROI"), idx_subj = get_col_idx("SubjNum"), idx_exp = get_col_idx("Exp");
    int idx_init_amp = get_col_idx("InitAmp"), idx_init_t2p = get_col_idx("InitT2p");
    int idx_init_fwhm = get_col_idx("InitFWHM"), idx_init_m = get_col_idx("InitM");
    int idx_init_snr = get_col_idx("InitSNR"), idx_init_cnr = get_col_idx("InitCNR");
    int idx_start = get_col_idx("Click1_Start_T"), idx_onset = get_col_idx("Click2_Onset_T");
    int idx_peak = get_col_idx("Click3_Peak_T"), idx_end = get_col_idx("Click4_End_T");
    int idx_f_amp = get_col_idx("F_Amp"), idx_f_t2p = get_col_idx("F_T2p");
    int idx_f_fwhm = get_col_idx("F_FWHM"), idx_f_m = get_col_idx("F_M");
    int idx_f_snr = get_col_idx("F_SNR"), idx_f_cnr = get_col_idx("F_CNR");
    int idx_auc = get_col_idx("AUC"), idx_aucn = get_col_idx("AUCn");
    int idx_ttlb = get_col_idx("TTlb"), idx_ttm = get_col_idx("TTm"), idx_tthb = get_col_idx("TThb");
    int idx_ont = get_col_idx("OnT"), idx_ont_sc = get_col_idx("OnTSc");
    int idx_roi_size = get_col_idx("ROISize"), idx_denoise = get_col_idx("Denoise_RMS");
    int idx_raw_sd_base = get_col_idx("raw_sd_base"), idx_stall_flag = get_col_idx("Stall_Flag");
    int idx_ves = get_col_idx("VesType"), idx_qc = get_col_idx("QC_Flag"), idx_source = get_col_idx("Fit_Source");

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
        while (cells.size() < headers.size()) cells.push_back("");

        CsvRecord rec;
        auto pd = [&](int idx) -> double {
            if (idx >= 0 && idx < (int)cells.size() && !cells[idx].empty()) {
                try { return std::stod(cells[idx]); } catch (...) {}
            }
            return std::numeric_limits<double>::quiet_NaN();
        };
        auto pi = [&](int idx) -> int {
            if (idx >= 0 && idx < (int)cells.size() && !cells[idx].empty()) {
                try { return std::stoi(cells[idx]); } catch (...) {}
            }
            return 0;
        };
        auto ps = [&](int idx) -> std::string {
            if (idx >= 0 && idx < (int)cells.size()) return cells[idx];
            return "";
        };

        if (idx_roi >= 0) rec.roi_id = pi(idx_roi);
        if (idx_subj >= 0) rec.subj_num = pi(idx_subj);
        if (idx_exp >= 0) rec.exp = ps(idx_exp);
        rec.init_amp = pd(idx_init_amp); rec.init_t2p = pd(idx_init_t2p);
        rec.init_fwhm = pd(idx_init_fwhm); rec.init_m = pd(idx_init_m);
        rec.init_snr = pd(idx_init_snr); rec.init_cnr = pd(idx_init_cnr);
        rec.click_start = pd(idx_start); rec.click_onset = pd(idx_onset);
        rec.click_peak = pd(idx_peak); rec.click_end = pd(idx_end);
        rec.f_amp = pd(idx_f_amp); rec.f_t2p = pd(idx_f_t2p);
        rec.f_fwhm = pd(idx_f_fwhm); rec.f_m = pd(idx_f_m);
        rec.f_snr = pd(idx_f_snr); rec.f_cnr = pd(idx_f_cnr);
        rec.auc = pd(idx_auc); rec.aucn = pd(idx_aucn);
        rec.ttlb = pd(idx_ttlb); rec.ttm = pd(idx_ttm); rec.tthb = pd(idx_tthb);
        rec.ont = pd(idx_ont); rec.ont_sc = pd(idx_ont_sc);
        if (idx_roi_size >= 0) rec.roi_size = pi(idx_roi_size);
        rec.denoise_rms = pd(idx_denoise);
        if (idx_raw_sd_base >= 0) rec.raw_sd_base = pd(idx_raw_sd_base);
        if (idx_stall_flag >= 0) rec.stall_flag = pi(idx_stall_flag);
        if (idx_ves >= 0) rec.ves_type = ps(idx_ves);
        if (idx_qc >= 0) rec.qc_flag = ps(idx_qc);
        if (idx_source >= 0) rec.fit_source = ps(idx_source);
        records.push_back(rec);
    }
    return records;
}

static void save_results_csv(const std::string& path, const std::vector<CsvRecord>& records) {
    std::ofstream out(path);
    if (!out.is_open()) return;
    out << "ROI,SubjNum,Exp,InitAmp,InitT2p,InitFWHM,InitM,InitSNR,InitCNR,"
           "Click1_Start_T,Click2_Onset_T,Click3_Peak_T,Click4_End_T,"
           "F_Amp,F_T2p,F_FWHM,F_M,F_SNR,F_CNR,AUC,AUCn,TTlb,TTm,TThb,OnT,OnTSc,ROISize,Denoise_RMS,VesType,QC_Flag,Fit_Source,Stall_Flag\n";
    auto fd = [](double v) -> std::string {
        if (std::isnan(v)) return "";
        std::stringstream ss; ss << v; return ss.str();
    };
    for (const auto& r : records) {
        out << r.roi_id << "," << r.subj_num << "," << r.exp << ","
            << fd(r.init_amp) << "," << fd(r.init_t2p) << "," << fd(r.init_fwhm) << "," << fd(r.init_m) << ","
            << fd(r.init_snr) << "," << fd(r.init_cnr) << ","
            << fd(r.click_start) << "," << fd(r.click_onset) << "," << fd(r.click_peak) << "," << fd(r.click_end) << ","
            << fd(r.f_amp) << "," << fd(r.f_t2p) << "," << fd(r.f_fwhm) << "," << fd(r.f_m) << ","
            << fd(r.f_snr) << "," << fd(r.f_cnr) << ","
            << fd(r.auc) << "," << fd(r.aucn) << "," << fd(r.ttlb) << "," << fd(r.ttm) << "," << fd(r.tthb) << ","
            << fd(r.ont) << "," << fd(r.ont_sc) << "," << r.roi_size << "," << fd(r.denoise_rms) << ","
            << r.ves_type << "," << r.qc_flag << "," << r.fit_source << "," << r.stall_flag << "\n";
    }
}

// ============================================================================
// Frame rate parsing
// ============================================================================

static double parse_frame_rate_from_meta(const std::string& filepath) {
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
// Base64 Encoder (for compact binary transfer over JSON IPC)
// ============================================================================

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i+1]) << 8) | data[i+2];
        out += b64_table[(n >> 18) & 0x3F];
        out += b64_table[(n >> 12) & 0x3F];
        out += b64_table[(n >> 6)  & 0x3F];
        out += b64_table[ n        & 0x3F];
    }
    if (i < data.size()) {
        uint32_t n = uint32_t(data[i]) << 16;
        if (i + 1 < data.size()) n |= uint32_t(data[i+1]) << 8;
        out += b64_table[(n >> 18) & 0x3F];
        out += b64_table[(n >> 12) & 0x3F];
        out += (i + 1 < data.size()) ? b64_table[(n >> 6) & 0x3F] : '=';
        out += '=';
    }
    return out;
}

// ============================================================================
// JSON Serialization Helpers
// ============================================================================

static json json_val(double v) {
    if (std::isnan(v) || std::isinf(v)) return nullptr;
    return v;
}

static json record_to_json(const CsvRecord& r) {
    return json{
        {"roi_id", r.roi_id}, {"subj_num", r.subj_num}, {"exp", r.exp},
        {"init_amp", json_val(r.init_amp)}, {"init_t2p", json_val(r.init_t2p)},
        {"init_fwhm", json_val(r.init_fwhm)}, {"init_m", json_val(r.init_m)},
        {"init_snr", json_val(r.init_snr)}, {"init_cnr", json_val(r.init_cnr)},
        {"click_start", json_val(r.click_start)}, {"click_onset", json_val(r.click_onset)},
        {"click_peak", json_val(r.click_peak)}, {"click_end", json_val(r.click_end)},
        {"f_amp", json_val(r.f_amp)}, {"f_t2p", json_val(r.f_t2p)},
        {"f_fwhm", json_val(r.f_fwhm)}, {"f_m", json_val(r.f_m)},
        {"f_snr", json_val(r.f_snr)}, {"f_cnr", json_val(r.f_cnr)},
        {"auc", json_val(r.auc)}, {"aucn", json_val(r.aucn)},
        {"ttlb", json_val(r.ttlb)}, {"ttm", json_val(r.ttm)}, {"tthb", json_val(r.tthb)},
        {"ont", json_val(r.ont)}, {"ont_sc", json_val(r.ont_sc)},
        {"roi_size", r.roi_size}, {"denoise_rms", json_val(r.denoise_rms)},
        {"raw_sd_base", json_val(r.raw_sd_base)}, {"stall_flag", r.stall_flag},
        {"ves_type", r.ves_type}, {"qc_flag", r.qc_flag}, {"fit_source", r.fit_source}
    };
}

static CsvRecord json_to_record(const json& j) {
    CsvRecord r;
    auto gd = [&](const std::string& k) -> double {
        if (j.contains(k) && !j[k].is_null()) return j[k].get<double>();
        return std::numeric_limits<double>::quiet_NaN();
    };
    r.roi_id = j.value("roi_id", 0);
    r.subj_num = j.value("subj_num", 0);
    r.exp = j.value("exp", "");
    r.init_amp = gd("init_amp"); r.init_t2p = gd("init_t2p");
    r.init_fwhm = gd("init_fwhm"); r.init_m = gd("init_m");
    r.init_snr = gd("init_snr"); r.init_cnr = gd("init_cnr");
    r.click_start = gd("click_start"); r.click_onset = gd("click_onset");
    r.click_peak = gd("click_peak"); r.click_end = gd("click_end");
    r.f_amp = gd("f_amp"); r.f_t2p = gd("f_t2p");
    r.f_fwhm = gd("f_fwhm"); r.f_m = gd("f_m");
    r.f_snr = gd("f_snr"); r.f_cnr = gd("f_cnr");
    r.auc = gd("auc"); r.aucn = gd("aucn");
    r.ttlb = gd("ttlb"); r.ttm = gd("ttm"); r.tthb = gd("tthb");
    r.ont = gd("ont"); r.ont_sc = gd("ont_sc");
    r.roi_size = j.value("roi_size", 0);
    r.denoise_rms = gd("denoise_rms");
    r.raw_sd_base = gd("raw_sd_base");
    r.stall_flag = j.value("stall_flag", 0);
    r.ves_type = j.value("ves_type", "U");
    r.qc_flag = j.value("qc_flag", "FAIL");
    r.fit_source = j.value("fit_source", "auto");
    return r;
}

// ============================================================================
// Persistent State (kept across requests within one session)
// ============================================================================

static TiffData g_tiff;
static std::vector<ROI> g_rois;
static std::vector<CsvRecord> g_records;
static std::unordered_map<int, size_t> g_record_map;  // roi_id -> index in g_records
static double g_fr = 1.0;
static int g_upsample_factor = 10;  // Match CLI pipeline default (10)
static double g_drift_win = 15.0;

/// Look up CSV record by ROI ID (not array index). Returns nullptr if not found.
/// The pipeline writes roi_id+1 to CSV (0-based mask → 1-based CSV), so we
/// look up both roi_id and roi_id+1 for compatibility.
static const CsvRecord* find_record_for_roi(int roi_idx) {
    if (roi_idx < 0 || roi_idx >= (int)g_rois.size()) return nullptr;
    int roi_id = g_rois[roi_idx].id;
    // Try exact match first (e.g. if CSV was produced with 1-based mask IDs)
    auto it = g_record_map.find(roi_id);
    if (it != g_record_map.end() && it->second < g_records.size()) {
        return &g_records[it->second];
    }
    // Try roi_id+1 (pipeline adds +1: mask 0 → CSV 1)
    it = g_record_map.find(roi_id + 1);
    if (it != g_record_map.end() && it->second < g_records.size()) {
        return &g_records[it->second];
    }
    return nullptr;
}
static BolusFitter g_fitter;
static QCSettings g_qc_settings;
static StallSettings g_stall_settings;

// Per-ROI cached trace data
struct TraceCache {
    std::vector<double> t_raw, y_raw, y_raw_detrended, y_denoised, t_us, y_us;
    double sd_base = 0.05;
    double raw_sd_base = 0.0;
    double drift_slope = 0.0;
};
static std::vector<TraceCache> g_traces;

// ============================================================================
// Trace Computation (reused logic from bolus_gui.cpp::precompute_single_trace)
// ============================================================================

static void compute_trace(size_t r, float denoise_strength = 1.0f) {
    if (r >= g_rois.size()) return;
    if (g_traces.size() <= r) g_traces.resize(g_rois.size());

    const auto& roi = g_rois[r];
    auto& c = g_traces[r];

    // 1. Rasterize
    std::vector<int> mask = ROIMaskRasterizer::get_mask_pixels(roi.poly, g_tiff.width, g_tiff.height);
    int mask_size = 0;
    for (int v : mask) mask_size += v;

    // 2. Average raw MFI
    c.y_raw.resize(g_tiff.frames.size(), 0.0);
    c.t_raw.resize(g_tiff.frames.size(), 0.0);
    for (size_t f = 0; f < g_tiff.frames.size(); ++f) {
        c.t_raw[f] = f / g_fr;
        if (mask_size > 0) {
            double sum = 0.0;
            for (int idx = 0; idx < (int)(g_tiff.width * g_tiff.height); ++idx) {
                if (mask[idx]) sum += g_tiff.frames[f][idx];
            }
            c.y_raw[f] = sum / mask_size;
        }
    }

    // 3. Drift estimation
    double sum_t = 0, sum_y = 0, sum_tt = 0, sum_ty = 0;
    int count = 0;
    for (size_t i = 0; i < c.t_raw.size(); ++i) {
        if (c.t_raw[i] <= g_drift_win) {
            sum_t += c.t_raw[i]; sum_y += c.y_raw[i];
            sum_tt += c.t_raw[i] * c.t_raw[i];
            sum_ty += c.t_raw[i] * c.y_raw[i];
            count++;
        }
    }
    c.drift_slope = 0.0;
    if (count > 1) {
        double mean_t = sum_t / count, mean_y = sum_y / count;
        double num = sum_ty - count * mean_t * mean_y;
        double den = sum_tt - count * mean_t * mean_t;
        if (std::abs(den) > 1e-9) c.drift_slope = num / den;
    }

    std::vector<double> detrended = c.y_raw;
    for (size_t i = 0; i < detrended.size(); ++i) {
        detrended[i] -= c.drift_slope * c.t_raw[i];
    }
    c.y_raw_detrended = detrended;

    // Adaptive denoise parameters
    int n_base = std::min((int)std::round(2.0 * g_fr), (int)std::round(detrended.size() * 0.1));
    n_base = std::max(2, n_base);
    std::vector<double> raw_base_win(detrended.begin(), detrended.begin() + n_base);
    double raw_baseline = SignalProcessor::compute_median(raw_base_win);
    double sum_rb = 0; for (double v : raw_base_win) sum_rb += v;
    double mean_rb = sum_rb / raw_base_win.size();
    double raw_sd = SignalProcessor::compute_std(raw_base_win, mean_rb);
    c.raw_sd_base = raw_sd;
    double raw_max_val = -1e9;
    for (double v : detrended) if (v > raw_max_val) raw_max_val = v;
    double raw_cnr = (raw_sd > 0) ? ((raw_max_val - raw_baseline) / raw_sd) : 0;

    double denoise_thresh = 2.0;
    int denoise_half_win = 5;
    if (raw_cnr < 4.0) { denoise_thresh = 1.5; denoise_half_win = 7; }
    else if (raw_cnr >= 15.0) { denoise_thresh = 3.0; denoise_half_win = 3; }
    else if (raw_cnr >= 8.0) { denoise_thresh = 2.5; denoise_half_win = 5; }

    if (denoise_strength > 0.01f) {
        denoise_thresh /= (double)denoise_strength;
        denoise_half_win = std::max(1, (int)std::round(denoise_half_win * denoise_strength));
    }

    // 4. Denoise and Spline
    c.y_denoised = SignalProcessor::denoise_trace(detrended, denoise_thresh, denoise_half_win);
    c.t_us.resize(c.t_raw.size() * g_upsample_factor);
    for (size_t i = 0; i < c.t_us.size(); ++i) {
        c.t_us[i] = i / (g_fr * g_upsample_factor);
    }

    SplineInterpolator spline;
    spline.build(c.t_raw, c.y_denoised);
    c.y_us.resize(c.t_us.size());
    for (size_t i = 0; i < c.t_us.size(); ++i) {
        c.y_us[i] = spline.eval(c.t_us[i]);
    }

    // 5. Baseline SD
    int n_base_us = std::min((int)std::round(2.0 * g_fr * g_upsample_factor), (int)std::round(c.y_us.size() * 0.1));
    n_base_us = std::max(1, n_base_us);
    std::vector<double> base_win(c.y_us.begin(), c.y_us.begin() + n_base_us);
    double mb = 0; for (double x : base_win) mb += x; mb /= base_win.size();
    c.sd_base = SignalProcessor::compute_std(base_win, mb);
    if (c.sd_base <= 0) c.sd_base = 0.05;
}

// ============================================================================
// Downsampling helper for large arrays sent over IPC
// ============================================================================

static json downsample_array(const std::vector<double>& arr, size_t max_points = 2000) {
    json j = json::array();
    if (arr.size() <= max_points) {
        for (double v : arr) j.push_back(json_val(v));
    } else {
        double step = (double)arr.size() / max_points;
        for (size_t i = 0; i < max_points; ++i) {
            size_t idx = std::min((size_t)(i * step), arr.size() - 1);
            j.push_back(json_val(arr[idx]));
        }
    }
    return j;
}

// ============================================================================
// Command Handlers
// ============================================================================

// ── Folder Resolution Helpers ───────────────────────────────────────────────
// When the GUI passes a folder path, these find the appropriate file inside.

namespace fs = std::filesystem;

static std::string to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

/// Find the first TIFF file in a folder (prefers bolus1, prefers _shifted)
static std::string find_tiff_in_folder(const std::string& folder) {
    std::vector<std::string> tiffs;
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = to_lower(entry.path().extension().string());
        if (ext == ".tif" || ext == ".tiff") {
            tiffs.push_back(entry.path().string());
        }
    }
    if (tiffs.empty()) return "";
    // Prefer bolus1 baseline shifted
    for (const auto& t : tiffs) {
        std::string lower = to_lower(fs::path(t).filename().string());
        if (lower.find("bolus1") != std::string::npos &&
            lower.find("baseline") != std::string::npos &&
            lower.find("shifted") != std::string::npos) return t;
    }
    // Fallback: any bolus1
    for (const auto& t : tiffs) {
        if (to_lower(fs::path(t).filename().string()).find("bolus1") != std::string::npos) return t;
    }
    return tiffs[0];
}

/// Find ROI file (_rois.txt or _MaskObj.mat) in a folder
static std::string find_roi_in_folder(const std::string& folder) {
    // Prefer _rois.txt files (search recursively into subdirectories)
    std::string first_rois_txt;
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        std::string lower = to_lower(entry.path().filename().string());
        if (lower.find("_rois.txt") != std::string::npos) {
            // Prefer bolus1
            if (lower.find("bolus1") != std::string::npos) return entry.path().string();
            if (first_rois_txt.empty()) first_rois_txt = entry.path().string();
        }
    }
    if (!first_rois_txt.empty()) return first_rois_txt;

    // Fallback: _MaskObj.mat (recursive)
    std::string first_mat;
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        std::string lower = to_lower(entry.path().filename().string());
        if (lower.find("maskobj") != std::string::npos && lower.find(".mat") != std::string::npos) {
            if (lower.find("adjusted") == std::string::npos) {
                if (lower.find("bolus1") != std::string::npos) return entry.path().string();
                if (first_mat.empty()) first_mat = entry.path().string();
            }
        }
    }
    if (!first_mat.empty()) return first_mat;

    // Last fallback: any .mat (recursive)
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = to_lower(entry.path().extension().string());
        if (ext == ".mat") return entry.path().string();
    }
    return "";
}

/// Find results CSV in a folder
static std::string find_csv_in_folder(const std::string& folder) {
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        std::string lower = to_lower(entry.path().filename().string());
        if (lower.find("_results") != std::string::npos && lower.find(".csv") != std::string::npos) {
            // Prefer the C++ results
            if (lower.find("_cpp") != std::string::npos) return entry.path().string();
        }
    }
    // Fallback: any results csv
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        std::string lower = to_lower(entry.path().filename().string());
        if (lower.find("_results") != std::string::npos && lower.find(".csv") != std::string::npos) {
            return entry.path().string();
        }
    }
    return "";
}

/// Find framerate text file in a folder
static std::string find_framerate_file_in_folder(const std::string& folder) {
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        std::string lower = to_lower(entry.path().filename().string());
        std::string ext = to_lower(entry.path().extension().string());
        if (ext == ".txt" && lower.find("bolus") != std::string::npos &&
            lower.find("_results") == std::string::npos &&
            lower.find("_rois") == std::string::npos) {
            return entry.path().string();
        }
    }
    return "";
}

/// If path is a directory, resolve to a file; otherwise return as-is
static std::string resolve_path(const std::string& path,
                                std::string (*finder)(const std::string&)) {
    if (fs::is_directory(path)) {
        return finder(path);
    }
    return path;
}

static json handle_scan_folder(const json& params) {
    std::string folder = params.at("path").get<std::string>();
    if (!fs::is_directory(folder)) {
        return json{{"ok", false}, {"error", "Not a directory: " + folder}};
    }

    // Collect all TIFFs, ROIs, and CSVs (recursive)
    struct FileInfo { std::string path; std::string lower_name; };
    std::vector<FileInfo> all_tiffs, all_rois, all_csvs;
    std::string framerate_file;

    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        std::string p = entry.path().string();
        std::string lower = to_lower(entry.path().filename().string());
        std::string ext = to_lower(entry.path().extension().string());

        if (ext == ".tif" || ext == ".tiff") {
            // Skip MIP/MAX projection files and registered files
            if (lower.find("max_") == 0 || lower.find("registered") != std::string::npos ||
                lower.find("xyz_") == 0) continue;
            all_tiffs.push_back({p, lower});
        } else if (lower.find("_rois.txt") != std::string::npos) {
            all_rois.push_back({p, lower});
        } else if (lower.find("maskobj") != std::string::npos && ext == ".mat" &&
                   lower.find("adjusted") == std::string::npos) {
            all_rois.push_back({p, lower});
        } else if (lower.find("_results") != std::string::npos && ext == ".csv") {
            all_csvs.push_back({p, lower});
        } else if (ext == ".txt" && lower.find("bolus") != std::string::npos &&
                   lower.find("_results") == std::string::npos &&
                   lower.find("_rois") == std::string::npos) {
            if (framerate_file.empty()) framerate_file = p;
        }
    }

    // Extract bolus identifier from filename (e.g. "bolus1", "bolus3")
    auto get_bolus_id = [](const std::string& lower_name) -> std::string {
        size_t pos = lower_name.find("bolus");
        if (pos == std::string::npos) return "";
        size_t end = pos + 5;
        while (end < lower_name.size() && std::isdigit(lower_name[end])) end++;
        return lower_name.substr(pos, end - pos);
    };

    // Group into datasets by bolus ID
    // Group into datasets by (parent_dir + bolus_id) so shifted and non-shifted stay separate
    std::map<std::string, json> datasets;

    auto make_key = [&](const std::string& filepath, const std::string& bid) -> std::string {
        std::string parent = fs::path(filepath).parent_path().string();
        // Use relative path from scan folder for display
        std::string rel = parent;
        if (rel.find(folder) == 0) {
            rel = rel.substr(folder.size());
            if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) rel = rel.substr(1);
        }
        return rel + "|" + bid;
    };

    auto make_label = [&](const std::string& key) -> std::string {
        size_t sep = key.find('|');
        std::string dir_part = key.substr(0, sep);
        std::string bid = key.substr(sep + 1);
        if (dir_part.empty()) return bid;
        return bid + " (" + dir_part + ")";
    };

    for (const auto& t : all_tiffs) {
        std::string bid = get_bolus_id(t.lower_name);
        if (bid.empty()) bid = "unknown";
        std::string key = make_key(t.path, bid);
        if (!datasets.count(key)) {
            datasets[key] = json{
                {"bolus_id", bid}, {"label", make_label(key)},
                {"tiff_path", ""}, {"roi_path", ""},
                {"csv_path", ""}, {"tiff_name", ""}, {"roi_name", ""},
                {"csv_name", ""}, {"ready", false}
            };
        }
        datasets[key]["tiff_path"] = t.path;
        datasets[key]["tiff_name"] = fs::path(t.path).filename().string();
    }

    // Match ROIs to datasets — try same-dir first, then any dir with same bolus_id
    for (const auto& r : all_rois) {
        std::string bid = get_bolus_id(r.lower_name);
        if (bid.empty()) continue;
        std::string key = make_key(r.path, bid);
        // Direct match
        if (datasets.count(key)) {
            std::string current = datasets[key]["roi_path"].get<std::string>();
            if (current.empty() || (r.lower_name.find("_rois.txt") != std::string::npos)) {
                datasets[key]["roi_path"] = r.path;
                datasets[key]["roi_name"] = fs::path(r.path).filename().string();
            }
        } else {
            // Fallback: match any dataset with same bolus_id that has no ROI yet
            for (auto& [k, ds] : datasets) {
                if (ds["bolus_id"].get<std::string>() == bid &&
                    ds["roi_path"].get<std::string>().empty()) {
                    ds["roi_path"] = r.path;
                    ds["roi_name"] = fs::path(r.path).filename().string();
                }
            }
        }
    }

    // Match CSVs to datasets — same logic
    for (const auto& c : all_csvs) {
        std::string bid = get_bolus_id(c.lower_name);
        if (bid.empty()) continue;
        std::string key = make_key(c.path, bid);
        if (datasets.count(key)) {
            std::string current = datasets[key]["csv_path"].get<std::string>();
            if (current.empty() || c.lower_name.find("_cpp") != std::string::npos) {
                datasets[key]["csv_path"] = c.path;
                datasets[key]["csv_name"] = fs::path(c.path).filename().string();
            }
        } else {
            for (auto& [k, ds] : datasets) {
                if (ds["bolus_id"].get<std::string>() == bid &&
                    ds["csv_path"].get<std::string>().empty()) {
                    ds["csv_path"] = c.path;
                    ds["csv_name"] = fs::path(c.path).filename().string();
                }
            }
        }
    }

    // Mark readiness and build ordered array — prefer root-level (non-shifted) first
    json datasets_arr = json::array();
    for (auto& [key, ds] : datasets) {
        ds["ready"] = !ds["tiff_path"].get<std::string>().empty() &&
                      !ds["roi_path"].get<std::string>().empty();
        // Root-level datasets first (key starts with "|")
        bool is_root = (key.find('|') == 0);
        ds["is_root"] = is_root;
        datasets_arr.push_back(ds);
    }

    // Also provide legacy single-dataset fields (prefer root-level bolus1)
    std::string best_tiff, best_roi, best_csv;
    for (const auto& ds : datasets_arr) {
        std::string bid = ds["bolus_id"].get<std::string>();
        bool is_root = ds.value("is_root", false);
        bool has_tiff = !ds["tiff_path"].get<std::string>().empty();
        if (!has_tiff) continue;

        // First available or root-level bolus1
        if (best_tiff.empty() || (bid == "bolus1" && is_root)) {
            best_tiff = ds["tiff_path"].get<std::string>();
            best_roi = ds["roi_path"].get<std::string>();
            best_csv = ds["csv_path"].get<std::string>();
            if (bid == "bolus1" && is_root) break;
        }
    }

    return json{{"ok", true}, {"data", {
        {"folder", folder},
        {"datasets", datasets_arr},
        {"dataset_count", datasets_arr.size()},
        {"tiff_path", best_tiff},
        {"tiff_found", !best_tiff.empty()},
        {"roi_path", best_roi},
        {"roi_found", !best_roi.empty()},
        {"csv_path", best_csv},
        {"csv_found", !best_csv.empty()},
        {"framerate_path", framerate_file},
        {"framerate_found", !framerate_file.empty()},
        {"ready", !best_tiff.empty() && !best_roi.empty()}
    }}};
}

// ── Data Loading Handlers ───────────────────────────────────────────────────

static json handle_load_tiff(const json& params) {
    std::string path = params.at("path").get<std::string>();

    // If path is a directory, find the TIFF inside
    path = resolve_path(path, find_tiff_in_folder);
    if (path.empty()) {
        return json{{"ok", false}, {"error", "No TIFF file found in folder"}};
    }

    // FIX: Free old data BEFORE loading new to avoid 2x memory spike
    g_tiff = TiffData{};
    g_traces.clear();
    g_traces.shrink_to_fit();

    g_tiff = load_tiff(path);
    if (g_tiff.frames.empty()) {
        return json{{"ok", false}, {"error", "Failed to load TIFF: " + path}};
    }

    // FIX: Encode MIP as base64 grayscale instead of 262K JSON integers
    // Saves ~10MB of JSON text per load
    float mip_min = *std::min_element(g_tiff.mip.begin(), g_tiff.mip.end());
    float mip_max = *std::max_element(g_tiff.mip.begin(), g_tiff.mip.end());
    float mip_range = (mip_max - mip_min > 0) ? (mip_max - mip_min) : 1.0f;
    std::vector<uint8_t> mip_bytes(g_tiff.mip.size());
    for (size_t i = 0; i < g_tiff.mip.size(); ++i) {
        mip_bytes[i] = static_cast<uint8_t>(std::clamp((g_tiff.mip[i] - mip_min) / mip_range * 255.0f, 0.0f, 255.0f));
    }
    std::string mip_b64 = base64_encode(mip_bytes);

    return json{{"ok", true}, {"data", {
        {"width", g_tiff.width}, {"height", g_tiff.height},
        {"n_frames", g_tiff.frames.size()},
        {"mip_base64", mip_b64}
    }}};
}

static json handle_load_rois(const json& params) {
    std::string path = params.at("path").get<std::string>();

    // If path is a directory, find the ROI file inside
    path = resolve_path(path, find_roi_in_folder);
    if (path.empty()) {
        return json{{"ok", false}, {"error", "No ROI file found in folder"}};
    }

    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".mat") {
        g_rois = MatParser::load_rois_from_mat(path);
    } else {
        g_rois = load_rois_txt(path);
    }

    json rois_arr = json::array();
    for (const auto& roi : g_rois) {
        json poly = json::array();
        for (const auto& pt : roi.poly) {
            poly.push_back(json::array({pt.first, pt.second}));
        }
        rois_arr.push_back(json{{"id", roi.id}, {"poly", poly}});
    }
    return json{{"ok", true}, {"data", {{"rois", rois_arr}, {"count", g_rois.size()}}}};
}

static json handle_load_csv(const json& params) {
    std::string path = params.at("path").get<std::string>();

    // If path is a directory, find the CSV inside
    path = resolve_path(path, find_csv_in_folder);
    if (path.empty()) {
        // No CSV is not an error — just return empty records
        return json{{"ok", true}, {"data", {{"records", json::array()}, {"count", 0}}}};
    }

    g_records = read_results_csv(path);
    // Build lookup map: roi_id -> index
    g_record_map.clear();
    for (size_t i = 0; i < g_records.size(); ++i) {
        g_record_map[g_records[i].roi_id] = i;
    }
    json records_arr = json::array();
    for (const auto& r : g_records) {
        records_arr.push_back(record_to_json(r));
    }
    return json{{"ok", true}, {"data", {{"records", records_arr}, {"count", g_records.size()}, {"path", path}}}};
}


static json handle_save_csv(const json& params) {
    std::string path = params.at("path").get<std::string>();
    if (params.contains("records")) {
        g_records.clear();
        for (const auto& jr : params["records"]) {
            g_records.push_back(json_to_record(jr));
        }
    }
    save_results_csv(path, g_records);
    return json{{"ok", true}};
}

static json handle_compute_traces(const json& params) {
    g_fr = params.value("framerate", g_fr);
    g_drift_win = params.value("drift_window", g_drift_win);
    float denoise_strength = params.value("denoise_strength", 1.0f);

    g_traces.resize(g_rois.size());
    for (size_t i = 0; i < g_rois.size(); ++i) {
        compute_trace(i, denoise_strength);
    }

    // FIX: Free TIFF frame data — traces are now cached, raw pixels no longer needed.
    // This reclaims 186-314MB depending on the TIFF.
    size_t n_frames = g_tiff.frames.size();
    g_tiff.frames.clear();
    g_tiff.frames.shrink_to_fit();

    // FIX: Return only a lightweight summary instead of serializing ALL traces
    // into a single JSON bomb (was 50-100MB). Use get_trace to fetch one at a time.
    json roi_ids = json::array();
    for (size_t i = 0; i < g_rois.size(); ++i) {
        roi_ids.push_back(g_rois[i].id);
    }
    return json{{"ok", true}, {"data", {
        {"count", g_traces.size()},
        {"n_frames", n_frames},
        {"roi_ids", roi_ids}
    }}};
}

// ============================================================================
// Single-ROI Trace Retrieval (memory-safe: one ROI at a time)
// ============================================================================

static json handle_get_trace(const json& params) {
    int roi_idx = params.at("roi_index").get<int>();
    if (roi_idx < 0 || roi_idx >= (int)g_traces.size()) {
        return json{{"ok", false}, {"error", "Invalid ROI index"}};
    }
    const auto& c = g_traces[roi_idx];
    return json{{"ok", true}, {"data", {
        {"roi_id", g_rois[roi_idx].id},
        {"t_raw", downsample_array(c.t_raw)},
        {"y_raw", downsample_array(c.y_raw)},
        {"y_denoised", downsample_array(c.y_denoised)},
        {"t_us", downsample_array(c.t_us)},
        {"y_us", downsample_array(c.y_us)},
        {"sd_base", c.sd_base},
        {"drift_slope", c.drift_slope}
    }}};
}

// ============================================================================
// SVG Plot Rendering (C++ side — no JS chart library needed)
// ============================================================================

static json handle_render_plot(const json& params) {
    int roi_idx = params.at("roi_index").get<int>();
    if (roi_idx < 0 || roi_idx >= (int)g_traces.size()) {
        return json{{"ok", false}, {"error", "Invalid ROI index"}};
    }

    const auto& c = g_traces[roi_idx];
    int w = params.value("width", 900);
    int h = params.value("height", 400);
    float crop_min_pct = params.value("crop_min", 0.0f);
    float crop_max_pct = params.value("crop_max", 100.0f);

    // Accept optional marker overrides from front-end (for dragging)
    double m_onset = params.value("onset", -1.0);
    double m_peak  = params.value("peak", -1.0);
    double m_end   = params.value("end_t", -1.0);

    // Determine data range
    double data_min_x = c.t_raw.empty() ? 0.0 : c.t_raw.front();
    double data_max_x = c.t_raw.empty() ? 10.0 : c.t_raw.back();
    double data_range = data_max_x - data_min_x;

    double min_x = data_min_x + data_range * (crop_min_pct / 100.0);
    double max_x = data_min_x + data_range * (crop_max_pct / 100.0);
    if (max_x <= min_x) max_x = min_x + 1.0;

    // Use detrended raw data (matches pipeline — raw and denoised share baseline)
    const auto& y_plot_raw = c.y_raw;  // Use raw (non-detrended) to match pipeline _fit.svg
    double k = c.drift_slope;  // linear drift for gamma model

    // Y range from visible data
    double min_y = 1e9, max_y = -1e9;
    for (size_t i = 0; i < c.t_raw.size(); ++i) {
        if (c.t_raw[i] >= min_x && c.t_raw[i] <= max_x) {
            if (y_plot_raw[i] < min_y) min_y = y_plot_raw[i];
            if (y_plot_raw[i] > max_y) max_y = y_plot_raw[i];
        }
    }
    for (size_t i = 0; i < c.t_us.size(); ++i) {
        if (c.t_us[i] >= min_x && c.t_us[i] <= max_x) {
            double val = c.y_us[i] + k * c.t_us[i];  // add drift back
            if (val < min_y) min_y = val;
            if (val > max_y) max_y = val;
        }
    }
    if (min_y > max_y) { min_y = 0; max_y = 100; }
    double y_range = max_y - min_y;
    min_y -= 0.1 * y_range;
    max_y += 0.1 * y_range;

    int pad_l = 70, pad_r = 30, pad_t = 45, pad_b = 50;

    auto px_x = [&](double x) { return pad_l + (x - min_x) / (max_x - min_x) * (w - pad_l - pad_r); };
    auto px_y = [&](double y) { return h - pad_b - (y - min_y) / (max_y - min_y) * (h - pad_t - pad_b); };

    // MCM Dark theme colors
    const char* bg_col     = "#383833";
    const char* grid_col   = "#42403b";
    const char* axis_col   = "#99948c";
    const char* text_col   = "#b5b0a5";
    const char* raw_col    = "#5e8a8a";   // teal — raw trace
    const char* denoise_col = "#ebb84d";  // golden — denoised
    const char* fit_col    = "#2ca02c";   // green — gamma fit (matches pipeline)
    const char* title_col  = "#E08C40";

    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << w << "\" height=\"" << h
        << "\" viewBox=\"0 0 " << w << " " << h << "\">\n";
    svg << "  <rect width=\"100%\" height=\"100%\" fill=\"" << bg_col << "\"/>\n";

    // Grid
    NiceTicks xt = BolusVisualizer::get_nice_ticks(min_x, max_x, 12);
    NiceTicks yt = BolusVisualizer::get_nice_ticks(min_y, max_y, 6);
    for (double tx : xt.ticks) {
        double px = px_x(tx);
        svg << "  <line x1=\"" << px << "\" y1=\"" << pad_t << "\" x2=\"" << px << "\" y2=\"" << h - pad_b
            << "\" stroke=\"" << grid_col << "\" stroke-width=\"1\"/>\n";
    }
    for (double ty : yt.ticks) {
        double py = px_y(ty);
        svg << "  <line x1=\"" << pad_l << "\" y1=\"" << py << "\" x2=\"" << w - pad_r << "\" y2=\"" << py
            << "\" stroke=\"" << grid_col << "\" stroke-width=\"1\"/>\n";
    }

    // Axes
    svg << "  <line x1=\"" << pad_l << "\" y1=\"" << h - pad_b << "\" x2=\"" << w - pad_r << "\" y2=\""
        << h - pad_b << "\" stroke=\"" << axis_col << "\" stroke-width=\"1\"/>\n";
    svg << "  <line x1=\"" << pad_l << "\" y1=\"" << pad_t << "\" x2=\"" << pad_l << "\" y2=\""
        << h - pad_b << "\" stroke=\"" << axis_col << "\" stroke-width=\"1\"/>\n";

    // Axis labels
    for (double tx : xt.ticks) {
        double px = px_x(tx);
        svg << "  <text x=\"" << px << "\" y=\"" << h - pad_b + 18
            << "\" font-family=\"sans-serif\" font-size=\"11\" text-anchor=\"middle\" fill=\""
            << text_col << "\">" << BolusVisualizer::format_tick(tx) << "</text>\n";
    }
    for (double ty : yt.ticks) {
        double py = px_y(ty);
        svg << "  <text x=\"" << pad_l - 8 << "\" y=\"" << py + 4
            << "\" font-family=\"sans-serif\" font-size=\"11\" text-anchor=\"end\" fill=\""
            << text_col << "\">" << BolusVisualizer::format_tick(ty) << "</text>\n";
    }

    // Axis titles
    svg << "  <text x=\"" << pad_l + (w - pad_l - pad_r) / 2.0 << "\" y=\"" << h - 10
        << "\" font-family=\"sans-serif\" font-size=\"12\" text-anchor=\"middle\" fill=\""
        << text_col << "\">Time (s)</text>\n";
    svg << "  <text x=\"14\" y=\"" << pad_t + (h - pad_t - pad_b) / 2.0
        << "\" font-family=\"sans-serif\" font-size=\"12\" text-anchor=\"middle\" fill=\""
        << text_col << "\" transform=\"rotate(-90 14 " << pad_t + (h - pad_t - pad_b) / 2.0
        << ")\">Signal (MFI)</text>\n";

    // Title with QC badge
    std::string qc_str = "";
    const CsvRecord* rec_ptr = find_record_for_roi(roi_idx);
    if (rec_ptr) {
        qc_str = " — " + rec_ptr->qc_flag;
    }
    svg << "  <text x=\"" << pad_l << "\" y=\"" << pad_t - 18
        << "\" font-family=\"sans-serif\" font-size=\"14\" font-weight=\"bold\" fill=\""
        << title_col << "\">ROI " << g_rois[roi_idx].id << qc_str << "</text>\n";

    // Raw data (detrended — matches pipeline)
    if (!c.t_raw.empty()) {
        svg << "  <path d=\"";
        bool first = true;
        for (size_t i = 0; i < c.t_raw.size(); ++i) {
            if (c.t_raw[i] < min_x || c.t_raw[i] > max_x) continue;
            double px = px_x(c.t_raw[i]), py = px_y(y_plot_raw[i]);
            svg << (first ? "M " : " L ") << px << " " << py;
            first = false;
        }
        svg << "\" fill=\"none\" stroke=\"" << raw_col << "\" stroke-width=\"1\" stroke-opacity=\"0.6\"/>\n";

        for (size_t i = 0; i < c.t_raw.size(); ++i) {
            if (c.t_raw[i] < min_x || c.t_raw[i] > max_x) continue;
            svg << "  <circle cx=\"" << px_x(c.t_raw[i]) << "\" cy=\"" << px_y(y_plot_raw[i])
                << "\" r=\"2.0\" fill=\"" << raw_col << "\" fill-opacity=\"0.6\"/>\n";
        }
    }

    // Denoised line (computed on detrended data — add back drift for display)
    if (c.y_denoised.size() == c.t_raw.size()) {
        svg << "  <path d=\"";
        bool first = true;
        for (size_t i = 0; i < c.t_raw.size(); ++i) {
            if (c.t_raw[i] < min_x || c.t_raw[i] > max_x) continue;
            double val = c.y_denoised[i] + k * c.t_raw[i];  // add drift back
            double px = px_x(c.t_raw[i]), py = px_y(val);
            svg << (first ? "M " : " L ") << px << " " << py;
            first = false;
        }
        svg << "\" fill=\"none\" stroke=\"" << denoise_col << "\" stroke-width=\"1.5\"/>\n";
    }

    // Gamma Fit curve — use k*t + gamma(t - click_onset) 
    // The fit was done on t_fit = tl_us[i] - tl_us[start_idx], so f_t2p is
    // relative to click_onset (the fit window start), not click_start (always 0).
    // Clip to fit window [click_onset, click_end] to match pipeline _fit.svg.
    bool has_fit = false;
    if (rec_ptr) {
        double f_amp = rec_ptr->f_amp, f_t2p = rec_ptr->f_t2p, f_fwhm = rec_ptr->f_fwhm, f_m = rec_ptr->f_m;
        double fit_origin = rec_ptr->click_onset;  // gamma model time origin = fit window start
        double fit_end = rec_ptr->click_end;        // gamma model end = fit window end

        if (!std::isnan(f_amp) && !std::isnan(f_t2p) && !std::isnan(f_fwhm) && f_t2p > 0 && f_fwhm > 0) {
            has_fit = true;
            svg << "  <path d=\"";
            bool first = true;
            for (size_t i = 0; i < c.t_us.size(); ++i) {
                double t = c.t_us[i];
                if (t < min_x || t > max_x) continue;
                double dt = t - fit_origin;
                double val;
                if (dt <= 0.0) {
                    val = k * t + f_m;  // before onset: flat baseline
                } else if (t > fit_end && fit_end > 0) {
                    continue;  // after fit window: don't render
                } else {
                    val = k * t + evaluate_gamma_model(dt, f_amp, f_t2p, f_fwhm, f_m);
                }
                double px = px_x(t), py = px_y(val);
                svg << (first ? "M " : " L ") << px << " " << py;
                first = false;
            }
            svg << "\" fill=\"none\" stroke=\"" << fit_col << "\" stroke-width=\"2.5\"/>\n";
        }
    }

    // Legend
    int legend_items = has_fit ? 3 : 2;
    int legend_h = 18 * legend_items + 12;
    int lx = w - pad_r - 150, ly = pad_t + 10;
    svg << "  <rect x=\"" << lx << "\" y=\"" << ly << "\" width=\"140\" height=\"" << legend_h << "\" rx=\"4\" "
        << "fill=\"" << bg_col << "\" fill-opacity=\"0.9\" stroke=\"" << grid_col << "\"/>\n";

    int li = 0;
    svg << "  <line x1=\"" << lx+8 << "\" y1=\"" << ly+15+li*18 << "\" x2=\"" << lx+28 << "\" y2=\"" << ly+15+li*18
        << "\" stroke=\"" << raw_col << "\" stroke-width=\"2\"/>\n";
    svg << "  <text x=\"" << lx+34 << "\" y=\"" << ly+19+li*18
        << "\" font-size=\"11\" fill=\"" << text_col << "\">Raw (Detrended)</text>\n";
    li++;
    svg << "  <line x1=\"" << lx+8 << "\" y1=\"" << ly+15+li*18 << "\" x2=\"" << lx+28 << "\" y2=\"" << ly+15+li*18
        << "\" stroke=\"" << denoise_col << "\" stroke-width=\"1.5\"/>\n";
    svg << "  <text x=\"" << lx+34 << "\" y=\"" << ly+19+li*18
        << "\" font-size=\"11\" fill=\"" << text_col << "\">Denoised</text>\n";
    li++;
    if (has_fit) {
        svg << "  <line x1=\"" << lx+8 << "\" y1=\"" << ly+15+li*18 << "\" x2=\"" << lx+28 << "\" y2=\"" << ly+15+li*18
            << "\" stroke=\"" << fit_col << "\" stroke-width=\"2.5\"/>\n";
        svg << "  <text x=\"" << lx+34 << "\" y=\"" << ly+19+li*18
            << "\" font-size=\"11\" fill=\"" << text_col << "\">Gamma Fit</text>\n";
    }

    svg << "</svg>\n";

    // Return coordinate mapping for JS-side draggable markers
    json coord_map = {
        {"pad_l", pad_l}, {"pad_r", pad_r}, {"pad_t", pad_t}, {"pad_b", pad_b},
        {"min_x", min_x}, {"max_x", max_x}, {"min_y", min_y}, {"max_y", max_y},
        {"svg_w", w}, {"svg_h", h}
    };

    // Return marker positions (from records or front-end overrides)
    json markers = json::object();
    if (rec_ptr) {
        markers["onset"] = m_onset >= 0 ? m_onset : rec_ptr->click_onset;
        markers["peak"]  = m_peak >= 0 ? m_peak : rec_ptr->click_peak;
        markers["end"]   = m_end >= 0 ? m_end : rec_ptr->click_end;
        markers["baseline"] = rec_ptr->f_m;
        markers["click_start"] = rec_ptr->click_start;
    }

    return json{{"ok", true}, {"data", {{"svg", svg.str()}, {"coord", coord_map}, {"markers", markers}}}};
}

static json handle_auto_estimate(const json& params) {
    int roi_idx = params.at("roi_index").get<int>();
    if (roi_idx < 0 || roi_idx >= (int)g_traces.size()) {
        return json{{"ok", false}, {"error", "Invalid ROI index"}};
    }
    const auto& c = g_traces[roi_idx];
    AutoEstimateResults res = g_fitter.auto_estimate_params(c.y_us, c.t_us, g_fr, g_upsample_factor);
    return json{{"ok", true}, {"data", {
        {"click_start", res.click_start}, {"click_onset", res.click_onset},
        {"click_peak", res.click_peak}, {"click_end", res.click_end},
        {"init_params", {res.init_params[0], res.init_params[1], res.init_params[2], res.init_params[3]}},
        {"sd_base", res.sd_base}
    }}};
}

// Recompute baseline from upsampled denoised trace over a user-adjustable window
// Matches pipeline logic: median of first ~2s (or 10% of trace)
static json handle_compute_baseline(const json& params) {
    int roi_idx = params.at("roi_index").get<int>();
    if (roi_idx < 0 || roi_idx >= (int)g_traces.size()) {
        return json{{"ok", false}, {"error", "Invalid ROI index"}};
    }
    const auto& c = g_traces[roi_idx];
    if (c.t_us.empty()) return json{{"ok", false}, {"error", "No trace data"}};

    // User-adjustable: start time and duration for baseline window
    double start_time = params.value("start_time", 0.0);  // seconds
    double default_dur = 2.0 / g_fr * g_fr;  // ~2 seconds
    double duration = params.value("duration_sec", default_dur > 0 ? default_dur : 2.0);

    // Collect samples from t_us/y_us in the [start_time, start_time + duration] window
    std::vector<double> window_vals;
    for (size_t i = 0; i < c.t_us.size(); ++i) {
        if (c.t_us[i] >= start_time && c.t_us[i] <= start_time + duration) {
            window_vals.push_back(c.y_us[i]);
        }
    }

    // Fallback: if window is empty (bad start_time), use pipeline default
    if (window_vals.empty()) {
        int n_base = std::min((int)std::round(2.0 * g_fr * g_upsample_factor),
                              (int)std::round(c.y_us.size() * 0.1));
        n_base = std::max(1, n_base);
        window_vals.assign(c.y_us.begin(), c.y_us.begin() + n_base);
    }

    double baseline_median = SignalProcessor::compute_median(window_vals);
    double sum_v = 0; for (double v : window_vals) sum_v += v;
    double baseline_mean = sum_v / window_vals.size();

    return json{{"ok", true}, {"data", {
        {"baseline_median", baseline_median},
        {"baseline_mean", baseline_mean},
        {"n_samples", (int)window_vals.size()},
        {"start_time", start_time},
        {"duration", duration}
    }}};
}

static json handle_run_fit(const json& params) {
    int roi_idx = params.at("roi_index").get<int>();
    double onset = params.at("onset").get<double>();
    double peak = params.at("peak").get<double>();
    double end_t = params.at("end").get<double>();
    double baseline = params.at("baseline").get<double>();
    double crop_min = params.value("crop_min", 0.0);

    if (roi_idx < 0 || roi_idx >= (int)g_traces.size()) {
        return json{{"ok", false}, {"error", "Invalid ROI index"}};
    }
    const auto& c = g_traces[roi_idx];

    // Build fit window from upsampled data
    std::vector<double> t_fit, y_fit;
    for (size_t i = 0; i < c.t_us.size(); ++i) {
        if (c.t_us[i] >= onset && c.t_us[i] <= end_t) {
            t_fit.push_back(c.t_us[i] - onset);
            y_fit.push_back(c.y_us[i]);
        }
    }
    if (t_fit.size() < 5) {
        return json{{"ok", false}, {"error", "Fit window too small"}};
    }

    // Initial guesses from markers
    size_t peak_idx = 0;
    double min_dist = 1e9;
    for (size_t i = 0; i < c.t_us.size(); ++i) {
        double d = std::abs(c.t_us[i] - peak);
        if (d < min_dist) { min_dist = d; peak_idx = i; }
    }
    double guess_amp = c.y_us[peak_idx] - baseline;
    if (guess_amp < 0.1) guess_amp = 10.0;
    double guess_t2p = peak - onset;
    if (guess_t2p < 0.1) guess_t2p = 3.0;
    double guess_fwhm = (end_t - onset) / 2.0;
    if (guess_fwhm < 0.1) guess_fwhm = 5.0;

    std::vector<double> init_params = {guess_amp, guess_t2p, guess_fwhm, baseline};
    bool fit_success = false, pass2_run = false;
    std::vector<double> popt = g_fitter.run_nonlinear_fit(t_fit, y_fit, init_params, c.sd_base, fit_success, pass2_run);

    // Build fit curve for plotting
    json fit_t = json::array(), fit_y = json::array();
    if (fit_success && popt[0] > 1e-6 && popt[1] > 1e-6 && popt[2] > 0.5) {
        for (size_t i = 0; i < c.t_us.size(); ++i) {
            double t = c.t_us[i];
            fit_t.push_back(t);
            if (t > end_t) {
                fit_y.push_back(nullptr);
            } else if (t >= onset) {
                double dt = t - onset;
                fit_y.push_back(evaluate_gamma_model(dt, popt[0], popt[1], popt[2], popt[3]));
            } else {
                fit_y.push_back(popt[3]);
            }
        }
    }

    // Determine QC flag
    std::string qc_flag = "FAIL";
    double actual_max_t2p = (g_fitter.max_t2p >= 1e5 && !t_fit.empty()) ? t_fit.back() : g_fitter.max_t2p;
    double actual_max_fwhm = (g_fitter.max_fwhm >= 1e5 && !t_fit.empty()) ? t_fit.back() : g_fitter.max_fwhm;
    if (fit_success && popt[0] > 1e-6 && popt[1] > 1e-6 && popt[2] > 0.5) {
        qc_flag = BolusFitter::determine_qc_flag(
            popt[0], popt[1], popt[2], popt[3], popt[0] / c.sd_base,
            g_fitter.min_amp, g_fitter.max_amp, g_fitter.min_t2p, actual_max_t2p,
            g_fitter.min_fwhm, actual_max_fwhm, fit_success, pass2_run);
    }

    // ── Kinetics computation (matches dataset_processor.cpp L269-357) ──
    double auc = NAN, aucn = NAN, ont = NAN, ttm = NAN, ttlb = NAN, tthb = NAN;
    double denoise_rms = 0.0;
    std::string ves_type = "U";

    if (fit_success && popt.size() >= 4) {
        // Build gamma model over fit window
        std::vector<double> y_fit_model(t_fit.size());
        for (size_t i = 0; i < t_fit.size(); ++i) {
            y_fit_model[i] = evaluate_gamma_model(t_fit[i], popt[0], popt[1], popt[2], popt[3]);
        }

        // AUC (trapezoidal rule)
        double sum_y = 0.0;
        for (double val : y_fit_model) sum_y += val;
        auc = sum_y - (y_fit_model.front() + y_fit_model.back()) / 2.0;

        // AUCn (normalized)
        double min_y = y_fit_model[0], max_y = y_fit_model[0];
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
        aucn = sum_yn - (first_yn + last_yn) / 2.0;

        // Onset time from 10% threshold on normalized model
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
                if (I[k+1] - I[k] == 1) last_idx = k;
            }
            onset_idx = (last_idx != -1) ? I[last_idx] + 1 : I[0];
        }
        ont = (double)onset_idx / (g_fr * g_upsample_factor);
        ttm = std::abs(popt[1] - ont);

        // Parameter SE and transit time CIs (matches dataset_processor L325-337)
        double sum_sq_resid = 0.0;
        for (size_t i = 0; i < y_fit.size(); ++i) {
            double diff = y_fit[i] - y_fit_model[i];
            sum_sq_resid += diff * diff;
        }
        double mse = (y_fit.size() > 4) ? (sum_sq_resid / (y_fit.size() - 4)) : 0.0;
        std::vector<double> se = g_fitter.get_parameter_se(t_fit, popt, mse);
        double se_t2p = se[1];
        double ci_lower = popt[1] - 1.96 * se_t2p;
        double ci_upper = popt[1] + 1.96 * se_t2p;
        ttlb = std::abs(ci_lower - ont);
        tthb = std::abs(ci_upper - ont);

        // NaN guard (matches dataset_processor L339-343)
        if (std::isnan(popt[0]) || std::isnan(popt[1]) || std::isnan(popt[2]) || std::isnan(popt[3]) ||
            std::isnan(popt[0] / c.sd_base) || std::isnan(popt[3] / c.sd_base) ||
            std::isnan(auc) || std::isnan(aucn) ||
            std::isnan(ttlb) || std::isnan(ttm) || std::isnan(tthb) || std::isnan(ont)) {
            qc_flag = "FAIL";
        }
    }

    // Denoise RMS (matches dataset_processor L361-366)
    {
        double sum_sq_diff = 0.0;
        for (size_t i = 0; i < c.y_raw_detrended.size(); ++i) {
            double diff = c.y_raw_detrended[i] - c.y_denoised[i];
            sum_sq_diff += diff * diff;
        }
        denoise_rms = (c.y_raw_detrended.size() > 0) ? std::sqrt(sum_sq_diff / c.y_raw_detrended.size()) : 0.0;
    }

    // Vessel type classification (matches dataset_processor L359)
    ves_type = BolusFitter::suggest_vessel_type(ont, popt.size() >= 2 ? popt[1] : NAN,
                                                 popt.size() >= 3 ? popt[2] : NAN,
                                                 popt.size() >= 1 ? popt[0] : NAN, qc_flag);

    // Update g_records with ALL fields (matches dataset_processor L269-359)
    if (fit_success && popt.size() >= 4) {
        const CsvRecord* existing = find_record_for_roi(roi_idx);
        CsvRecord updated;
        if (existing) {
            updated = *existing;
        } else {
            updated.roi_id = (roi_idx < (int)g_rois.size()) ? g_rois[roi_idx].id : roi_idx;
        }
        updated.f_amp = popt[0];
        updated.f_t2p = popt[1];
        updated.f_fwhm = popt[2];
        updated.f_m = popt[3];
        updated.f_cnr = popt[0] / c.sd_base;
        updated.f_snr = popt[3] / c.sd_base;
        updated.click_onset = onset;
        updated.click_peak = peak;
        updated.click_end = end_t;
        updated.auc = auc;
        updated.aucn = aucn;
        updated.ont = ont;
        updated.ttm = ttm;
        updated.ttlb = ttlb;
        updated.tthb = tthb;
        updated.denoise_rms = denoise_rms;
        updated.raw_sd_base = c.raw_sd_base;
        updated.ves_type = ves_type;
        updated.qc_flag = qc_flag;
        updated.fit_source = "manual";

        // Update or insert into g_records
        auto it = g_record_map.find(updated.roi_id);
        if (it != g_record_map.end() && it->second < g_records.size()) {
            g_records[it->second] = updated;
        } else {
            g_record_map[updated.roi_id] = g_records.size();
            g_records.push_back(updated);
        }
    }

    return json{{"ok", true}, {"data", {
        {"fit_success", fit_success},
        {"params", {popt.size() >= 4 ? popt[0] : 0, popt.size() >= 4 ? popt[1] : 0,
                    popt.size() >= 4 ? popt[2] : 0, popt.size() >= 4 ? popt[3] : 0}},
        {"cnr", (fit_success && popt.size() >= 4) ? popt[0] / c.sd_base : 0},
        {"qc_flag", qc_flag},
        {"fit_curve_t", fit_t}, {"fit_curve_y", fit_y},
        {"onset", onset}, {"peak", peak}, {"end", end_t}, {"baseline", baseline},
        {"auc", auc}, {"aucn", aucn}, {"ont", ont}, {"ttm", ttm},
        {"ttlb", ttlb}, {"tthb", tthb}, {"ves_type", ves_type},
        {"denoise_rms", denoise_rms}, {"stall_flag", 0},
        {"effective_max_t2p", actual_max_t2p},
        {"effective_max_fwhm", actual_max_fwhm}
    }}};
}

// ============================================================================
// Batch Fit — Full pipeline equivalent (matches dataset_processor.cpp)
// ============================================================================

static json handle_batch_fit(const json& /*params*/) {
    if (g_traces.empty()) {
        return json{{"ok", false}, {"error", "No traces loaded"}};
    }

    g_records.clear();
    g_record_map.clear();
    g_records.resize(g_traces.size());

    int pass_count = 0, warn_count = 0, fail_count = 0, stall_count = 0;

    // ── Pass 1: Auto-estimate + fit each ROI (matches process_single_roi) ──
    for (size_t r = 0; r < g_traces.size(); ++r) {
        auto& c = g_traces[r];
        // 1-indexed ROI IDs to match CLI pipeline (dataset_processor.cpp:499)
        int roi_id = (r < g_rois.size()) ? (g_rois[r].id + 1) : (int)(r + 1);
        CsvRecord& rec = g_records[r];
        rec.roi_id = roi_id;
        g_record_map[roi_id] = r;

        // ── Recompute trace with CLI-matching denoising ──
        // The GUI compute_trace may have used a user-adjusted denoise_strength,
        // but the CLI pipeline always uses the raw CNR-adaptive settings.
        // Recompute from y_raw_detrended to match process_single_roi exactly.
        bool is_low_cnr = false;
        {
            const auto& detrended = c.y_raw_detrended;
            int n_base = std::min((int)std::round(2.0 * g_fr), (int)std::round(detrended.size() * 0.1));
            n_base = std::max(2, n_base);
            std::vector<double> raw_base_win(detrended.begin(), detrended.begin() + n_base);
            double raw_baseline = SignalProcessor::compute_median(raw_base_win);
            double sum_rb = 0; for (double v : raw_base_win) sum_rb += v;
            double raw_sd = SignalProcessor::compute_std(raw_base_win, sum_rb / raw_base_win.size());
            c.raw_sd_base = raw_sd;
            double raw_max_val = -1e9;
            for (double v : detrended) if (v > raw_max_val) raw_max_val = v;
            double raw_cnr = (raw_sd > 0) ? ((raw_max_val - raw_baseline) / raw_sd) : 0;

            // CLI-matching adaptive denoise (no user strength modifier)
            double denoise_thresh = 2.0;
            int denoise_half_win = 5;
            if (raw_cnr < 4.0) { denoise_thresh = 1.5; denoise_half_win = 7; is_low_cnr = true; }
            else if (raw_cnr >= 15.0) { denoise_thresh = 3.0; denoise_half_win = 3; }
            else if (raw_cnr >= 8.0) { denoise_thresh = 2.5; denoise_half_win = 5; }

            c.y_denoised = SignalProcessor::denoise_trace(detrended, denoise_thresh, denoise_half_win);

            // Rebuild spline and upsampled trace
            SplineInterpolator spline;
            spline.build(c.t_raw, c.y_denoised);
            for (size_t i = 0; i < c.t_us.size(); ++i) c.y_us[i] = spline.eval(c.t_us[i]);

            // Recompute upsampled baseline SD
            int n_base_us = std::min((int)std::round(2.0 * g_fr * g_upsample_factor), (int)std::round(c.y_us.size() * 0.1));
            n_base_us = std::max(1, n_base_us);
            std::vector<double> base_win(c.y_us.begin(), c.y_us.begin() + n_base_us);
            double mb = 0; for (double x : base_win) mb += x; mb /= base_win.size();
            c.sd_base = SignalProcessor::compute_std(base_win, mb);
            if (c.sd_base <= 0) c.sd_base = 0.05;
        }

        // Auto-estimate with correct is_low_cnr flag (matches dataset_processor.cpp:163)
        AutoEstimateResults auto_res = g_fitter.auto_estimate_params(c.y_us, c.t_us, g_fr, g_upsample_factor, is_low_cnr);
        double init_cnr = (auto_res.sd_base > 0.0) ? (auto_res.init_params[0] / auto_res.sd_base) : 0.0;
        // Only re-denoise if init_cnr < 5 AND not already using low-CNR settings
        // (matches dataset_processor.cpp:166 guard: "if (init_cnr < 5.0 && !is_low_cnr)")
        if (init_cnr < 5.0 && !is_low_cnr) {
            c.y_denoised = SignalProcessor::denoise_trace(c.y_raw_detrended, 1.5, 7);
            SplineInterpolator spline;
            spline.build(c.t_raw, c.y_denoised);
            for (size_t i = 0; i < c.t_us.size(); ++i) c.y_us[i] = spline.eval(c.t_us[i]);
            auto_res = g_fitter.auto_estimate_params(c.y_us, c.t_us, g_fr, g_upsample_factor, true);
        }

        rec.init_amp = auto_res.init_params[0];
        rec.init_t2p = auto_res.init_params[1];
        rec.init_fwhm = auto_res.init_params[2];
        rec.init_m = auto_res.init_params[3];
        rec.init_snr = (auto_res.sd_base > 0) ? (auto_res.init_params[3] / auto_res.sd_base) : NAN;
        rec.init_cnr = (auto_res.sd_base > 0) ? (auto_res.init_params[0] / auto_res.sd_base) : 0;
        rec.click_start = auto_res.click_start;
        rec.click_onset = auto_res.click_onset;
        rec.click_peak = auto_res.click_peak;
        rec.click_end = auto_res.click_end;
        rec.raw_sd_base = c.raw_sd_base;
        rec.roi_size = (r < g_rois.size()) ? (int)g_rois[r].poly.size() : 0;

        int start_idx = auto_res.start_idx, end_idx = auto_res.end_idx;
        // Clamp indices to valid range (CLI doesn't early-exit, just builds empty arrays)
        if (start_idx < 0) start_idx = 0;
        if (end_idx > (int)c.t_us.size()) end_idx = (int)c.t_us.size();
        if (end_idx < start_idx) end_idx = start_idx;

        std::vector<double> t_fit(end_idx - start_idx), y_fit(end_idx - start_idx);
        // CLI uses exclusive end: for (int i = start_idx; i < end_idx; ++i)
        for (int i = start_idx; i < end_idx; ++i) {
            t_fit[i - start_idx] = c.t_us[i] - c.t_us[start_idx];
            y_fit[i - start_idx] = c.y_us[i];
        }

        // Fit (matches dataset_processor.cpp:195-227)
        bool fit_success = false, pass2_run = false;
        std::vector<double> popt = g_fitter.run_nonlinear_fit(t_fit, y_fit, auto_res.init_params, auto_res.sd_base, fit_success, pass2_run);

        // QC flag (matches dataset_processor.cpp:249-265)
        std::string qc_flag = "FAIL";
        if (fit_success) {
            double actual_max_t2p = (g_fitter.max_t2p >= 1e5 && !t_fit.empty()) ? (c.t_us[end_idx] - c.t_us[start_idx]) : g_fitter.max_t2p;
            double actual_max_fwhm = (g_fitter.max_fwhm >= 1e5 && !t_fit.empty()) ? (c.t_us[end_idx] - c.t_us[start_idx]) : g_fitter.max_fwhm;
            double f_cnr = (auto_res.sd_base > 0.0) ? (popt[0] / auto_res.sd_base) : 0.0;
            qc_flag = BolusFitter::determine_qc_flag(
                popt[0], popt[1], popt[2], popt[3], f_cnr,
                g_fitter.min_amp, g_fitter.max_amp, g_fitter.min_t2p, actual_max_t2p,
                g_fitter.min_fwhm, actual_max_fwhm, fit_success, pass2_run);
        }
        rec.qc_flag = qc_flag;
        rec.fit_source = "auto";

        if (fit_success && popt.size() >= 4) {
            rec.f_amp = popt[0]; rec.f_t2p = popt[1]; rec.f_fwhm = popt[2]; rec.f_m = popt[3];
            rec.f_cnr = (auto_res.sd_base > 0) ? (popt[0] / auto_res.sd_base) : NAN;
            rec.f_snr = (auto_res.sd_base > 0) ? (popt[3] / auto_res.sd_base) : NAN;

            // Kinetics (matches dataset_processor.cpp:277-337)
            std::vector<double> y_fit_model(t_fit.size());
            for (size_t i = 0; i < t_fit.size(); ++i)
                y_fit_model[i] = evaluate_gamma_model(t_fit[i], popt[0], popt[1], popt[2], popt[3]);

            double sum_y = 0; for (double v : y_fit_model) sum_y += v;
            rec.auc = sum_y - (y_fit_model.front() + y_fit_model.back()) / 2.0;

            double mn = y_fit_model[0], mx = y_fit_model[0];
            for (double v : y_fit_model) { if (v < mn) mn = v; if (v > mx) mx = v; }
            double rng = mx - mn;
            double syn = 0; for (double v : y_fit_model) syn += (rng > 0) ? (v - mn) / rng : 0;
            double fyn = (rng > 0) ? (y_fit_model.front() - mn) / rng : 0;
            double lyn = (rng > 0) ? (y_fit_model.back() - mn) / rng : 0;
            rec.aucn = syn - (fyn + lyn) / 2.0;

            std::vector<int> I;
            for (size_t i = 0; i < y_fit_model.size(); ++i) {
                double vn = (rng > 0) ? (y_fit_model[i] - mn) / rng : 0;
                if (vn < 0.1) I.push_back(i);
            }
            int oidx = 0;
            if (!I.empty()) {
                int li = -1;
                for (size_t k = 0; k + 1 < I.size(); ++k) { if (I[k+1] - I[k] == 1) li = k; }
                oidx = (li != -1) ? I[li] + 1 : I[0];
            }
            rec.ont = (double)oidx / (g_fr * g_upsample_factor);
            rec.ttm = std::abs(popt[1] - rec.ont);

            double ssr = 0;
            for (size_t i = 0; i < y_fit.size(); ++i) { double d = y_fit[i] - y_fit_model[i]; ssr += d*d; }
            double mse = (y_fit.size() > 4) ? ssr / (y_fit.size() - 4) : 0;
            auto se = g_fitter.get_parameter_se(t_fit, popt, mse);
            rec.ttlb = std::abs((popt[1] - 1.96 * se[1]) - rec.ont);
            rec.tthb = std::abs((popt[1] + 1.96 * se[1]) - rec.ont);

            if (std::isnan(rec.f_amp) || std::isnan(rec.f_t2p) || std::isnan(rec.f_fwhm) || std::isnan(rec.f_m) ||
                std::isnan(rec.f_cnr) || std::isnan(rec.f_snr) || std::isnan(rec.auc) || std::isnan(rec.aucn) ||
                std::isnan(rec.ttlb) || std::isnan(rec.ttm) || std::isnan(rec.tthb) || std::isnan(rec.ont)) {
                rec.qc_flag = "FAIL";
            }
        } else {
            rec.f_amp = NAN; rec.f_t2p = NAN; rec.f_fwhm = NAN; rec.f_m = NAN;
            rec.f_cnr = NAN; rec.f_snr = NAN;
            rec.auc = NAN; rec.aucn = NAN; rec.ttlb = NAN; rec.ttm = NAN; rec.tthb = NAN; rec.ont = NAN;
        }

        rec.ves_type = BolusFitter::suggest_vessel_type(rec.ont, rec.f_t2p, rec.f_fwhm, rec.f_amp, rec.qc_flag);
        double sd2 = 0;
        for (size_t i = 0; i < c.y_raw_detrended.size(); ++i) {
            double d = c.y_raw_detrended[i] - c.y_denoised[i]; sd2 += d*d;
        }
        rec.denoise_rms = (c.y_raw_detrended.size() > 0) ? std::sqrt(sd2 / c.y_raw_detrended.size()) : 0;
    }

    // ── Pass 2: Quality-Aware Population Priors Refitting ──
    // (matches dataset_processor.cpp:509-562 — uses high-CNR PASS only)
    std::vector<double> high_quality_t2ps, high_quality_fwhms;
    for (const auto& rec : g_records) {
        if (rec.qc_flag == "PASS" && rec.f_cnr > 10.0) {
            high_quality_t2ps.push_back(rec.f_t2p);
            high_quality_fwhms.push_back(rec.f_fwhm);
        }
    }

    if (!high_quality_t2ps.empty()) {
        double median_t2p = SignalProcessor::compute_median(high_quality_t2ps);
        double median_fwhm = SignalProcessor::compute_median(high_quality_fwhms);

        for (size_t r = 0; r < g_records.size(); ++r) {
            CsvRecord& rec = g_records[r];
            if (rec.qc_flag != "FAIL" && rec.qc_flag != "WARN") continue;
            // Divergence guard (matches dataset_processor.cpp:533-534)
            if (!std::isnan(rec.f_t2p) && rec.f_t2p > 3.0 * median_t2p) continue;

            const auto& c = g_traces[r];
            AutoEstimateResults auto_res = g_fitter.auto_estimate_params(c.y_us, c.t_us, g_fr, g_upsample_factor, false);
            double icnr = (auto_res.sd_base > 0.0) ? (auto_res.init_params[0] / auto_res.sd_base) : 0.0;
            if (icnr < 5.0) {
                auto_res = g_fitter.auto_estimate_params(c.y_us, c.t_us, g_fr, g_upsample_factor, true);
            }

            int si = auto_res.start_idx, ei = auto_res.end_idx;
            if (si < 0 || ei <= si || ei >= (int)c.t_us.size()) continue;
            std::vector<double> t_fit, y_fit;
            for (int i = si; i < ei; ++i) {
                t_fit.push_back(c.t_us[i] - c.t_us[si]);
                y_fit.push_back(c.y_us[i]);
            }
            if (t_fit.size() < 5) continue;

            bool refit_ok = false;
            // CLI replaces T2P/FWHM initial estimates with population medians
            std::vector<double> prior_params = auto_res.init_params;
            prior_params[1] = median_t2p;
            prior_params[2] = median_fwhm;
            std::vector<double> rpopt = g_fitter.run_nonlinear_fit_with_bounds(
                t_fit, y_fit, prior_params, auto_res.sd_base,
                g_fitter.min_amp, g_fitter.max_amp,
                0.5 * median_t2p, 1.5 * median_t2p,
                0.5 * median_fwhm, 1.5 * median_fwhm, refit_ok);
            if (!refit_ok || rpopt.size() < 4) continue;

            double act_max_t2p = 1.5 * median_t2p;
            double act_max_fwhm = 1.5 * median_fwhm;
            std::string rqc = BolusFitter::determine_qc_flag(
                rpopt[0], rpopt[1], rpopt[2], rpopt[3], rpopt[0] / auto_res.sd_base,
                g_fitter.min_amp, g_fitter.max_amp, 0.5 * median_t2p, act_max_t2p,
                0.5 * median_fwhm, act_max_fwhm, refit_ok, false);

            bool improvement = (rqc == "PASS" && rec.qc_flag != "PASS") ||
                              (rqc == "WARN" && rec.qc_flag == "FAIL") ||
                              (rqc == "WARN" && rec.qc_flag == "WARN" &&
                               (rec.f_t2p < 0.1 || rec.f_fwhm < 0.5) &&
                               rpopt[1] >= 0.1 && rpopt[2] >= 0.5);
            if (!improvement) continue;

            rec.f_amp = rpopt[0]; rec.f_t2p = rpopt[1]; rec.f_fwhm = rpopt[2]; rec.f_m = rpopt[3];
            rec.f_cnr = rpopt[0] / auto_res.sd_base; rec.f_snr = rpopt[3] / auto_res.sd_base;
            rec.qc_flag = rqc;
            rec.fit_source = "population_prior";

            // Recompute kinetics for refit
            std::vector<double> y_fit_model(t_fit.size());
            for (size_t i = 0; i < t_fit.size(); ++i)
                y_fit_model[i] = evaluate_gamma_model(t_fit[i], rpopt[0], rpopt[1], rpopt[2], rpopt[3]);
            double mn = y_fit_model[0], mx = y_fit_model[0];
            for (double v : y_fit_model) { if (v < mn) mn = v; if (v > mx) mx = v; }
            double rng = mx - mn;
            double syn = 0; for (double v : y_fit_model) syn += (rng > 0) ? (v - mn) / rng : 0;
            double fyn = (rng > 0) ? (y_fit_model.front() - mn) / rng : 0;
            double lyn = (rng > 0) ? (y_fit_model.back() - mn) / rng : 0;
            rec.aucn = syn - (fyn + lyn) / 2.0;
            double sum_y = 0; for (double v : y_fit_model) sum_y += v;
            rec.auc = sum_y - (y_fit_model.front() + y_fit_model.back()) / 2.0;
            std::vector<int> I;
            for (size_t i = 0; i < y_fit_model.size(); ++i) {
                double vn = (rng > 0) ? (y_fit_model[i] - mn) / rng : 0;
                if (vn < 0.1) I.push_back(i);
            }
            int oidx = 0;
            if (!I.empty()) {
                int li = -1;
                for (size_t k = 0; k + 1 < I.size(); ++k) { if (I[k+1] - I[k] == 1) li = k; }
                oidx = (li != -1) ? I[li] + 1 : I[0];
            }
            rec.ont = (double)oidx / (g_fr * g_upsample_factor);
            rec.ttm = std::abs(rpopt[1] - rec.ont);
            double ssr = 0;
            for (size_t i = 0; i < y_fit.size(); ++i) { double d = y_fit[i] - y_fit_model[i]; ssr += d*d; }
            double mse = (y_fit.size() > 4) ? ssr / (y_fit.size() - 4) : 0;
            auto se = g_fitter.get_parameter_se(t_fit, rpopt, mse);
            rec.ttlb = std::abs((rpopt[1] - 1.96 * se[1]) - rec.ont);
            rec.tthb = std::abs((rpopt[1] + 1.96 * se[1]) - rec.ont);
            rec.ves_type = BolusFitter::suggest_vessel_type(rec.ont, rec.f_t2p, rec.f_fwhm, rec.f_amp, rec.qc_flag);
        }
    }

    // ── Pass 3: Capillary Stall Heuristics ──
    // (matches dataset_processor.cpp:569-617, using configurable g_stall_settings)
    std::vector<double> stall_t2ps, stall_fwhms, stall_onts;
    for (const auto& rec : g_records) {
        if (rec.qc_flag == "PASS") {
            stall_t2ps.push_back(rec.f_t2p);
            stall_fwhms.push_back(rec.f_fwhm);
            stall_onts.push_back(rec.ont);
        }
    }
    double s_med_t2p = stall_t2ps.empty() ? 3.0 : SignalProcessor::compute_median(stall_t2ps);
    double s_med_fwhm = stall_fwhms.empty() ? 5.0 : SignalProcessor::compute_median(stall_fwhms);
    double s_median_ont = stall_onts.empty() ? 0.0 : SignalProcessor::compute_median(stall_onts);

    for (auto& rec : g_records) {
        bool is_stall = false;
        if (!std::isnan(rec.ont) && !std::isnan(rec.f_t2p)) {
            bool late_ont = (rec.ont > s_median_ont + g_stall_settings.ont_offset) ||
                           (s_median_ont > 0.0 && rec.ont > g_stall_settings.ont_mult * s_median_ont);
            bool slow_transit = (rec.f_t2p > g_stall_settings.t2p_mult * s_med_t2p) ||
                               (rec.f_t2p > g_stall_settings.t2p_abs);
            if (late_ont && slow_transit) is_stall = true;
        }
        if (!std::isnan(rec.raw_sd_base) && rec.raw_sd_base > g_stall_settings.sd_base) is_stall = true;
        if (!std::isnan(rec.f_t2p) && !std::isnan(rec.f_fwhm)) {
            if (rec.f_t2p < g_stall_settings.step_t2p && rec.f_fwhm > g_stall_settings.step_fwhm) is_stall = true;
        }
        if (is_stall) {
            rec.stall_flag = 1;
            rec.qc_flag = "STALL";
            rec.ves_type = "S";
        }
    }

    // ── Scan-corrected onset ──
    double min_ont = 999999.0;
    for (const auto& rec : g_records) {
        if (!std::isnan(rec.ont) && rec.ont < min_ont) min_ont = rec.ont;
    }
    for (auto& rec : g_records) {
        if (!std::isnan(rec.ont) && min_ont < 99999.0) {
            rec.ont_sc = rec.ont - min_ont;
        } else {
            rec.ont_sc = NAN;
        }
    }

    // Count results
    pass_count = warn_count = fail_count = stall_count = 0;
    for (const auto& rec : g_records) {
        if (rec.qc_flag == "PASS") pass_count++;
        else if (rec.qc_flag == "WARN") warn_count++;
        else if (rec.qc_flag == "STALL") stall_count++;
        else fail_count++;
    }

    json records = json::array();
    for (const auto& rec : g_records) {
        records.push_back(record_to_json(rec));
    }

    return json{{"ok", true}, {"data", {
        {"total", (int)g_records.size()},
        {"pass", pass_count}, {"warn", warn_count},
        {"fail", fail_count}, {"stall", stall_count},
        {"records", records}
    }}};
}


static json handle_get_fit_limits(const json& /*params*/) {
    return json{{"ok", true}, {"data", {
        {"min_amp",  g_fitter.min_amp},
        {"max_amp",  g_fitter.max_amp},
        {"min_t2p",  g_fitter.min_t2p},
        {"max_t2p",  g_fitter.max_t2p},
        {"min_fwhm", g_fitter.min_fwhm},
        {"max_fwhm", g_fitter.max_fwhm},
        {"auto_max_t2p",  g_fitter.max_t2p >= 1e5},
        {"auto_max_fwhm", g_fitter.max_fwhm >= 1e5}
    }}};
}

static json handle_set_fit_limits(const json& params) {
    if (params.contains("min_amp"))  g_fitter.min_amp  = params["min_amp"].get<double>();
    if (params.contains("max_amp"))  g_fitter.max_amp  = params["max_amp"].get<double>();
    if (params.contains("min_t2p"))  g_fitter.min_t2p  = params["min_t2p"].get<double>();
    if (params.contains("max_t2p"))  g_fitter.max_t2p  = params["max_t2p"].get<double>();
    if (params.contains("min_fwhm")) g_fitter.min_fwhm = params["min_fwhm"].get<double>();
    if (params.contains("max_fwhm")) g_fitter.max_fwhm = params["max_fwhm"].get<double>();
    return handle_get_fit_limits(params); // echo back current values
}

static json handle_parse_framerate(const json& params) {
    std::string path = params.at("path").get<std::string>();
    // If path is a directory, find the framerate text file inside
    path = resolve_path(path, find_framerate_file_in_folder);
    if (path.empty()) {
        return json{{"ok", true}, {"data", {{"framerate", 9.39}}}};  // default
    }
    double fr = parse_frame_rate_from_meta(path);
    return json{{"ok", true}, {"data", {{"framerate", fr}}}};
}

static json handle_convert_mat(const json& params) {
    std::string mat_path = params.at("path").get<std::string>();
    auto rois = MatParser::load_rois_from_mat(mat_path);
    if (rois.empty()) {
        return json{{"ok", false}, {"error", "No ROIs found in MAT file"}};
    }
    // Determine output path
    std::string stem = std::filesystem::path(mat_path).stem().string();
    std::string parent = std::filesystem::path(mat_path).parent_path().string();
    std::string out_path = parent + "/" + stem + "_rois.txt";

    bool force = params.value("force", false);
    if (std::filesystem::exists(out_path) && !force) {
        return json{{"ok", false}, {"error", "Output file already exists: " + out_path}};
    }

    bool ok = write_rois_txt(out_path, rois);
    return json{{"ok", ok}, {"data", {{"output_path", out_path}, {"n_rois", rois.size()}}}};
}

static json handle_ping(const json&) {
    return json{{"ok", true}, {"data", {{"version", "1.0.0"}, {"status", "ready"}}}};
}

// ============================================================================
// Main IPC Loop
// ============================================================================

int main() {
    TIFFSetWarningHandler(nullptr);

    // Send ready signal
    json ready_msg = {{"id", 0}, {"ok", true}, {"data", {{"status", "ready"}, {"version", "1.0.0"}}}};
    std::cout << ready_msg.dump() << std::endl;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        json response;
        int req_id = 0;

        try {
            json request = json::parse(line);
            req_id = request.value("id", 0);
            std::string action = request.at("action").get<std::string>();
            json params = request.value("params", json::object());

            json result;
            if (action == "ping")              result = handle_ping(params);
            else if (action == "scan_folder")  result = handle_scan_folder(params);
            else if (action == "load_tiff")    result = handle_load_tiff(params);
            else if (action == "load_rois")    result = handle_load_rois(params);
            else if (action == "load_csv")     result = handle_load_csv(params);
            else if (action == "save_csv")     result = handle_save_csv(params);
            else if (action == "compute_traces") result = handle_compute_traces(params);
            else if (action == "get_trace")    result = handle_get_trace(params);
            else if (action == "render_plot")  result = handle_render_plot(params);
            else if (action == "auto_estimate") result = handle_auto_estimate(params);
            else if (action == "compute_baseline") result = handle_compute_baseline(params);
            else if (action == "run_fit")      result = handle_run_fit(params);
            else if (action == "parse_framerate") result = handle_parse_framerate(params);
            else if (action == "convert_mat")  result = handle_convert_mat(params);
            else if (action == "batch_fit")    result = handle_batch_fit(params);
            else if (action == "get_fit_limits") result = handle_get_fit_limits(params);
            else if (action == "set_fit_limits") result = handle_set_fit_limits(params);
            else {
                result = json{{"ok", false}, {"error", "Unknown action: " + action}};
            }

            response = result;
            response["id"] = req_id;

        } catch (const json::exception& e) {
            response = json{{"id", req_id}, {"ok", false}, {"error", std::string("JSON error: ") + e.what()}};
        } catch (const std::exception& e) {
            response = json{{"id", req_id}, {"ok", false}, {"error", std::string("Error: ") + e.what()}};
        }

        std::cout << response.dump() << std::endl;
    }

    return 0;
}
