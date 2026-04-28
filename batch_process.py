import os
import glob
import argparse
import numpy as np
import scipy.io as sio
import tifffile
from skimage.draw import polygon
from scipy.interpolate import interp1d
import pandas as pd
import warnings
from bolus_tracking import denoise_trace, auto_estimate_params, fit_bolus, gamma_fun

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("WARNING: matplotlib is not installed. Skipping screenshot generation.")

warnings.filterwarnings("ignore")

def parse_metadata(meta_path):
    """
    Parses the Fluoview .txt metadata file to extract frame rate.
    Looks for the "T Dimension" line.
    """
    with open(meta_path, 'r') as f:
        content = f.read()
    
    # We'll just look for the typical line format.
    # E.g., "T Dimension"  "300, 0.000 - 59.004 [s], Interval FreeRun"
    import re
    match = re.search(r'"T Dimension"\s+"(\d+),\s+([\d.]+)\s*-\s*([\d.]+)\s*\[s\]', content)
    if match:
        frames = int(match.group(1))
        t_start = float(match.group(2))
        t_end = float(match.group(3))
        fr = round(frames / (t_end - t_start), 2)
        return fr
    else:
        raise ValueError(f"Could not parse frame rate from {meta_path}")

def get_mask_from_poly(poly_verts, shape):
    """
    Converts polygon vertices to a boolean raster mask.
    """
    # poly_verts are (x, y) coordinates, so (col, row)
    # Ensure they are contiguous float arrays to avoid Cython segfaults under Rosetta
    r = np.ascontiguousarray(poly_verts[:, 1], dtype=np.float64)
    c = np.ascontiguousarray(poly_verts[:, 0], dtype=np.float64)
    
    rr, cc = polygon(r, c, shape)
    mask = np.zeros(shape, dtype=bool)
    mask[rr, cc] = True
    return mask

def process_bolus(tiff_path, mask_path, meta_path, out_dir):
    """
    Process a single bolus dataset.
    """
    print(f"Processing {os.path.basename(tiff_path)}...")
    
    # 1. Load Data
    fr = parse_metadata(meta_path)
    tiff_stack = tifffile.imread(tiff_path)
    
    # Optional median filtering (omitted here for speed, but can be added if needed)
    
    mat_data = sio.loadmat(mask_path, struct_as_record=False, squeeze_me=True)
    if 'maskObj' in mat_data:
        mask_objs = mat_data['maskObj']
    else:
        raise ValueError(f"No maskObj found in {mask_path}. If this is an old MATLAB object array, please load it into the updated BolusTrack and click 'Save ROIs' to save it as a clean struct array that Python can read.")
        
    # Ensure it's iterable
    if not isinstance(mask_objs, np.ndarray):
        mask_objs = [mask_objs]
        
    img_shape = tiff_stack.shape[1:]
    up_f = 20
    
    results = []
    
    # 2. Process each ROI
    for i, obj in enumerate(mask_objs):
        # Extract polygon vertices depending on MATLAB structure
        if hasattr(obj, 'poli'):
            pos = obj.poli.Position
        elif hasattr(obj, 'Position'):
            pos = obj.Position
        else:
            print(f"Skipping ROI {i+1}: Cannot find Position field.")
            continue
            
        if len(pos) < 3:
            print(f"Skipping ROI {i+1}: Not enough points for a polygon ({len(pos)}).")
            continue
            
        mask = get_mask_from_poly(pos, img_shape)
        
        # Calculate mean fluorescence intensity (MFI)
        mfi = np.array([np.mean(frame[mask]) for frame in tiff_stack])
        
        # Spline upsample
        tl_raw = np.linspace(0, len(mfi)/fr, len(mfi))
        tl_us = np.linspace(0, len(mfi)/fr, len(mfi)*up_f)
        
        spline_interp = interp1d(tl_raw, mfi, kind='cubic', fill_value='extrapolate')
        y_us = spline_interp(tl_us)
        
        # Estimate parameters
        init_params, start_idx, end_idx = auto_estimate_params(y_us, tl_us, fr, up_f)
        
        # Fit Gamma Function
        popt, pcov = fit_bolus(tl_us[start_idx:end_idx], y_us[start_idx:end_idx], init_params)
        
        # Record results
        if popt is not None:
            # We don't have the full metrics here, just the fitted parameters for demonstration
            results.append({
                'ROI': i+1,
                'InitAmp': init_params[0],
                'InitT2p': init_params[1],
                'InitFWHM': init_params[2],
                'InitM': init_params[3],
                'F_Amp': popt[0],
                'F_T2p': popt[1],
                'F_FWHM': popt[2],
                'F_M': popt[3],
            })
        else:
            results.append({
                'ROI': i+1,
                'InitAmp': init_params[0],
                'InitT2p': init_params[1],
                'InitFWHM': init_params[2],
                'InitM': init_params[3],
                'F_Amp': np.nan, 'F_T2p': np.nan, 'F_FWHM': np.nan, 'F_M': np.nan
            })
            
        # --- Plotting/Screenshots ---
        if HAS_MATPLOTLIB:
            plots_dir = os.path.join(out_dir, "plots")
            os.makedirs(plots_dir, exist_ok=True)
            
            fig, ax = plt.subplots(figsize=(8, 5))
            ax.plot(tl_raw, mfi, 'k.', label='Raw Data', alpha=0.6)
            ax.plot(tl_us, y_us, 'b--', label='Spline Upsampled', alpha=0.8)
            
            if popt is not None:
                y_fit = gamma_fun(tl_us[start_idx:end_idx], *popt)
                ax.plot(tl_us[start_idx:end_idx], y_fit, 'r-', linewidth=2, label='Gamma Fit')
                ax.set_title(f"ROI {i+1} Fit\nAmp={popt[0]:.2f}, T2p={popt[1]:.2f}, FWHM={popt[2]:.2f}, Base={popt[3]:.2f}")
            else:
                ax.set_title(f"ROI {i+1} Fit Failed")
                
            ax.set_xlabel('Time (s)')
            ax.set_ylabel('Fluorescence Intensity')
            ax.legend()
            
            base_name = os.path.basename(tiff_path).replace('.tif', '')
            plot_name = f"{base_name}_ROI_{i+1}_fit.png"
            plt.savefig(os.path.join(plots_dir, plot_name), dpi=150, bbox_inches='tight')
            plt.close(fig)
            
    df = pd.DataFrame(results)
    out_csv = os.path.join(out_dir, os.path.basename(tiff_path).replace('.tif', '_results.csv'))
    df.to_csv(out_csv, index=False)
    print(f"Saved results to {out_csv}")

import re

def find_triplets(folder):
    """
    Automatically finds and pairs (TIFF, MAT, TXT) files in a folder based on 'bolusX_condition' naming.
    """
    tifs = glob.glob(os.path.join(folder, '**/*.tif'), recursive=True)
    mats = glob.glob(os.path.join(folder, '**/adjusted_*.mat'), recursive=True)
    txts = glob.glob(os.path.join(folder, '**/*.txt'), recursive=True)
    
    triplets = []
    for tif in tifs:
        # Ignore generated mips, shift info, or results if any
        tif_lower = tif.lower()
        name = os.path.basename(tif).lower()
        if 'mips' in tif_lower or 'results' in tif_lower or 'shift_info' in tif_lower or 'max_' in name:
            continue
            
        # Extract the core bolus identifier like 'bolus1_baseline' or 'bolus2_co2'
        match = re.search(r'(bolus\d+_[a-z0-9]+)', name)
        if not match:
            continue
            
        identifier = match.group(1)
        
        # Find matching mat
        matching_mat = [m for m in mats if identifier in os.path.basename(m).lower()]
        # Find matching txt
        matching_txt = [t for t in txts if identifier in os.path.basename(t).lower()]
        
        if matching_mat and matching_txt:
            triplets.append((tif, matching_mat[0], matching_txt[0]))
            
    return list(set(triplets)) # remove duplicates if any

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Batch process Bolus Tracking data")
    parser.add_argument("--folder", help="Path to a folder containing TIFFs, Masks, and TXTs to auto-detect and process")
    parser.add_argument("--tiff", help="Path to Bolus TIFF stack (if not using --folder)")
    parser.add_argument("--mask", help="Path to maskObj .mat file (if not using --folder)")
    parser.add_argument("--meta", help="Path to metadata .txt file (if not using --folder)")
    parser.add_argument("--outdir", help="Output directory", default="./")
    
    args = parser.parse_args()
    
    if args.folder:
        triplets = find_triplets(args.folder)
        if not triplets:
            print(f"No matching (TIFF, MAT, TXT) sets found in {args.folder}.")
        else:
            print(f"Found {len(triplets)} matching datasets to process.")
            for tif, mat, txt in triplets:
                try:
                    process_bolus(tif, mat, txt, args.outdir)
                except Exception as e:
                    print(f"Failed to process {tif}: {e}")
    elif args.tiff and args.mask and args.meta:
        process_bolus(args.tiff, args.mask, args.meta, args.outdir)
    else:
        print("Please provide either --folder OR --tiff, --mask, and --meta arguments.")
