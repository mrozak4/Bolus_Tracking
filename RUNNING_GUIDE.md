# Complete Guide to Running the Bolus Tracking Pipeline

**[English](RUNNING_GUIDE.md) | [Français (Québec)](RUNNING_GUIDE_FR.md)**

This guide is designed for **both human users (even with zero coding experience)** and **AI coding agents** to easily set up, run, and maintain the bolus tracking pipeline and interactive GUI.

---

## 1. How to Download the Files from GitHub

You can download the files from GitHub using either of the following two methods:

### Option A: Download as a ZIP File (No Coding Experience Needed)
1. Open your web browser and navigate to the repository page on GitHub.
2. Click the green **`<> Code`** button located at the top-right of the file list.
3. Select **Download ZIP** from the dropdown menu.
4. Once the download is complete, locate the ZIP file on your computer and extract (unzip) it.

### Option B: Clone via Git (For Programmers & AI Agents)
Open your terminal (macOS/Linux) or PowerShell (Windows) and run:
```bash
git clone https://github.com/mrozak4/Bolus_Tracking.git
cd Bolus_Tracking
```

---

## 2. Recommended Workflow: Running the C++ Pipeline with Docker

> [!IMPORTANT]
> **Docker is the highly recommended and preferred way to run the C++ pipeline.**
> Using Docker guarantees that all library versions are identical, prevents dependency conflicts, and requires zero installation of C++ compilers or packages on your system.

### Prerequisites (One-Time Setup)
Make sure you have Docker installed and running on your system:
- **macOS / Windows**: Download and run [Docker Desktop](https://www.docker.com/products/docker-desktop/).
- **Linux (Ubuntu/Debian)**: Run:
  ```bash
  sudo apt-get update && sudo apt-get install docker.io -y
  sudo systemctl start docker && sudo systemctl enable docker
  sudo usermod -aG docker $USER  # Log out and back in after running this
  ```

---

### Running the C++ Parallel Pipeline with Docker
The C++ pipeline is a standalone computational engine designed for maximum speed. It runs completely standalone inside its own Docker container with zero Python overhead.

To process a target folder (e.g. `sample-subject-2259`) using the C++ pipeline with Docker:
```bash
bash run_pipeline_cpp.sh sample-subject-2259
```

> [!TIP]
> **Generating Plots & Drift Correction**: By default, the C++ pipeline only generates the results CSV to maximize speed. To generate publication-quality SVG plots for all capillary ROIs, append the `--plot` flag:
> ```bash
> bash run_pipeline_cpp.sh sample-subject-2259 --plot
> ```
> By default, the baseline linear drift correction uses the first **15.0** seconds. If you want to change this duration (e.g. to 10 seconds), append `--drift 10`:
> ```bash
> bash run_pipeline_cpp.sh sample-subject-2259 --plot --drift 10
> ```
> **Configuring Fit & Quality Limits**: You can override the default physiological parameter bounds and QC thresholds directly from the script arguments (e.g. to enforce a minimum $T_{2p}$ threshold of `2.0` seconds or custom QC CNR warnings):
> ```bash
> # Restrict Time-to-Peak (T2p) and set custom QC CNR warnings
> bash run_pipeline_cpp.sh sample-subject-2259 --min-t2p 2.0 --qc-cnr-min 6.0
> ```
> The SVG plots will be saved to a `plots_cpp` subdirectory within the subject folder.

**Batch Processing Multiple Subjects**: You can process multiple subjects at once by targeting a parent directory that contains multiple subject folders. The pipeline recursively scans and matches the corresponding `.tif`, `_rois.txt`, and metadata files within each subject directory without conflicts:
```bash
# Process all subjects found under the current directory
bash run_pipeline_cpp.sh . --plot
```

#### Manual command:
If you want to run the Docker command manually:
```bash
# 1. Build the C++ image
docker build -t bolus_tracking_cpp -f Dockerfile.cpp .

# 2. Run the C++ processing (maps current directory to /data in the container)
docker run --rm -v "$(pwd):/data" bolus_tracking_cpp --folder /data/sample-subject-2259
```

##### Fit Quality Parameters & Triage Limits:
| Parameter | Description | Warning Threshold (`WARN`) | Failure Threshold (`FAIL`) | Absolute Solver Bounds (Hard Limit) |
| :--- | :--- | :--- | :--- | :--- |
| **Amplitude** | Peak height of the bolus curve | Near solver boundary | — | `[1e-6, 1023.0]` |
| **Time to Peak ($T_{2p}$)** | Duration from onset to peak | `< 0.1 s` or `> 10.0 s` or near boundary | — | `[1e-6, fit window duration]` |
| **FWHM** | Full width of bolus at half max amplitude | `< 0.5 s` or `> 15.0 s` or near boundary | — | `[0.5, fit window duration]` |
| **CNR** | Contrast-to-Noise Ratio (Peak / SD of baseline) | `[3.0, 5.0]` | `< 3.0` | — |

##### Customizing Fit Bounds and QC Thresholds from Command Line
You can configure both the absolute optimization bounds and the warning/failure triage thresholds dynamically when running the C++ parallel pipeline using the following command-line flags:

| CLI Option | Parameter Targeted | Default Value | Description |
| :--- | :--- | :--- | :--- |
| **`--min-amp <val>`** | Absolute Minimum Amplitude | `1e-6` | Hard lower bound for fitting solver amplitude. |
| **`--max-amp <val>`** | Absolute Maximum Amplitude | `1023.0` | Hard upper bound (10-bit dynamic range of digitizer). |
| **`--min-t2p <val>`** | Absolute Minimum $T_{2p}$ | `1e-6` | Hard lower bound for fitting solver time to peak. |
| **`--max-t2p <val>`** | Absolute Maximum $T_{2p}$ | `Scan duration` | Hard upper bound for fitting solver time to peak. |
| **`--min-fwhm <val>`** | Absolute Minimum FWHM | `0.5` | Hard lower bound (fastest plausible capillary transit). |
| **`--max-fwhm <val>`** | Absolute Maximum FWHM | `Scan duration` | Hard upper bound for fitting solver FWHM. |
| **`--qc-amp-fail <val>`** | Amplitude Failure Limit | `1.0` | Fits with amplitude below this are flagged `FAIL`. |
| **`--qc-t2p-max <val>`** | $T_{2p}$ Warning Limit | `10.0` | Fits with $T_{2p}$ above this are flagged `WARN`. |
| **`--qc-t2p-fail <val>`** | $T_{2p}$ Failure Limit | `50.0` | Fits with $T_{2p}$ above this are flagged `FAIL`. |
| **`--qc-fwhm-max <val>`** | FWHM Warning Limit | `15.0` | Fits with FWHM above this are flagged `WARN`. |
| **`--qc-fwhm-fail <val>`** | FWHM Failure Limit | `100.0` | Fits with FWHM above this are flagged `FAIL`. |
| **`--qc-cnr-min <val>`** | CNR Warning Limit | `5.0` | Fits with CNR below this are flagged `WARN`. |
| **`--qc-cnr-fail <val>`** | CNR Failure Limit | `3.0` | Fits with CNR below this are flagged `FAIL`. |

For example, to process a dataset with a custom FWHM warning threshold of `20.0` seconds and a minimum CNR warning threshold of `6.0`:
```bash
bash run_pipeline_cpp.sh sample-subject-2259 --qc-fwhm-max 20.0 --qc-cnr-min 6.0
```

* **`PASS`**: The fit completed successfully, did not hit parameter bounds, CNR > 5.0, FWHM is between 0.5–15.0 s, and $T_{2p}$ is between 0.1–10.0 s.
* **`WARN`**: The fit succeeded, but one or more parameters crossed the warning/pass limits (e.g. FWHM > 15 s, CNR 3.0–5.0, or landed within 1% of a solver boundary—using the relaxed solver bounds `Amplitude: [1.0, max_amp]`, `T2p: [0.01, 12.0]`, and `FWHM: [0.1, 20.0]` if a second-pass refit was run).
* **`FAIL`**: The fit returned NaN, failed solver convergence, or had CNR < 3.0.

##### Triaging & Correcting Fits:
If a batch run yields `WARN` or `FAIL` flags, you can easily inspect and correct them using the native C++ GUI app (see **[INSTALL.md](INSTALL.md)** for installation instructions).

**Step-by-step Triage Workflow:**
1. **Load the Processed Data:** 
   - Launch the C++ GUI app. 
   - Click the **"Load Subject Data"** button in the top bar to open the file browser modal, select your generated results CSV (e.g., `sample-subject-2259/bolus1_baseline_results_cpp.csv`), and click **"Open Selected File"** (or click **"Select Current Folder"** if selecting the parent folder containing the CSV/TIFF).
   - *Note on Multi-Subject Loading*: The GUI is designed to load and triage one subject dataset (CSV, TIFF, and ROI files) at a time to focus on that subject's capillaries. You can load subjects sequentially via the file browser, or open multiple instances of the GUI app in separate terminal windows to work on multiple subjects in parallel.
   - *Alternative (CLI)*: You can pass the path to the CSV directly when running the application from the command line:
     ```bash
     ./build/bolus_tracking_gui sample-subject-2259/bolus1_baseline_results_cpp.csv
     ```
2. **Focus on Problem Cases:** Check the **"Show WARN/FAIL only"** checkbox at the top of the sidebar queue. This filters out all successful `PASS` cases. Note that **any fit calculation returning a `NaN` parameter (due to divergence or numerical issues) is automatically flagged as `FAIL`**.
3. **Inspect the Raw Signal:** Click on any flagged ROI. Inspect the trace to identify if the issue is due to pre-bolus baseline noise, a recirculation tail, or incorrect initial marker detection.
4. **Define a Crop Window (On-the-Fly Cropping):** Adjust the blue and magenta vertical brackets on the plot boundaries to crop out noise or secondary recirculation tails. Double-clicking the plot resets the zoom, and the **Undo Crop** button resets the active crop window.
5. **Manually Drag the Fitting Markers:** Drag the three vertical lines (**Green** for onset, **Yellow** for peak, **Red** for clearance/end) to visually align with the first-pass bolus.
6. **Run the Constrained Re-fit:** Click **Re-fit Manual**. The Levenberg-Marquardt optimizer runs exclusively within your crop window using your markers as initial parameters. The ROI status in the queue will update dynamically.
7. **Save Changes:** Click **Save Final CSV** in the toolbar to write the updated parameters back to your results file.
8. **Resume Anytime:** The GUI automatically saves your triage progress (including active selection, visual crop boundaries, customized fitting markers, and queue filters) in a sidecar `.gui_state` file. You can close the app at any point and resume exactly where you left off when you reload the CSV.

---

> [!NOTE]
> **Python Reference Pipeline**: If you want to run the original Python-based batch processing pipeline or launch the Tkinter/Matplotlib GUI, please see the dedicated **[README_Python_Pipeline.md](README_Python_Pipeline.md)**.

---

## 3. Running the C++ Interactive GUI: Dear ImGui & ImPlot Studio

Since GUI applications require display access, they are run locally on your host operating system. The C++ GUI is a high-performance visual dashboard built on top of the ultra-fast C++ fitting engine. It uses Dear ImGui and ImPlot to display traces, triage problem fits, adjust fitting parameters, and crop data ranges interactively. The layout features a dynamic responsive design that auto-scales the plot height and utilizes aligned grid tables for the sidebar and headers, ensuring all parameters, labels, and navigation buttons (`< Previous` / `Next >`) remain fully visible and never cut off on different screen sizes.

#### How to Build and Launch the C++ GUI:

##### Option A: Automatic Installer (macOS Only)
If you are on macOS, you can automatically build, package, and install a native clickable app bundle with a custom icon in one command:
```bash
bash install_macos.sh
```
This script compiles the GUI, builds `BolusTrackingStudio.app` in the repository folder, and installs it into `/Applications/` (or `~/Applications/` if not writable) so you can launch it directly from **Launchpad**, Finder, or Spotlight.

##### Option B: Manual Compilation (macOS, Linux, Windows)
1. Make sure you have CMake, a C++17 compiler, and LibTIFF installed on your system.
2. Compile the binaries locally:
   ```bash
   mkdir -p build && cd build
   cmake -DBUILD_GUI=ON ..
   make -j4
   ```
3. Run the GUI:
   ```bash
   ./bolus_tracking_gui
   ```
   *(You can optionally pass a CSV file path directly on launch to load it immediately: `./bolus_tracking_gui /path/to/results_cpp.csv`)*

#### Key GUI Workflows:
* **Triage Queue Sidebar**: Quickly review all ROIs. Check "Show WARN/FAIL only" to hide already successful fits and focus exclusively on problem cases.
* **Interactive Marker Adjustments**: Drag the three vertical lines on the plot:
  * **Onset (Green)**
  * **Peak (Yellow)**
  * **End (Red)**
* **On-the-Fly Fitting Cropping**: Adjust the blue/magenta brackets at the bottom or sides of the plot to define a cropped fitting sub-window (e.g., to exclude baseline noise or late recirculation).
* **Resetting / Visual Zoom**: Double-click the plot to reset the axis limits, or click the **Undo Crop** button to restore the full signal range.
* **Manual Re-fitting**: Click **Re-fit Manual** to execute a constrained fit using your manually dragged markers as the initial parameters and your visual crop bounds as the active fit window. All exported parameters remain mapped relative to the *uncropped* absolute time scale.
* **Batch Auto-Fitting**: Load a folder, click **Run Auto Fit Batch** to process the folder automatically, and watch the results list update in real time.
* **Interactive Denoising Strength**: Adjust the **Denoise Strength** slider (0.5x to 3.0x) in the control panel to interactively alter the noise-filtering factor for raw trace smoothing.
* **Revert to Original (Autoassignments)**: Click the **Revert to Original** button to discard all manual corrections for the selected ROI, restore the baseline automatic fit parameters, and remove the manual triage marker tags.
* **Reset All Changes**: Click the **Reset All** button in the top menu bar to restore the entire loaded dataset back to its pristine, pre-triaged automatic state. A confirmation dialog prevents accidental data loss.
* **Clear Subject Data**: Click the **Clear Subject** button in the top menu bar to unload all datasets, ROIs, and TIFF frames, returning the visual dashboard to its empty welcome screen.
* **Bilingual Translation (OQLF Compliant)**: Click the **EN / FR** flags in the top menu bar to toggle the user interface between Canadian English and official Quebec French (fully compliant with OQLF standardization).

---

### Fit Quality Parameters & Triage Limits

To ensure biological plausibility, the automated pipeline and GUI evaluate the fits against the following QC criteria:

| Parameter | Description | Warning Threshold (`WARN`) | Failure Threshold (`FAIL`) | Absolute Solver Bounds (Hard Limit) |
| :--- | :--- | :--- | :--- | :--- |
| **Amplitude** | Peak height of the bolus curve | Near solver boundary | — | `[1e-6, 1023.0]` |
| **Time to Peak ($T_{2p}$)** | Duration from onset to peak | `< 0.1 s` or `> 10.0 s` or near boundary | — | `[1e-6, fit window duration]` |
| **FWHM** | Full width of bolus at half max amplitude | `< 0.5 s` or `> 15.0 s` or near boundary | — | `[0.5, fit window duration]` |
| **CNR** | Contrast-to-Noise Ratio (Peak / SD of baseline) | `[3.0, 5.0]` | `< 3.0` | — |

* **`PASS`**: The fit completed successfully, did not hit parameter bounds, CNR > 5.0, FWHM is between 0.5–15.0 s, and $T_{2p}$ is between 0.1–10.0 s.
* **`WARN`**: The fit succeeded, but one or more parameters crossed the warning/pass limits (e.g. FWHM > 15 s, CNR 3.0–5.0, or landed within 1% of a solver boundary—using the relaxed solver bounds `Amplitude: [1.0, max_amp]`, `T2p: [0.01, 12.0]`, and `FWHM: [0.1, 20.0]` if a second-pass refit was run).
* **`FAIL`**: The fit returned NaN, failed solver convergence, or had CNR < 3.0.

---

### Operator's Guide: Triaging & Correcting Fits in the GUI

If a subject folder has capillary fits flagged as `WARN` or `FAIL`, use the C++ GUI (see **[INSTALL.md](INSTALL.md)** for installation instructions) to review and manually fit them:

1. **Focus on Problem Cases:** Click **Load Folder** to select a subject directory, or open the existing results CSV. Once the queue list in the sidebar is populated, check **"Show WARN/FAIL only"** at the top of the sidebar. This hides all healthy fits, letting you isolate problem cases.
2. **Inspect the Raw Signal:** Click on a flagged ROI to load its trace. Look for common issues:
   * *Baseline Noise:* High high-frequency fluctuations before the dye bolus arrives.
   * *Recirculation Tail:* A secondary rise or slow decay in fluorescence after the main bolus passes.
   * *Incorrect Peak/Onset Selection:* The automated derivative heuristic may have locked onto a noisy spike instead of the true bolus passage.
3. **Define a Crop Window (On-the-Fly Cropping):**
   * Adjust the blue and magenta vertical brackets at the edges of the plot to define a crop window.
   * For example, if there is late recirculation or a noisy baseline tail, drag the right bracket (magenta) to the left to exclude data past the first-pass clearance.
   * If the pre-bolus baseline is noisy, drag the left bracket (blue) to the right.
   * *Note:* You can double-click the plot to reset the axis limits, or click **Undo Crop** to restore the full signal range at any time.
4. **Manually Drag the Fitting Markers:**
   * Drag the three vertical lines directly on the plot to specify your manual estimates:
     * **Green line:** Onset time
     * **Yellow line:** Peak time
     * **Red line:** Clearance/End time
   * These lines serve as the initial guess parameters for the constrained optimizer.
5. **Run the Constrained Re-fit:**
   * Click **Re-fit Manual**. The C++ optimizer will run a Levenberg-Marquardt fitting step *exclusively* within the cropped window you defined in Step 3, using your dragged markers from Step 4 as initial values.
   * The fit will immediately update on screen. If it satisfies the QC constraints, its status in the sidebar will update (e.g. to a green `PASS` or `WARN` manually-fit status).
6. **Save and Export:**
   * When you are satisfied with the manually adjusted fits, click **Save CSV** in the sidebar. The updated fitting parameters (Amplitude, FWHM, Time to Peak, AUC, etc.) are written directly to the output CSV. Note that all timing parameters are exported relative to the *original, uncropped* absolute time scale so that manual crop bounds do not bias the physical transit times.

---

## 4. Alternative Workflow: Running Locally without Docker

If you cannot use Docker, you can run the pipelines locally on your machine:

### C++ Pipeline (Local)
Make sure you have CMake, a C++17 compiler, Eigen3, and libtiff installed on your system.
```bash
# Compile and run
mkdir -p build && cd build
cmake ..
make -j4
cd ..
./build/bolus_tracking_cpp --folder sample-subject-2259
```

> [!TIP]
> **Generating Plots & Drift Correction**: To generate SVG plots when running locally, append the `--plot` flag:
> ```bash
> ./build/bolus_tracking_cpp --folder sample-subject-2259 --plot
> ```
> To customize the drift baseline duration (e.g. to 10 seconds instead of the default 15), append `--drift 10`:
> ```bash
> ./build/bolus_tracking_cpp --folder sample-subject-2259 --plot --drift 10
> ```
> 
> To constrain the fit parameters (amplitude, time-to-peak, and FWHM) to physiologically reasonable values, you can specify lower and upper bounds:
> - `--min-amp` (default: `1e-6`)
> - `--max-amp` (default: `1023.0` - matching the 10-bit microscope digitizer limit)
> - `--min-t2p` (default: `1e-6`)
> - `--max-t2p` (default: dynamically capped to the duration of the fit window)
> - `--min-fwhm` (default: `0.5` seconds - matching physiological transit speed limits)
> - `--max-fwhm` (default: dynamically capped to the duration of the fit window)
> 
> For example, to constrain the time-to-peak ($T_{2p}$) between 2.0 and 8.0 seconds:
> ```bash
> ./build/bolus_tracking_cpp --folder sample-subject-2259 --plot --min-t2p 2.0 --max-t2p 8.0
> ```
> This is also supported in the C++ runner script (`run_pipeline_cpp.sh`).

---

## 5. Technical Overview of the Files

Here is what each file does:
* `bolus_gui.cpp`: The C++ interactive GUI built on Dear ImGui and ImPlot.
* `bolus_gui.py`: The Python-based interactive GUI built on Tkinter and Matplotlib.
* `run_pipeline.sh`: The master control script that prepares the Python virtual environment and kicks off the processing.
* `batch_process.py`: The high-level script that scans for datasets, reads TIFF image stacks, extracts the mean signal from each ROI, fits the Gamma curve, and saves results/plots.
* `bolus_tracking.py`: The core computational engine containing all denoising, thresholding, onset/peak/end detection, and mathematical optimization logic.
* `run_pipeline_cpp.sh`: The pure Bash script to compile and launch the C++ parallel processing pipeline.
* `bolus_tracking_cpp.cpp`: The core C++ source code containing natural cubic spline upsampling, Levenberg-Marquardt robust curve fitting (Eigen), and parallel multi-threading.
* `bolus_tracking_cpp.hpp`: Header file containing the OOP structure of the C++ pipeline.
* `test_bolus_tracking_cpp.cpp`: The C++ testing suite.
* `CMakeLists.txt` & `Dockerfile.cpp`: Compilation and Docker configurations for the C++ implementation.
* `test_bolus_parity.py` & `test_bolus_tracking.py`: Python unit tests and parity test suite.
* `BolusTrack_InteractiveEdit.m`: The MATLAB graphical user interface (GUI) for manually visualizing and adjusting fits.
* `gammaFun.m`: The MATLAB definition of the Gamma variate function.
* `Dockerfile`: Instructs Docker how to build the runtime container image for the Python pipeline.

---

## 6. Inputs and Output Structure

### Expected Inputs
The pipeline automatically scans directories in the workspace for subject folders (e.g., `sample-subject-2259`). Inside each folder, it expects:
1. **A TIFF stack image** (e.g., `bolus1_baseline.tif`): The 3D fluorescent video of the bolus transit.
2. **A Metadata text file** (e.g., `bolus1_baseline.txt`): A text file containing the acquisition frame rate (e.g., `Fr = 8.16` or `FrameRate = 8.16`).
3. **MATLAB ROI masks** (or the converted `_rois.txt` coordinate files): Contain the spatial shapes of the capillary regions of interest.

### Generated Outputs
After running the pipeline, each subject folder will contain:
1. **A Results CSV file** (e.g., `bolus1_baseline_results_cpp.csv` for C++ or `bolus1_baseline_results.csv` for Python): A table containing fitted parameters (Amplitude, Time to Peak, FWHM, Baseline, AUC, AUCn, transit times) for every ROI.
2. **A Plots folder** (`plots_cpp/` for C++ or `plots/` for Python): If plotting is enabled, contains high-resolution visual fits for each ROI.
