# Bolus Tracking: Python Pipeline

This document explains how to run the newly automated Python bolus tracking pipeline. This pipeline replaces the manual MATLAB GUI workflow, allowing for headless batch processing on any system (including your Linux registration machine).

## Prerequisites

Before running the Python scripts, you need to convert your older MATLAB `MaskObj.mat` files into a clean format that Python can read.

1. **Convert the Masks (One-Time Step):**
   Open MATLAB in this directory and run the conversion script:
   ```matlab
   run('scratch/convert_masks_for_python.m')
   ```
   This will automatically find every `MaskObj.mat` file in your folders and create a duplicate file named `adjusted_<OriginalName>.mat`. **You will use these `adjusted_*.mat` files for the Python pipeline.**

## Setup Python Environment

You need to install the required scientific libraries. You can use the provided virtual environment or install them globally/via Docker.

**Using the Virtual Environment:**
```bash
# Create a virtual environment (only needed once)
python3 -m venv .venv

# Activate it
source .venv/bin/activate

# Install dependencies
pip install -r requirements.txt
```

*(Note: If you prefer Docker, you can build the provided `Dockerfile` using `docker build -t bolus_tracking .`)*

## Running the Pipeline

The main script is `batch_process.py`. It takes a registered TIFF stack, an adjusted mask `.mat` file, and a `.txt` metadata file as inputs.

### Usage
You can either provide a specific folder to auto-detect all matching files, or specify them individually.

**To auto-detect files in a folder:**
```bash
python batch_process.py --folder <path_to_folder> --outdir <output_directory>
```
*(The script will automatically find and pair the TIFFs, Metadata `.txt` files, and `adjusted_*.mat` masks based on the `bolusX_condition` naming convention).*

**To specify files individually:**
```bash
python batch_process.py --tiff <path_to_tif> --mask <path_to_adjusted_mat> --meta <path_to_txt> --outdir <output_directory>
```

### Example
Here is how you would process the entire `sample-subject-2259` dataset automatically:

```bash
# Ensure your virtual environment is active first!
source .venv/bin/activate

python batch_process.py --folder sample-subject-2259 --outdir ./
```

*(Note: You were getting a `FileNotFoundError` earlier because the example command had the placeholder name `adjusted_maskObj.mat` instead of the actual file name `adjusted_3554_bolus1_baseline_shifted_MaskObj.mat`).*

## Output
The script will output a CSV file (e.g., `3554_bolus1_baseline_123-300_shifted_results.csv`) containing the automatically estimated and fitted parameters for every ROI mask in the stack:
- `InitAmp`, `InitT2p`, `InitFWHM`, `InitM` (Auto-estimated starting points)
- `F_Amp`, `F_T2p`, `F_FWHM`, `F_M` (Final fitted Gamma parameters mathematically identical to MATLAB's `nlinfit`)
