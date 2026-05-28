# Bolus Tracking: Python Pipeline

**[English](README_Python_Pipeline.md) | [Français (Québec)](README_Python_Pipeline_FR.md)**

> ⚠️ **DEPRECATED — Legacy Reference Only.** The Python pipeline is no longer the recommended workflow. The **C++ parallel pipeline** (`bolus_tracking_cpp`) is the primary, production-grade implementation — it is ~11× faster and supports the full QC triage workflow via the **native C++ Bolus Tracking Studio** GUI. See the **[RUNNING_GUIDE.md](RUNNING_GUIDE.md)** for current instructions.
>
> The content below is retained as a reference for users who need to prototype with Python or extend the pipeline with custom scripts.

---

This document explains how to run the legacy Python bolus tracking pipeline. This pipeline originally replaced the manual MATLAB GUI workflow, allowing for headless batch processing on any system (including your Linux registration machine). **For new projects, use the C++ pipeline instead.**

## Prerequisites

Before running the Python scripts, you need to convert your older MATLAB `MaskObj.mat` files into a clean format that Python can read.

1. **Convert the Masks (One-Time Step):**
   Open MATLAB in this directory and run the conversion script:
   ```matlab
   run('matlab/convert_masks_for_python.m')
   ```
   This will automatically find every `MaskObj.mat` file in your folders and create a duplicate file named `adjusted_<OriginalName>.mat`. **You will use these `adjusted_*.mat` files for the Python pipeline.**

## Setup

The pipeline is now fully Dockerized! You do **not** need to install Python dependencies locally if you don't want to. 

If you just run the `run_pipeline.sh` script, it will automatically build the Docker container using the pinned versions in `requirements.txt` and run the data processing inside it. 

*(If you prefer to run it locally without Docker, you can still use the `python3 -m venv .venv` approach and install the dependencies from `requirements.txt`).*

## Running the Pipeline

The main script is `python/src/batch_process.py`. It takes a registered TIFF stack, an adjusted mask `.mat` file, and a `.txt` metadata file as inputs.

### Usage
You can either provide a specific folder to auto-detect all matching files, or specify them individually.

**To auto-detect files in a folder:**
```bash
python python/src/batch_process.py --folder <path_to_folder> --outdir <output_directory>
```
*(The script will automatically find and pair the TIFFs, Metadata `.txt` files, and `adjusted_*.mat` masks based on the `bolusX_condition` naming convention).*

**To specify files individually:**
```bash
python python/src/batch_process.py --tiff <path_to_tif> --mask <path_to_adjusted_mat> --meta <path_to_txt> --outdir <output_directory>
```

### Example (Dockerized Script)
Here is how you would process the entire `sample-subject-2259` dataset automatically using the new script:

```bash
./run_pipeline.sh sample-subject-2259
```
This script will:
1. Run MATLAB in the background to convert the masks
2. Build the Docker container (if it hasn't been built yet)
3. Pass the folder into the Docker container and run the Python analysis
4. Output the `.csv` result files directly to your current directory!

### Manual Example (Local Python)
If you are running the python script manually (without the `.sh` script), you can use:

*(Note: You were getting a `FileNotFoundError` earlier because the example command had the placeholder name `adjusted_maskObj.mat` instead of the actual file name `adjusted_3554_bolus1_baseline_shifted_MaskObj.mat`).*

## Output
The script will output a CSV file (e.g., `3554_bolus1_baseline_123-300_shifted_results.csv`) containing the automatically estimated and fitted parameters for every ROI mask in the stack. Output columns align with the C++ pipeline:
- `InitAmp`, `InitT2p`, `InitFWHM`, `InitM`, `InitSNR`, `InitCNR` (Initial guesses and metrics)
- `F_Amp`, `F_T2p`, `F_FWHM`, `F_M`, `F_SNR`, `F_CNR` (Final fitted Gamma parameters and metrics)
- `AUC`, `AUCn`, `OnT`, `OnTSc`, `TTlb`, `TTm`, `TThb` (Calculated kinetics: Area under curves, onset time, transit times, and confidence bounds)
- `QC_Flag`, `Fit_Source`, `VesType` (Fit quality classification, source origin of the parameters, and kinetics-based suggested vessel classification)

---

## Interactive Python GUI (Tkinter & Matplotlib Studio)

> ⚠️ **DEPRECATED.** The Python tkinter GUI is no longer maintained. Use the **native C++ Bolus Tracking Studio** instead — download the pre-built app from [GitHub Releases](https://github.com/mrozak4/Bolus_Tracking/releases/latest) or build from source:
> ```bash
> mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . --target bolus_tracking_gui -j8
> ```

<details>
<summary>Legacy Python GUI Instructions (for reference only)</summary>

The Python GUI provides an interactive interface to visually browse datasets, select ROIs, click on plots to adjust markers, run fits, and save results.

### How to Launch the Python GUI:
1. Create a Python virtual environment and install dependencies:
   ```bash
   # macOS / Linux
   python3 -m venv .venv
   source .venv/bin/activate
   pip install -r requirements.txt
   python python/src/bolus_gui.py
   
   # Windows (PowerShell)
   python -m venv .venv
   .venv\Scripts\Activate.ps1
   pip install -r requirements.txt
   python python/src/bolus_gui.py
   ```
2. **GUI Step-by-Step Instructions**:
   - **Load Subject Folder**: Click **📁 Open Subject Folder** and select the subject folder.
   - **Select Dataset**: Choose the dataset triplet from the dropdown.
   - **Select Capillary ROI**: Navigate between the different capillary regions of interest.
   - **Interactive Marker Placement**: Click **Adjust Markers** and click anywhere on the plot to adjust onset/peak/end points visually. The fit will update instantly!
   - **Save & Export**: Click **💾 Save & Export Results** to write parameters to the CSV and save a high-resolution screenshot.

</details>

---

## Python Parameter Constraints Configuration

When running the batch script `python/src/batch_process.py`, you can pass custom bounds to constrain the fitting parameters to physiologically reasonable values:
- `--min-amp` (default: `1e-6`)
- `--max-amp` (default: `1023.0` - matching the 10-bit microscope digitizer limit)
- `--min-t2p` (default: `1e-6`)
- `--max-t2p` (default: dynamically capped to the duration of the fit window)
- `--min-fwhm` (default: `0.5` seconds - matching physiological transit speed limits)
- `--max-fwhm` (default: dynamically capped to the duration of the fit window)

For example, to constrain the time-to-peak ($T_{2p}$) between 2.0 and 8.0 seconds:
```bash
python python/src/batch_process.py --folder sample-subject-2259 --min-t2p 2.0 --max-t2p 8.0
```

### Quality Control Triage Status (`QC_Flag`):
- **`PASS`**: The fit completed successfully, parameters did not land within 1% of absolute solver bounds, $F\_CNR > 5.0$, $F\_FWHM \in [0.5, 15.0]\text{ s}$, and $F\_T2p \in [0.1, 10.0]\text{ s}$.
- **`WARN`**: The fit succeeded, but one or more parameters crossed the warning limits, $F\_CNR \in [3.0, 5.0]$, or one parameter is near a fitting solver boundary (evaluated against relaxed bounds of `Amplitude: [1.0, max_amp]`, `T2p: [0.01, 12.0]`, and `FWHM: [0.1, 20.0]` if a second-pass refit was run).
- **`FAIL`**: The fit diverged, returned `NaN`, or had a $F\_CNR < 3.0$.
