#include "bolus_tracking_cpp.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <tiffio.h>

#if !defined(BUILD_TESTS) && !defined(BUILD_GUI)
int main(int argc, char** argv) {
    TIFFSetWarningHandler(nullptr);
    bool enable_plots = false;
    bool folder_mode = false;
    std::string folder_path = "";
    double drift_window = 15.0;
    bool verbose = false;
    bool preflight_mode = false;
    std::vector<std::string> pos_args;
    
    double min_amp = 1e-6;
    double max_amp = 1023.0; // microscope 10-bit max value
    double min_t2p = 1e-6;
    double max_t2p = 1e6;   // dynamically capped inside fit function
    double min_fwhm = 0.5;   // physiological minimum duration (seconds)
    double max_fwhm = 1e6;   // dynamically capped inside fit function
    
    QCSettings qc_settings;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--plot") {
            enable_plots = true;
        } else if (arg == "--folder") {
            folder_mode = true;
            if (i + 1 < argc) {
                folder_path = argv[i + 1];
                i++;
            }
        } else if (arg == "--drift" || arg == "--drift-window") {
            if (i + 1 < argc) {
                drift_window = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--min-amp") {
            if (i + 1 < argc) {
                min_amp = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--max-amp") {
            if (i + 1 < argc) {
                max_amp = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--min-t2p") {
            if (i + 1 < argc) {
                min_t2p = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--max-t2p") {
            if (i + 1 < argc) {
                max_t2p = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--min-fwhm") {
            if (i + 1 < argc) {
                min_fwhm = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--max-fwhm") {
            if (i + 1 < argc) {
                max_fwhm = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--qc-cnr-min") {
            if (i + 1 < argc) {
                qc_settings.cnr_min = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--qc-fwhm-max") {
            if (i + 1 < argc) {
                qc_settings.fwhm_max = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--qc-t2p-max") {
            if (i + 1 < argc) {
                qc_settings.t2p_max = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--qc-cnr-fail") {
            if (i + 1 < argc) {
                qc_settings.cnr_fail = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--qc-fwhm-fail") {
            if (i + 1 < argc) {
                qc_settings.fwhm_fail = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--qc-t2p-fail") {
            if (i + 1 < argc) {
                qc_settings.t2p_fail = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--qc-amp-fail") {
            if (i + 1 < argc) {
                qc_settings.amp_fail = std::stod(argv[i + 1]);
                i++;
            }
        } else if (arg == "--verbose" || arg == "--debug") {
            verbose = true;
        } else if (arg == "--preflight" || arg == "--validate") {
            preflight_mode = true;
        } else {
            pos_args.push_back(arg);
        }
    }
    
    BolusFitter fitter(min_amp, max_amp, min_t2p, max_t2p, min_fwhm, max_fwhm, verbose);
    
    if (preflight_mode) {
        if (folder_path.empty()) {
            if (!pos_args.empty()) {
                folder_path = pos_args[0];
            } else {
                folder_path = ".";
            }
        }
        if (!std::filesystem::exists(folder_path)) {
            std::cerr << "Folder does not exist: " << folder_path << std::endl;
            return 1;
        }
        BatchProcessor batch_processor(folder_path, drift_window, enable_plots, fitter, qc_settings);
        bool has_warns = false;
        bool has_errs = false;
        batch_processor.run_preflight_scan(has_warns, has_errs);
        return has_errs ? 1 : 0;
    }
    
    if (folder_mode) {
        if (folder_path.empty()) {
            std::cerr << "Error: --folder requires a path." << std::endl;
            return 1;
        }
        if (!std::filesystem::exists(folder_path)) {
            std::cerr << "Folder does not exist: " << folder_path << std::endl;
            return 1;
        }
        
        BatchProcessor batch_processor(folder_path, drift_window, enable_plots, fitter, qc_settings);
        bool run_success = batch_processor.run();
        return run_success ? 0 : 1;
    }
    
    if (pos_args.size() >= 5) {
        std::string tiff_path = pos_args[0];
        std::string rois_path = pos_args[1];
        double fr = std::stod(pos_args[2]);
        int up_f = std::stoi(pos_args[3]);
        std::string out_csv = pos_args[4];
        
        DatasetProcessor ds_processor(drift_window, enable_plots, fitter, qc_settings);
        bool success = ds_processor.process_dataset_file(tiff_path, rois_path, fr, up_f, out_csv);
        return success ? 0 : 1;
    }
    
    std::cerr << "Usage for single file:\n  " << argv[0] << " <tiff_path> <rois_txt_path> <fr> <up_f> <out_csv_path> [--plot] [--drift <seconds>] [--min-amp <val>] [--max-amp <val>] [--min-t2p <val>] [--max-t2p <val>] [--min-fwhm <val>] [--max-fwhm <val>] [qc_options...]\n"
              << "Usage for folder batch processing:\n  " << argv[0] << " --folder <path_to_folder> [--plot] [--drift <seconds>] [bounds_options...] [qc_options...]\n\n"
              << "Bounds Options (Defaults):\n"
              << "  --min-amp (1e-6)   --max-amp (1023.0)\n"
              << "  --min-t2p (1e-6)   --max-t2p (dynamically set)\n"
              << "  --min-fwhm (0.5)   --max-fwhm (dynamically set)\n\n"
              << "QC Options (Defaults):\n"
              << "  --qc-cnr-min (5.0)   --qc-fwhm-max (15.0)   --qc-t2p-max (10.0)\n"
              << "  --qc-cnr-fail (3.0)  --qc-fwhm-fail (100.0)  --qc-t2p-fail (50.0)  --qc-amp-fail (1.0)\n" << std::endl;
    return 1;
}
#endif
