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

## 2. Operating System Setup Instructions

### macOS (Mac)
1. Open the Terminal app (press `Cmd + Space`, type `Terminal`, and press `Enter`).
2. Make sure you have python installed. If not, install it via Homebrew (`brew install python`) or download from [python.org](https://www.python.org/downloads/).
3. Navigate to the folder you extracted:
   ```bash
   cd ~/Downloads/Bolus_Tracking
   ```
4. Run the pipeline:
   ```bash
   bash run_pipeline.sh
   ```

### Linux (e.g., Ubuntu/Debian)
1. Open your terminal.
2. Install Python, virtual environment tools, Git, and GUI libraries (Tkinter) by running:
   ```bash
   sudo apt-get update
   sudo apt-get install python3 python3-pip python3-venv python3-tk git -y
   ```
3. To install Docker (optional, but highly recommended for containerized runs):
   ```bash
   sudo apt-get install docker.io -y
   sudo systemctl start docker
   sudo systemctl enable docker
   # Optional: Allow running docker commands without sudo
   sudo usermod -aG docker $USER
   ```
   *(Note: If you add yourself to the docker group, log out and log back in for the changes to take effect).*
4. Navigate to the repository folder and run the pipeline:
   ```bash
   bash run_pipeline.sh
   ```

### Windows
1. Open **PowerShell** as Administrator.
2. Navigate to your extracted folder (e.g. `cd C:\Users\YourName\Downloads\Bolus_Tracking`).
3. Run the Python setup:
   ```powershell
   python -m venv .venv
   .venv\Scripts\Activate.ps1
   pip install -r requirements.txt
   python batch_process.py --folder .
   ```

---

## 3. Running the Python Interactive GUI (New!)

We have created a premium Python-based Graphical User Interface (`bolus_gui.py`) so you can visually verify and custom-adjust the fits for every ROI without needing MATLAB!

### How to Launch the GUI
Make sure your virtual environment is active, then run:
```bash
# macOS / Linux
.venv/bin/python bolus_gui.py

# Windows (PowerShell)
.venv/Scripts/python bolus_gui.py
```

### GUI Step-by-Step Instructions
1. **Load Subject Folder**: Click **📁 Open Subject Folder** at the top left. Select any folder containing your TIFF stack and metadata files (it defaults to your current directory).
2. **Select Dataset**: Choose your dataset triplet (TIFF, MAT mask, and metadata TXT) from the **1. Select Dataset** dropdown. The image trace will load automatically.
3. **Select Capillary ROI**: Navigate between the different capillary regions of interest using the **2. Select Capillary ROI** dropdown.
4. **Interactive Marker Placement**:
   - If the automatic guess is slightly off, click one of the orange, purple, red, or green **Adjust Markers** buttons (e.g., **Set Onset ⌖**).
   - Click anywhere on the plot to place that marker. The GUI will instantly re-calculate the fit and draw the updated curves!
5. **Direct Entry Editing**: You can also type exact numbers directly into the Amplitude, T2P, FWHM, Baseline, Onset, and End fields. Click **⚡ Run Gamma Fit** to apply them.
6. **Vessel Designation**: Set the capillary type (Unknown, Artery, Vein, or Capillary) under the dropdown.
7. **Save & Export**: Click **💾 Save & Export Results** to write the parameters to the CSV results table and save a high-resolution screenshot of the fit inside the subject's `plots/` folder.

---

## 4. Running with Docker (Containerized Execution)

Running inside Docker is highly recommended as it guarantees all library versions are identical and prevents dependency conflicts on your computer.

### Method 1: Automatic Detection
If Docker is installed and running on your computer, the `run_pipeline.sh` script will **automatically** build the docker container and run the processing inside it. You do not need to type any Docker commands manually!

### Method 2: Manual Docker Execution (Command Line)
If you want to run the Docker container manually without the wrapper script:

1. **Build the Docker Image**:
   ```bash
   docker build -t bolus_tracking:latest .
   ```

2. **Run on all subjects in the current directory**:
   This maps your current working directory (`$(pwd)`) to the `/data` folder inside the container so it can read files and write output.
   ```bash
   docker run --rm -v "$(pwd):/data" bolus_tracking:latest --folder /data
   ```

3. **Run on a specific subject folder**:
   ```bash
   docker run --rm -v "$(pwd):/data" bolus_tracking:latest --folder /data/sample-subject-2259
   ```

---

## 5. Pure C++ Parallel Pipeline (New!)

For extremely fast, lightweight execution in high-performance or headless environments, we provide a standalone, parallelized C++ implementation (`bolus_tracking_cpp.cpp`).

### Important Note on Plotting
> [!NOTE]
> The C++ pipeline is a purely computational engine designed to run extremely fast with minimal dependencies. It **does not generate plots**. It processes the TIFF stacks and exports results directly to `*_results_cpp.csv`.
> If you want to visualize the fits and generate high-resolution PNG plots, run the Python batch pipeline (`run_pipeline.sh`) or use the interactive GUI (`bolus_gui.py`).

### How to Run the C++ Pipeline

You can run the C++ pipeline locally or inside its own dedicated Docker container:

#### Option A: Run via Bash Script (Local or Docker)
The bash script `run_pipeline_cpp.sh` automatically detects if Docker is running.
- **If Docker is running**: It builds `Dockerfile.cpp` and runs the C++ batch processor containerized.
- **If Docker is not running**: It compiles the C++ code locally using CMake and runs the binary.

```bash
# Process all folders in the current directory
bash run_pipeline_cpp.sh

# Process a specific subject folder
bash run_pipeline_cpp.sh sample-subject-2259
```

#### Option B: Run C++ manually inside Docker
1. **Build the image**:
   ```bash
   docker build -t bolus_tracking_cpp -f Dockerfile.cpp .
   ```
2. **Execute**:
   ```bash
   docker run --rm -v "$(pwd):/data" bolus_tracking_cpp --folder /data
   ```

---

## 6. Technical Overview of the Files

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

---

## 7. Inputs and Output Structure

### Expected Inputs
The pipeline automatically scans directories in the workspace for subject folders (e.g., `sample-subject-2259`). Inside each folder, it expects:
1. **A TIFF stack image** (e.g., `bolus1_baseline.tif`): The 3D fluorescent video of the bolus transit.
2. **A Metadata text file** (e.g., `bolus1_baseline.txt`): A text file containing the acquisition frame rate (e.g., `Fr = 8.16` or `FrameRate = 8.16`).
3. **MATLAB ROI masks** (or the converted `_rois.txt` coordinate files): Contain the spatial shapes of the capillary regions of interest.

### Generated Outputs
After running the pipeline, each subject folder will contain:
1. **A Results CSV file** (e.g., `bolus1_baseline_results.csv` or `bolus1_baseline_results_cpp.csv`): A table containing fitted parameters (Amplitude, Time to Peak, FWHM, Baseline, AUC, transit times) for every ROI.
2. **A Plots folder** (Python only, e.g., `plots/`): High-resolution visual fits for each ROI.
