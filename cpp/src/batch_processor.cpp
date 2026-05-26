#include "bolus_tracking_cpp.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
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
BatchProcessor::BatchProcessor(const std::string& folder_path, double drift_window, bool enable_plots, const BolusFitter& fitter, const QCSettings& qc_settings)
    : folder_path(folder_path), drift_window(drift_window), enable_plots(enable_plots), fitter(fitter), qc_settings(qc_settings) {}

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
    
    DatasetProcessor ds_processor(drift_window, enable_plots, fitter, qc_settings);
    
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
