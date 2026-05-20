# Complete Guide to Running the Bolus Tracking Pipeline

This guide is designed for **both human users (even with zero coding experience)** and **AI coding agents** to easily set up, run, and maintain the bolus tracking pipeline.

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
git clone https://github.com/your-username/Adrienne_Bolus_Tracking.git
cd Adrienne_Bolus_Tracking
```

---

## 2. Operating System Setup Instructions

### macOS (Mac)
1. Open the Terminal app (press `Cmd + Space`, type `Terminal`, and press `Enter`).
2. Make sure you have python installed. If not, install it via Homebrew (`brew install python`) or download from [python.org](https://www.python.org/downloads/).
3. Navigate to the folder you extracted:
   ```bash
   cd ~/Downloads/Adrienne_Bolus_Tracking
   ```
4. Run the pipeline:
   ```bash
   bash run_pipeline.sh
   ```

### Linux (e.g., Ubuntu/Debian)
1. Open your terminal.
2. Install Python, virtual environment tools, Git, and Docker by running:
   ```bash
   sudo apt-get update
   sudo apt-get install python3 python3-pip python3-venv git -y
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
2. Navigate to your extracted folder (e.g. `cd C:\Users\YourName\Downloads\Adrienne_Bolus_Tracking`).
3. Run the Python setup:
   ```powershell
   python -m venv .venv
   .venv\Scripts\Activate.ps1
   pip install -r requirements.txt
   python batch_process.py --folder .
   ```

---

## 3. Running with Docker (Containerized Execution)

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

## 4. Technical Overview of the Files

Here is what each file does:
* `run_pipeline.sh`: The master control script that prepares the Python virtual environment and kicks off the processing.
* `batch_process.py`: The high-level script that scans for datasets, reads TIFF image stacks, extracts the mean signal from each ROI, fits the Gamma curve, and saves results/plots.
* `bolus_tracking.py`: The core computational engine containing all denoising, thresholding, onset/peak/end detection, and mathematical optimization logic.
* `test_bolus_parity.py`: A test suite to verify that Python results match legacy MATLAB results.
* `BolusTrack_InteractiveEdit.m`: The MATLAB graphical user interface (GUI) for manually visualizing and adjusting fits.
* `gammaFun.m`: The MATLAB definition of the Gamma variate function.
* `Dockerfile`: Instructs Docker how to build the runtime container image.

---

## 5. Inputs and Output Structure

### Expected Inputs
The pipeline automatically scans directories in the workspace for subject folders (e.g., `sample-subject-2259`). Inside each folder, it expects:
1. **A TIFF stack image** (e.g., `bolus1_baseline.tif`): The 3D fluorescent video of the bolus transit.
2. **A Metadata text file** (e.g., `bolus1_baseline.txt`): A text file containing the acquisition frame rate (e.g., `Fr = 8.16` or `FrameRate = 8.16`).
3. **MATLAB ROI masks** (e.g. inside `old_masks_drawROI/` or as `.mat` files): Contain the spatial shapes of the capillary regions of interest.

### Generated Outputs
After running the pipeline, each subject folder will contain:
1. **A Results CSV file** (e.g., `bolus1_baseline_results.csv`): A table containing fitted parameters (Amplitude, Time to Peak, FWHM, Baseline, AUC, transit times) for every ROI.
2. **A Plots folder** (e.g., `plots/`): High-resolution visual fits for each ROI showing:
   * **Raw data** (gray dots)
   * **Denoised trace** (green pluses)
   * **Upsampled spline trace** (dashed blue line)
   * **Heuristic markers**: Baseline start (green), Onset (cyan), Peak (purple), and End (red)
   * **Fitted Gamma curve** (solid red line)
