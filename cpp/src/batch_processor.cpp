#include "bolus_tracking_cpp.hpp"
#include "mat_parser.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <map>
#include <regex>
#include <algorithm>
#include <filesystem>
#include <chrono>

// ---------------------------------------------------------
// BatchProcessor Implementation
// ---------------------------------------------------------

/**
 * @brief Constructs a BatchProcessor instance.
 */
BatchProcessor::BatchProcessor(const std::string& folder_path, double drift_window, bool enable_plots, const BolusFitter& fitter, const QCSettings& qc_settings, const StallSettings& stall_settings)
    : folder_path(folder_path), drift_window(drift_window), enable_plots(enable_plots), fitter(fitter), qc_settings(qc_settings), stall_settings(stall_settings) {}

/**
 * @brief Parses the camera frame rate from the Fluoview metadata file format.
 */
double BatchProcessor::parse_frame_rate(const std::string& filepath) const {
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

/**
 * @brief Extracts the logical bolus identifier from a filename.
 */
std::string BatchProcessor::extract_identifier(const std::string& filename) const {
    std::regex re("(bolus\\d+[-_](baseline|co2))", std::regex_constants::icase);
    std::smatch m;
    if (std::regex_search(filename, m, re)) {
        std::string id = m.str(1);
        std::replace(id.begin(), id.end(), '-', '_');
        return id;
    }
    return "";
}

/**
 * @brief Extracts the directory folder relative to the scanning base directory.
 */
std::string BatchProcessor::get_top_relative_dir(const std::filesystem::path& file_path, const std::filesystem::path& base_folder) const {
    try {
        std::filesystem::path abs_file = std::filesystem::absolute(file_path).lexically_normal();
        std::filesystem::path abs_base = std::filesystem::absolute(base_folder).lexically_normal();
        
        std::filesystem::path rel = std::filesystem::relative(abs_file, abs_base);
        auto it = rel.begin();
        if (it != rel.end() && *it != "..") {
            int components = 0;
            for (auto const& c : rel) {
                (void)c; // suppress unused warning
                components++;
            }
            if (components <= 1) {
                return "";
            }
            return it->string();
        }
    } catch (...) {}
    return "";
}

/**
 * @brief Checks if a path string contains any of the pattern directories/files to ignore.
 */
bool BatchProcessor::contains_ignored_pattern(const std::string& path) const {
    std::vector<std::string> ignores = {"mips", "results", "shift_info", "max_"};
    for (const auto& pat : ignores) {
        if (path.find(pat) != std::string::npos) return true;
    }
    return false;
}

/**
 * @brief Runs recursive scanning, pairing, and batch processing for all matching triplets.
 */
bool BatchProcessor::run() const {
    bool pf_warn = false;
    bool pf_err = false;
    run_preflight_scan(pf_warn, pf_err);

    std::cout << "Pure C++ Pipeline - Scanning: " << folder_path << std::endl;
    std::cout << "Drift window duration: " << drift_window << " seconds." << std::endl;
    if (enable_plots) {
        std::cout << "Plotting enabled. Fits will be saved to plots_cpp/ folder." << std::endl;
    } else {
        std::cout << "Plotting disabled. Only results CSVs will be generated." << std::endl;
    }
    
    std::vector<PathInfo> rois_files;
    std::vector<PathInfo> meta_files;
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder_path)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            std::string filename = entry.path().filename().string();
            if (filename.empty() || filename.front() == '.') continue;
            
            std::string identifier = extract_identifier(filename);
            if (identifier.empty()) continue;
            
            std::transform(identifier.begin(), identifier.end(), identifier.begin(), ::tolower);
            
            std::string filename_lower = filename;
            std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);
            
            PathInfo pinfo;
            pinfo.path = entry.path();
            pinfo.identifier = identifier;
            pinfo.top_dir = get_top_relative_dir(entry.path(), folder_path);
            
            if (ext == ".txt") {
                if (filename_lower.find("_rois.txt") != std::string::npos || filename_lower.find("_rois_cpp.txt") != std::string::npos) {
                    rois_files.push_back(pinfo);
                } else if (filename_lower.find("_rois") == std::string::npos) {
                    meta_files.push_back(pinfo);
                }
            } else if (ext == ".mat") {
                if (filename_lower.find("maskobj") != std::string::npos || filename_lower.find("mask") != std::string::npos) {
                    rois_files.push_back(pinfo);
                }
            }
        }
    }
    
    std::vector<std::filesystem::path> tiff_files;
    std::vector<std::string> registered_stems;
    std::vector<std::string> skipped_unregistered;
    std::vector<std::string> skipped_missing;

    // First pass: collect registered stems
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder_path)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".tif" || ext == ".tiff") {
                std::string filename = entry.path().filename().string();
                if (filename.empty() || filename.front() == '.') continue;
                std::string name_lower = filename;
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
                if (name_lower.find("registered") != std::string::npos && !contains_ignored_pattern(name_lower)) {
                    std::string stem = entry.path().stem().string();
                    std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
                    registered_stems.push_back(stem);
                }
            }
        }
    }

    // Second pass: collect TIFF files, skipping unregistered counterparts
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
                    std::string stem = entry.path().stem().string();
                    std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
                    if (stem.find("registered") == std::string::npos) {
                        std::string target_reg = stem + "_registered";
                        if (std::find(registered_stems.begin(), registered_stems.end(), target_reg) != registered_stems.end() ||
                            std::find(registered_stems.begin(), registered_stems.end(), stem + "registered") != registered_stems.end()) {
                            skipped_unregistered.push_back(filename + " (using registered version instead)");
                            continue;
                        }
                    }
                    tiff_files.push_back(entry.path());
                }
            }
        }
    }
    
    std::cout << "Found " << tiff_files.size() << " TIFF files to process." << std::endl;
    int processed_count = 0;
    
    DatasetProcessor ds_processor(drift_window, enable_plots, fitter, qc_settings, stall_settings);
    
    for (const auto& tiff_path : tiff_files) {
        std::string filename = tiff_path.filename().string();
        std::string identifier = extract_identifier(filename);
        if (identifier.empty()) {
            std::cout << "Skipping non-bolus TIFF: " << filename << std::endl;
            continue;
        }
        
        std::string id_lower = identifier;
        std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(), ::tolower);
        
        std::string tif_subj = get_top_relative_dir(tiff_path, folder_path);
        
        std::string rois_file = "";
        std::string meta_file = "";
        
        // 1. Try to find a .txt ROI file
        for (const auto& r : rois_files) {
            std::string r_ext = r.path.extension().string();
            std::transform(r_ext.begin(), r_ext.end(), r_ext.begin(), ::tolower);
            if (r_ext == ".txt" && r.identifier == id_lower) {
                if (tif_subj.empty() || r.top_dir == tif_subj) {
                    rois_file = r.path.string();
                    break;
                }
            }
        }
        // 2. If not found, try to find a .mat ROI file
        if (rois_file.empty()) {
            for (const auto& r : rois_files) {
                std::string r_ext = r.path.extension().string();
                std::transform(r_ext.begin(), r_ext.end(), r_ext.begin(), ::tolower);
                if (r_ext == ".mat" && r.identifier == id_lower) {
                    if (tif_subj.empty() || r.top_dir == tif_subj) {
                        rois_file = r.path.string();
                        break;
                    }
                }
            }
        }
        
        for (const auto& m : meta_files) {
            if (m.identifier == id_lower) {
                if (tif_subj.empty() || m.top_dir == tif_subj) {
                    meta_file = m.path.string();
                    break;
                }
            }
        }
        
        if (rois_file.empty() || meta_file.empty()) {
            std::string reason = "Missing: ";
            if (rois_file.empty()) reason += "ROIs (.mat or _rois.txt) ";
            if (meta_file.empty()) reason += "metadata (.txt) ";
            skipped_missing.push_back(filename + " (" + reason + ")");
            continue;
        }
        
        double fr = parse_frame_rate(meta_file);
        if (fr <= 0.0) {
            skipped_missing.push_back(filename + " (failed to parse frame rate from metadata)");
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
        
        ds_processor.process_dataset_file(tiff_path.string(), rois_file, fr, 20, out_csv);
        processed_count++;
    }
    
    std::cout << "\n==================================================" << std::endl;
    std::cout << "   BATCH PROCESSING EXECUTION SUMMARY             " << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "Successfully Processed: " << processed_count << " datasets." << std::endl;
    std::cout << "Skipped Unregistered TIFFs (registered version used): " << skipped_unregistered.size() << std::endl;
    for (const auto& item : skipped_unregistered) {
        std::cout << "  - " << item << std::endl;
    }
    std::cout << "Skipped Due to Errors/Missing Files: " << skipped_missing.size() << std::endl;
    for (const auto& item : skipped_missing) {
        std::cout << "  - " << item << std::endl;
    }
    std::cout << "==================================================" << std::endl;
    
    return true;
}

/**
 * @brief Performs a validation scan over the target directory and outputs a pairing/sanity report.
 */
bool BatchProcessor::run_preflight_scan(bool& has_warnings, bool& has_errors) const {
    has_warnings = false;
    has_errors = false;
    
    std::cout << "\n==================================================" << std::endl;
    std::cout << "        PRE-FLIGHT PIPELINE VALIDATION            " << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "Scanning folder: " << folder_path << std::endl;

    if (!std::filesystem::exists(folder_path)) {
        std::cerr << "ERROR: Folder does not exist: " << folder_path << std::endl;
        has_errors = true;
        return false;
    }

    // Collect files
    std::vector<std::filesystem::path> all_tifs;
    std::vector<std::filesystem::path> all_rois;
    std::vector<std::filesystem::path> all_metas;
    std::vector<std::string> registered_stems;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder_path)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            std::string filename = entry.path().filename().string();
            if (filename.empty() || filename.front() == '.') continue;
            std::string name_lower = filename;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

            if (ext == ".tif" || ext == ".tiff") {
                if (!contains_ignored_pattern(name_lower)) {
                    all_tifs.push_back(entry.path());
                    if (name_lower.find("registered") != std::string::npos) {
                        std::string stem = entry.path().stem().string();
                        std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
                        registered_stems.push_back(stem);
                    }
                }
            } else if (ext == ".txt") {
                if (name_lower.find("_rois.txt") != std::string::npos || name_lower.find("_rois_cpp.txt") != std::string::npos) {
                    all_rois.push_back(entry.path());
                } else if (name_lower.find("_rois") == std::string::npos) {
                    all_metas.push_back(entry.path());
                }
            } else if (ext == ".mat") {
                if (name_lower.find("maskobj") != std::string::npos || name_lower.find("mask") != std::string::npos) {
                    all_rois.push_back(entry.path());
                }
            }
        }
    }

    // Pair files logically
    // A dataset key is defined by: top_relative_dir + "|" + identifier
    struct DatasetGroup {
        std::string identifier;
        std::string top_dir;
        std::vector<std::filesystem::path> tifs;
        std::vector<std::filesystem::path> rois;
        std::vector<std::filesystem::path> metas;
    };
    std::map<std::string, DatasetGroup> groups;

    auto insert_file = [&](const std::filesystem::path& p, int type) {
        std::string filename = p.filename().string();
        std::string id = extract_identifier(filename);
        if (id.empty()) return;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        std::string top = get_top_relative_dir(p, folder_path);
        std::string key = top + "|" + id;
        if (groups.find(key) == groups.end()) {
            groups[key] = {id, top, {}, {}, {}};
        }
        if (type == 0) groups[key].tifs.push_back(p);
        else if (type == 1) groups[key].rois.push_back(p);
        else if (type == 2) groups[key].metas.push_back(p);
    };

    for (const auto& p : all_tifs) insert_file(p, 0);
    for (const auto& p : all_rois) insert_file(p, 1);
    for (const auto& p : all_metas) insert_file(p, 2);

    int count = 0;
    for (const auto& pair : groups) {
        const auto& g = pair.second;
        count++;
        std::string folder_label = g.top_dir.empty() ? "(Root)" : g.top_dir;
        std::cout << "\nDataset " << count << ": " << g.identifier << " in " << folder_label << std::endl;

        // Check TIFFs
        bool has_reg_tif = false;
        bool has_unreg_tif = false;
        std::filesystem::path reg_tif_path;
        std::filesystem::path unreg_tif_path;

        for (const auto& t : g.tifs) {
            std::string stem = t.stem().string();
            std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
            if (stem.find("registered") != std::string::npos) {
                has_reg_tif = true;
                reg_tif_path = t;
            } else {
                has_unreg_tif = true;
                unreg_tif_path = t;
            }
        }

        if (g.tifs.empty()) {
            std::cout << "  [ERROR] TIFF file is missing!" << std::endl;
            has_errors = true;
        } else {
            for (const auto& t : g.tifs) {
                std::cout << "  -> TIFF: " << t.filename().string() << std::endl;
            }
            if (has_reg_tif && has_unreg_tif) {
                std::cout << "  [WARN] Both registered and unregistered TIFFs exist. Unregistered TIFF (" 
                          << unreg_tif_path.filename().string() << ") will be skipped." << std::endl;
                has_warnings = true;
            }
        }

        // Check ROIs
        if (g.rois.empty()) {
            std::cout << "  [ERROR] ROI Mask file (.mat or _rois.txt) is missing!" << std::endl;
            has_errors = true;
        } else {
            for (const auto& r : g.rois) {
                std::cout << "  -> ROIs: " << r.filename().string() << std::endl;
                // Capitalization warnings on maskObj
                std::string fname = r.filename().string();
                if (r.extension().string() == ".mat") {
                    if (fname.find("MaskObj") == std::string::npos && fname.find("maskObj") == std::string::npos) {
                        std::cout << "  [WARN] MAT file name '" << fname << "' does not explicitly contain 'maskObj' (matching might be fragile)." << std::endl;
                        has_warnings = true;
                    }
                }
            }
        }

        // Check Metadata
        if (g.metas.empty()) {
            std::cout << "  [ERROR] Metadata file (.txt) is missing!" << std::endl;
            has_errors = true;
        } else {
            for (const auto& m : g.metas) {
                double fr = parse_frame_rate(m.string());
                if (fr <= 0.0) {
                    std::cout << "  [ERROR] Metadata " << m.filename().string() << " exists but failed to parse camera frame rate!" << std::endl;
                    has_errors = true;
                } else {
                    std::cout << "  -> Meta: " << m.filename().string() << " (Parsed Frame Rate: " << fr << " Hz)" << std::endl;
                }
            }
        }

        // Check case sensitivity mismatch in spelling
        if (!g.tifs.empty() && !g.metas.empty()) {
            std::string t_name = g.tifs[0].filename().string();
            std::string m_name = g.metas[0].filename().string();
            bool t_co2 = (t_name.find("CO2") != std::string::npos);
            bool m_co2 = (m_name.find("CO2") != std::string::npos);
            bool t_co2_l = (t_name.find("co2") != std::string::npos);
            bool m_co2_l = (m_name.find("co2") != std::string::npos);
            if ((t_co2 && m_co2_l) || (t_co2_l && m_co2)) {
                std::cout << "  [WARN] Case mismatch in condition name (e.g. CO2 vs co2) between TIFF and Metadata. Normalization will handle this but naming consistency is recommended." << std::endl;
                has_warnings = true;
            }
        }
    }

    // Check for completely unmatched/unpaired TIFFs
    for (const auto& t : all_tifs) {
        std::string filename = t.filename().string();
        std::string id = extract_identifier(filename);
        if (id.empty()) {
            std::cout << "\n[WARN] TIFF file does not match expected bolus naming convention (skipped): " << filename << std::endl;
            has_warnings = true;
        }
    }

    std::cout << "\n--------------------------------------------------" << std::endl;
    std::cout << "Pre-flight scan finished. Summary:" << std::endl;
    std::cout << "  Total logical datasets: " << groups.size() << std::endl;
    std::cout << "  Status: ";
    if (has_errors) {
        std::cout << "ERRORS FOUND (Pipeline will fail/skip some datasets)" << std::endl;
    } else if (has_warnings) {
        std::cout << "PASS WITH WARNINGS" << std::endl;
    } else {
        std::cout << "ALL OK" << std::endl;
    }
    std::cout << "==================================================\n" << std::endl;

    return !has_errors;
}

// ---------------------------------------------------------
// write_rois_txt — Free function
// ---------------------------------------------------------

/**
 * @brief Writes a vector of ROI polygons to the pipeline's text format.
 *
 * Format:
 *   <n_rois>
 *   <roi_id> <n_points>
 *   <x1> <y1>
 *   <x2> <y2>
 *   ...
 *
 * This mirrors the output of matlab/convert_masks_for_python.m (lines 71-78).
 */
bool write_rois_txt(const std::string& output_path, const std::vector<ROI>& rois) {
    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "ERROR: Could not open file for writing: " << output_path << std::endl;
        return false;
    }

    out << rois.size() << "\n";
    for (const auto& roi : rois) {
        out << roi.id << " " << roi.poly.size() << "\n";
        for (const auto& pt : roi.poly) {
            out << std::fixed << std::setprecision(6) << pt.first << " " << pt.second << "\n";
        }
    }

    out.close();
    return true;
}

// ---------------------------------------------------------
// BatchProcessor::run_prepare
// ---------------------------------------------------------

/**
 * @brief Scans the subject directory for .mat mask files and converts them to _rois.txt format.
 *
 * @param dry_run If true, only report what would be done without writing files.
 * @param force_overwrite If true, overwrite existing _rois.txt files.
 * @return true if all conversions succeeded (or dry-run completed), false on any errors.
 */
bool BatchProcessor::run_prepare(bool dry_run, bool force_overwrite) const {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "       FILE PREPARATION UTILITY" << std::endl;
    if (dry_run) {
        std::cout << "       MODE: DRY RUN (no files will be written)" << std::endl;
    } else {
        std::cout << "       MODE: APPLY" << (force_overwrite ? " (force overwrite)" : "") << std::endl;
    }
    std::cout << "==================================================" << std::endl;
    std::cout << "Scanning folder: " << folder_path << std::endl;

    if (!std::filesystem::exists(folder_path)) {
        std::cerr << "ERROR: Folder does not exist: " << folder_path << std::endl;
        return false;
    }

    // 1. Collect all .mat mask files
    struct MatFileInfo {
        std::filesystem::path mat_path;
        std::filesystem::path expected_txt_path;
        bool txt_exists;
    };
    std::vector<MatFileInfo> mat_files;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder_path)) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        if (filename.empty() || filename.front() == '.') continue;

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".mat") continue;

        std::string filename_lower = filename;
        std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);

        // Skip non-mask .mat files (e.g. bolus1_shift.mat, adjusted_ files)
        // Only skip files ending with _shift.mat (registration transforms), not files with "shifted" in the name
        if (filename_lower.find("_shift.mat") != std::string::npos &&
            filename_lower.find("shifted") == std::string::npos) continue;
        if (filename.substr(0, 9) == "adjusted_") continue;

        // Match mask patterns: maskObj, mask, MaskObj
        bool is_mask = (filename_lower.find("maskobj") != std::string::npos ||
                        filename_lower.find("mask") != std::string::npos);
        if (!is_mask) continue;

        // Determine the expected _rois.txt output path
        std::filesystem::path parent = entry.path().parent_path();
        std::string stem = entry.path().stem().string();
        std::filesystem::path txt_path = parent / (stem + "_rois.txt");

        MatFileInfo info;
        info.mat_path = entry.path();
        info.expected_txt_path = txt_path;
        info.txt_exists = std::filesystem::exists(txt_path);
        mat_files.push_back(info);
    }

    if (mat_files.empty()) {
        std::cout << "\nNo .mat mask files found in the directory tree." << std::endl;
        std::cout << "Nothing to prepare." << std::endl;
        return true;
    }

    // Sort by path for consistent ordering
    std::sort(mat_files.begin(), mat_files.end(), [](const MatFileInfo& a, const MatFileInfo& b) {
        return a.mat_path < b.mat_path;
    });

    // 2. Process each .mat file
    int converted_count = 0;
    int skipped_count = 0;
    int error_count = 0;
    int overwrite_count = 0;

    std::cout << "\nFound " << mat_files.size() << " .mat mask file(s):\n" << std::endl;

    for (const auto& info : mat_files) {
        std::string rel_mat;
        try {
            rel_mat = std::filesystem::relative(info.mat_path, folder_path).string();
        } catch (...) {
            rel_mat = info.mat_path.filename().string();
        }

        std::string rel_txt;
        try {
            rel_txt = std::filesystem::relative(info.expected_txt_path, folder_path).string();
        } catch (...) {
            rel_txt = info.expected_txt_path.filename().string();
        }

        if (info.txt_exists && !force_overwrite) {
            std::cout << "  [SKIP] " << rel_mat << std::endl;
            std::cout << "         -> " << rel_txt << " already exists" << std::endl;
            skipped_count++;
            continue;
        }

        if (dry_run) {
            if (info.txt_exists && force_overwrite) {
                std::cout << "  [WOULD OVERWRITE] " << rel_mat << std::endl;
            } else {
                std::cout << "  [WOULD CONVERT] " << rel_mat << std::endl;
            }
            std::cout << "         -> " << rel_txt << std::endl;

            // Still validate the .mat file is parseable
            try {
                auto rois = MatParser::load_rois_from_mat(info.mat_path.string());
                if (rois.empty()) {
                    std::cout << "         [WARN] MAT file parsed but contains 0 ROIs" << std::endl;
                } else {
                    std::cout << "         (" << rois.size() << " ROIs detected)" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "         [ERROR] Failed to parse: " << e.what() << std::endl;
                error_count++;
            }
            converted_count++;
            continue;
        }

        // APPLY mode: actually convert
        try {
            auto rois = MatParser::load_rois_from_mat(info.mat_path.string());
            if (rois.empty()) {
                std::cout << "  [WARN] " << rel_mat << " -> 0 ROIs parsed (skipping)" << std::endl;
                error_count++;
                continue;
            }

            bool write_ok = write_rois_txt(info.expected_txt_path.string(), rois);
            if (!write_ok) {
                std::cout << "  [ERROR] " << rel_mat << " -> failed to write " << rel_txt << std::endl;
                error_count++;
                continue;
            }

            if (info.txt_exists) {
                std::cout << "  [OVERWRITE] " << rel_mat << " -> " << rel_txt
                          << " (" << rois.size() << " ROIs)" << std::endl;
                overwrite_count++;
            } else {
                std::cout << "  [CONVERT] " << rel_mat << " -> " << rel_txt
                          << " (" << rois.size() << " ROIs)" << std::endl;
            }
            converted_count++;
        } catch (const std::exception& e) {
            std::cout << "  [ERROR] " << rel_mat << " -> " << e.what() << std::endl;
            error_count++;
        }
    }

    // 3. Summary
    std::cout << "\n--------------------------------------------------" << std::endl;
    std::cout << "Preparation Summary:" << std::endl;
    if (dry_run) {
        std::cout << "  Would convert: " << converted_count << " file(s)" << std::endl;
    } else {
        std::cout << "  Converted:     " << converted_count << " file(s)" << std::endl;
        if (overwrite_count > 0) {
            std::cout << "  Overwritten:   " << overwrite_count << " file(s)" << std::endl;
        }
    }
    std::cout << "  Skipped:       " << skipped_count << " file(s) (already exist)" << std::endl;
    if (error_count > 0) {
        std::cout << "  Errors:        " << error_count << " file(s)" << std::endl;
    }
    std::cout << "==================================================" << std::endl;

    if (dry_run && converted_count > 0) {
        std::cout << "\nThis was a dry run. To actually write files, re-run with --apply:" << std::endl;
        std::cout << "  bolus_tracking_cpp --folder " << folder_path << " --prepare --apply" << std::endl;
    }

    // 4. Run preflight scan to show dataset pairing status after preparation
    if (!dry_run && converted_count > 0) {
        std::cout << "\nRunning post-preparation validation scan...\n" << std::endl;
        bool pf_warn = false, pf_err = false;
        run_preflight_scan(pf_warn, pf_err);
    }

    return error_count == 0;
}
