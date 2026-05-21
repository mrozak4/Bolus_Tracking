# Capillary Bolus Tracking & Gamma Curve Fitting Studio

Welcome to the **Capillary Bolus Tracking & Gamma Curve Fitting** repository. This project is a comprehensive suite of MATLAB, Python, and C++ tools designed to extract, analyze, and mathematically model fluorescent dye transit kinetics in brain capillaries for neurovascular coupling research.

---

## 1. Project Overview

During functional imaging experiments, boluses of fluorescent dye are injected into the vasculature. By tracking the Mean Fluorescence Intensity (MFI) within capillary Regions of Interest (ROIs), we can measure transit kinetics. 

This repository provides tools to:
1. **Denoise and Upsample** raw MFI time-series data.
2. **Estimate Timing Markers**: Automate the identification of bolus onset, peak amplitude, and transit end (first-pass clearance) using robust derivative-based heuristics.
3. **Fit Gamma Variate Functions**: Run two-pass robust curve fitting (Linear least-squares followed by Cauchy IRLS) to compute transit properties like Amplitude, Time to Peak ($T_{2p}$), Full Width at Half Maximum (FWHM), and Area Under the Curve (AUC).
4. **Compare Parity**: Match automated Python fits with legacy MATLAB output.
5. **Manually Edit Fits**: Interactively adjust fits using modern graphical user interfaces.

> [!TIP]
> **Preferred Pipeline**: While we maintain a reference Python solver, we highly recommend using the parallelized **C++ implementation** for cohort-scale batch processing. It executes **~11x faster** by parallelizing calculations across all CPU cores.

---

## 2. Directory and Sub-README Guide

We provide detailed documentation for each part of the codebase. Please refer to the files below depending on your workflow:

* **[RUNNING_GUIDE.md](RUNNING_GUIDE.md)**: **Start Here.** A complete, beginner-friendly guide for setting up and running the parallel C++ batch pipeline and launching the interactive C++ GUI. Covers macOS, Linux, Windows, and Docker.
* **[INSTALL.md](INSTALL.md)**: **Installation Instructions.** Setup, building, packaging, and installing the GUI application (including macOS app bundles and custom icons).
* **[PARITY_REPORT.md](PARITY_REPORT.md)**: **Performance & Parity Report.** Compares Python vs. C++ implementation execution speeds, numerical outcomes, and provides recommendations.
* **[README_Python_Pipeline.md](README_Python_Pipeline.md)**: **Python Reference Guide.** Details the Python implementation, custom loss functions (Cauchy robust weightings), Python GUI, local Python running instructions, and numerical optimization settings.
* **[README_BolusAnalysis.md](README_BolusAnalysis.md)**: Standard overview of the bolus analysis workflow and math models.
* **[README_ApplyRegistrationToMask.md](README_ApplyRegistrationToMask.md)**: Describes the MATLAB image registration script (`ApplyRegistrationToMask.m`) used to warp ROI masks to match moving spatial coords over time.

---

## 3. Codebase File Structure

### C++-Based Parallel & GUI Tools (Recommended)
* **`run_pipeline_cpp.sh`**: Command-line wrapper script that compiles and executes the parallel C++ pipeline inside Docker or locally.
* **`bolus_tracking_cpp.cpp`**: Core C++ source code implementing spline upsampling, Levenberg-Marquardt robust curve fitting (via Eigen), and multi-threaded processing.
* **`bolus_tracking_cpp.hpp`**: C++ header file containing the object-oriented structure of the code.
* **`bolus_gui.cpp`**: Cross-platform interactive C++ GUI built with Dear ImGui and ImPlot to manually inspect and correct fits.
* **`test_bolus_tracking_cpp.cpp`**: C++ test suite verifying all math routines, fit models, and edge cases.
* **`CMakeLists.txt`**: C++ build configuration file.
* **`Dockerfile.cpp`**: Docker configuration for compiling, testing, and containerizing the C++ pipeline.

### Python-Based Reference & GUI Tools
* **`bolus_gui.py`**: A premium, interactive Python interface to visually browse datasets, select ROIs, click on plots to adjust markers, run fits, and save results.
* **`run_pipeline.sh`**: Command-line wrapper script that sets up python virtual environments and executes the reference Python batch pipeline.
* **`batch_process.py`**: Scans folders for triplets (TIFF image, MAT mask, metadata TXT), extracts traces, runs fitting, and saves CSVs.
* **`bolus_tracking.py`**: Core mathematical algorithms containing filtering, upsampling, peak/onset/end detections, and curve fitting.
* **`test_bolus_parity.py`**: Parity test suite verifying Python vs. MATLAB numerical outputs.

### MATLAB-Based Legacy Tools
* **`BolusTrack_InteractiveEdit.m`**: Interactive MATLAB GUI for visualizing and manually clicking bolus curves.
* **`gammaFun.m`**: Standard MATLAB implementation of the Gamma variate model.
* **`ApplyRegistrationToMask.m` & `GlobalShiftMask.m`**: Tools for aligning ROIs across registered images.
* **`calcFWHM.m`, `denoiseTrace.m`, `findMaskObjInData.m`, `parseFrameRateFromMetadata.m`**: Auxiliary MATLAB functions.

---

## 4. Quick Start Summary

For detailed instructions, see the **[RUNNING_GUIDE.md](RUNNING_GUIDE.md)**.

### Run Automated Batch Pipeline (Preferred: C++ Parallel Solver)
To build and run the high-performance parallelized C++ pipeline via Docker:
```bash
bash run_pipeline_cpp.sh
```
This processes all subject datasets in parallel. To run the reference Python pipeline instead, run:
```bash
bash run_pipeline.sh
```

### Launch C++ GUI

#### On macOS (Clickable App Bundle)
Build, package, and install the GUI as a native macOS app to Launchpad:
```bash
bash install_macos.sh
open /Applications/BolusTrackingStudio.app  # Or open ~/Applications/BolusTrackingStudio.app
```

#### On Linux, Windows, or Manual macOS Setup
Build and run the executable locally:
```bash
mkdir -p build && cd build
cmake -DBUILD_GUI=ON ..
make -j4
./bolus_tracking_gui
```
This launches the high-performance Dear ImGui dashboard. You can select folders, triage ROIs by status (PASS/WARN/FAIL), drag vertical markers to adjust onset, peak, and end points, crop the fitting window on the fly, automatically save and resume your workflow progress via sidecar `.gui_state` files, and export parameters.

### Launch Python GUI
Run:
```bash
.venv/bin/python bolus_gui.py
```
This opens the Tkinter window to select datasets, navigate capillary ROIs, adjust onset/peak/end points by clicking on the graph, and export the results.

---

## 5. Output Data Format

The batch pipelines output CSV files containing the following metrics for each capillary ROI:

| Column | Description |
| :--- | :--- |
| **ROI** | 1-indexed ID of the capillary Region of Interest. |
| **SubjNum** | Subject ID parsed from the data path. |
| **Exp** | Experiment condition name (e.g. `bolus1_baseline`). |
| **InitAmp** / **F_Amp** | Initial estimate and fitted peak amplitude of the bolus (in arbitrary Intensity **SU**). |
| **InitT2p** / **F_T2p** | Initial estimate and fitted Time-to-Peak (in **seconds**). |
| **InitFWHM** / **F_FWHM** | Initial estimate and fitted Full Width at Half Maximum (in **seconds**). |
| **InitM** / **F_M** | Initial estimate and fitted baseline mean value (in **SU**). |
| **InitSNR** / **F_SNR** | Baseline Signal-to-Noise Ratio ($\text{Baseline Mean} / \text{Baseline Noise SD}$). |
| **InitCNR** / **F_CNR** | Contrast-to-Noise Ratio ($\text{Bolus Amplitude} / \text{Baseline Noise SD}$). |
| **Click1_Start_T** | Time of the baseline start marker (in **seconds**). |
| **Click2_Onset_T** | Time of bolus onset (in **seconds**). |
| **Click3_Peak_T** | Time of the bolus peak (in **seconds**). |
| **Click4_End_T** | Time of first-pass clearance / end marker (in **seconds**). |
| **AUC** | Trapezoidal integration of the fitted bolus curve. |
| **AUCn** | Trapezoidal integration of the min-max normalized fitted bolus curve. |
| **TTlb** | Transit Time Lower Bound (95% CI lower limit of Time-to-Peak relative to Onset). |
| **TTm** | Peak Transit Time (Time-to-Peak relative to Onset). |
| **TThb** | Transit Time Higher Bound (95% CI upper limit of Time-to-Peak relative to Onset). |
| **OnT** | Onset Time (relative to fit window start, where normalized fit crosses 0.1). |
| **OnTSc** | Onset Time in Scan (relative to the earliest onset across all ROIs in the scan). |
| **ROISize** | Size of the Region of Interest mask (in pixels). |
| **Denoise_RMS** | Root Mean Square (RMS) of the noise removed during spline denoising (in **SU**). |
| **VesType** | Vessel type classification (`A` for Arteriole, `V` for Venule, `C` for Capillary, `U` for Unknown). |
| **QC_Flag** | Quality control flag (`PASS`, `WARN`, or `FAIL`). |
| **Fit_Source** | Origin of the fit parameters (`auto`, `population_prior`, or `manual`). |

---

## 6. Physiological Fit Parameter Constraints & Quality Control

To prevent non-physical fits (e.g. infinite/negative values, extremely slow bolus peaks, or amplitude levels exceeding hardware limits), the pipeline implements strict, physiologically informed parameter bounds and QC checks.

### Default Constraint Bounds:
* **Amplitude (`F_Amp`)**: Constrained between `1e-6` and `1023.0`. The upper bound of `1023.0` corresponds to the maximum dynamic range of the 10-bit microscope digitizer.
* **Time-to-Peak (`F_T2p`)**: Constrained between `1e-6` and the **duration of the fit window** (automatically computed from the onset to end of the trace segment). This ensures the peak is found within the actual scan window.
* **FWHM (`F_FWHM`)**: Constrained between `0.5` seconds and the **duration of the fit window**. The lower bound of `0.5` seconds represents the fastest physiologically plausible transit speed of dye through a capillary.
* **Baseline shift (`F_M`)**: Dynamically constrained based on the estimated baseline noise standard deviation to prevent optimizer divergence.

### Quality Control Triage Status (`QC_Flag`):
- **`PASS`**: The fit completed successfully, parameters did not land within 1% of absolute solver bounds, $F\_CNR > 5.0$, $F\_FWHM \in [0.5, 15.0]\text{ s}$, and $F\_T2p \in [0.1, 10.0]\text{ s}$.
- **`WARN`**: The fit succeeded, but one or more parameters crossed the warning limits, $F\_CNR \in [3.0, 5.0]$, or one parameter is near a fitting solver boundary.
- **`FAIL`**: The fit diverged, returned `NaN`, or had a $F\_CNR < 3.0$.

### Kinetics-Based Vessel Type Suggestions (`VesType`):
Fits flagged as valid are classified using calculated kinetics metrics:
- **Arteriole (`A`)**: Early onset ($OnT < 1.8\text{ s}$) and rapid transit time ($TTm < 3.0\text{ s}$).
- **Venule (`V`)**: Late onset ($OnT > 3.0\text{ s}$) or prolonged transit time ($TTm > 4.5\text{ s}$).
- **Capillary (`C`)**: Fits falling into intermediate ranges.
- **Unknown (`U`)**: Failed or `NaN` fits.

These boundaries are automatically applied across the C++ pipeline, the Python batch process script, and the interactive GUI. Override options are available via CLI flags (e.g. `--min-t2p`, `--max-amp`, etc.).

