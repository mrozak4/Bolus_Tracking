# Complete Guide to Running the Bolus Tracking Pipeline

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

## 2. Recommended Workflow: Running Everything with Docker

> [!IMPORTANT]
> **Docker is the highly recommended and preferred way to run both the Python and C++ pipelines.**
> Using Docker guarantees that all library versions are identical, prevents dependency conflicts, and requires zero installation of Python or C++ compilers on your system.

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

### A. Running the C++ Parallel Pipeline with Docker
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
> The SVG plots will be saved to a `plots_cpp` subdirectory within the subject folder.

#### Manual command:
If you want to run the Docker command manually:
```bash
# 1. Build the C++ image
docker build -t bolus_tracking_cpp -f Dockerfile.cpp .

# 2. Run the C++ processing (maps current directory to /data in the container)
docker run --rm -v "$(pwd):/data" bolus_tracking_cpp --folder /data/sample-subject-2259
```

---

### B. Running the Python Pipeline with Docker
The master control script `run_pipeline.sh` automatically detects if Docker is installed and running, and will run the Python batch processor inside the container.

To process a target folder (e.g. `sample-subject-2259`) using the Python pipeline with Docker:
```bash
bash run_pipeline.sh sample-subject-2259
```

#### Manual command:
If you want to run the Docker command manually:
```bash
# 1. Build the Python image
docker build -t bolus_tracking:latest .

# 2. Run the processing (maps current directory to /data in the container)
docker run --rm -v "$(pwd):/data" bolus_tracking:latest --folder /data/sample-subject-2259
```

---

## 3. Running the Python Interactive GUI

Since GUI applications require display access, the GUI (`bolus_gui.py`) is run locally on your host operating system.

### How to Launch the GUI
1. Create a Python virtual environment and install dependencies:
   ```bash
   # macOS / Linux
   python3 -m venv .venv
   source .venv/bin/activate
   pip install -r requirements.txt
   python bolus_gui.py
   
   # Windows (PowerShell)
   python -m venv .venv
   .venv\Scripts\Activate.ps1
   pip install -r requirements.txt
   python bolus_gui.py
   ```
2. **GUI Step-by-Step Instructions**:
   - **Load Subject Folder**: Click **📁 Open Subject Folder** and select the subject folder.
   - **Select Dataset**: Choose the dataset triplet from the dropdown.
   - **Select Capillary ROI**: Navigate between the different capillary regions of interest.
   - **Interactive Marker Placement**: Click **Adjust Markers** and click anywhere on the plot to adjust onset/peak/end points visually. The fit will update instantly!
   - **Save & Export**: Click **💾 Save & Export Results** to write parameters to the CSV and save a high-resolution screenshot.

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
> - `--min-amp` (default: 1e-6)
> - `--max-amp` (default: infinity)
> - `--min-t2p` (default: 1e-6)
> - `--max-t2p` (default: infinity)
> - `--min-fwhm` (default: 1e-6)
> - `--max-fwhm` (default: infinity)
> 
> For example, to constrain the time-to-peak ($T_{2p}$) between 2.0 and 8.0 seconds:
> ```bash
> ./build/bolus_tracking_cpp --folder sample-subject-2259 --plot --min-t2p 2.0 --max-t2p 8.0
> ```
> This is also supported in the Python script (`batch_process.py`) and the standard runner scripts (`run_pipeline.sh` / `run_pipeline_cpp.sh`).

### Python Pipeline (Local)
```bash
# macOS / Linux
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python batch_process.py --folder sample-subject-2259

# Windows (PowerShell)
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
python batch_process.py --folder sample-subject-2259
```

---

## 5. Technical Overview of the Files

Here is what each file does:
* `bolus_gui.py`: The Python-based interactive GUI for loading, viewing, adjusting, and exporting individual ROI bolus fits.
* `run_pipeline.sh`: The master control script that prepares the Python virtual environment and kicks off the processing.
* `batch_process.py`: The high-level script that scans for datasets, reads TIFF image stacks, extracts the mean signal from each ROI, fits the Gamma curve, and saves results/plots.
* `bolus_tracking.py`: The core computational engine containing all denoising, thresholding, onset/peak/end detection, and mathematical optimization logic.
* `run_pipeline_cpp.sh`: The pure Bash script to compile and launch the C++ parallel processing pipeline.
* `bolus_tracking_cpp.cpp`: The standalone C++ source code containing natural cubic spline upsampling, Levenberg-Marquardt robust curve fitting (Eigen), and parallel multi-threading.
* `CMakeLists.txt` & `Dockerfile.cpp`: Compilation and Docker configurations for the C++ implementation.
* `test_bolus_parity.py`: A test suite to verify that Python/C++ results match legacy MATLAB results.
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
1. **A Results CSV file** (e.g., `bolus1_baseline_results.csv` or `bolus1_baseline_results_cpp.csv`): A table containing fitted parameters (Amplitude, Time to Peak, FWHM, Baseline, AUC, transit times) for every ROI.
2. **A Plots folder** (Python only, e.g., `plots/`): High-resolution visual fits for each ROI.
