# Capillary Bolus Tracking & Gamma Curve Fitting Studio

Welcome to the **Capillary Bolus Tracking & Gamma Curve Fitting** repository. This project is a comprehensive suite of MATLAB and Python tools designed to extract, analyze, and mathematically model fluorescent dye transit kinetics in brain capillaries for neurovascular coupling research.

---

## 1. Project Overview

During functional imaging experiments, boluses of fluorescent dye are injected into the vasculature. By tracking the Mean Fluorescence Intensity (MFI) within capillary Regions of Interest (ROIs), we can measure transit kinetics. 

This repository provides tools to:
1. **Denoise and Upsample** raw MFI time-series data.
2. **Estimate Timing Markers**: Automate the identification of bolus onset, peak amplitude, and transit end (first-pass clearance) using robust derivative-based heuristics.
3. **Fit Gamma Variate Functions**: Run two-pass robust curve fitting (Linear least-squares followed by Cauchy IRLS) to compute transit properties like Amplitude, Time to Peak ($T_{2p}$), Full Width at Half Maximum (FWHM), and Area Under the Curve (AUC).
4. **Compare Parity**: Match automated Python fits with legacy MATLAB output.
5. **Manually Edit Fits**: Interactively adjust fits using modern graphical user interfaces.

---

## 2. Directory and Sub-README Guide

We provide detailed documentation for each part of the codebase. Please refer to the files below depending on your workflow:

* **[RUNNING_GUIDE.md](RUNNING_GUIDE.md)**: **Start Here.** A complete, beginner-friendly guide for setting up and running the Python batch pipeline and launching the new interactive GUI. Covers macOS, Linux, Windows, and Docker.
* **[PARITY_REPORT.md](PARITY_REPORT.md)**: **Performance & Parity Report.** Compares Python vs. C++ implementation execution speeds, numerical outcomes, and provides recommendations.
* **[README_Python_Pipeline.md](README_Python_Pipeline.md)**: Details the Python implementation, custom loss functions (Cauchy robust weightings), and numerical optimization settings.
* **[README_BolusAnalysis.md](README_BolusAnalysis.md)**: Standard overview of the bolus analysis workflow and math models.
* **[README_ApplyRegistrationToMask.md](README_ApplyRegistrationToMask.md)**: Describes the MATLAB image registration script (`ApplyRegistrationToMask.m`) used to warp ROI masks to match moving spatial coords over time.

---

## 3. Codebase File Structure

### Python-Based Tools
* **`bolus_gui.py`**: A premium, interactive Python interface to visually browse datasets, select ROIs, click on plots to adjust markers, run fits, and save results.
* **`run_pipeline.sh`**: Command-line wrapper script that sets up python virtual environments and executes the batch pipeline.
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

### Run Automated Batch Pipeline
Simply run:
```bash
bash run_pipeline.sh
```
This script will auto-detect Python, construct a sandbox environment, install dependencies, and run the pipeline on all subjects.

### Launch Python GUI
Run:
```bash
.venv/bin/python bolus_gui.py
```
This opens the window to select datasets, navigate capillary ROIs, adjust onset/peak/end points by clicking on the graph, and export the results.
