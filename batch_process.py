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

def process_bolus(tiff_path, mask_path, meta_path, out_dir, drift_window=15.0,
                  min_amp=1e-6, max_amp=np.inf, min_t2p=1e-6, max_t2p=np.inf,
                  min_fwhm=1e-6, max_fwhm=np.inf):
    """
    Process a single bolus dataset.
    """
    if not out_dir:
        out_dir = os.path.dirname(tiff_path)
        
    print(f"Processing {os.path.basename(tiff_path)}...")
    print(f"Drift window duration: {drift_window} seconds.")
    
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
        mfi_raw = np.array([np.mean(frame[mask]) for frame in tiff_stack])
        
        # Create time vector
        tl_raw = np.arange(len(mfi_raw)) / fr
        
        # Compute linear drift slope k using first drift_window seconds
        drift_mask = tl_raw <= drift_window
        k = 0.0
        if np.sum(drift_mask) > 1:
            t_drift = tl_raw[drift_mask]
            y_drift = mfi_raw[drift_mask]
            cov = np.cov(t_drift, y_drift)
            if cov[0, 0] > 1e-9:
                k = cov[0, 1] / cov[0, 0]
                
        # Detrend raw signal before running fitting pipeline
        mfi_raw_detrended = mfi_raw - k * tl_raw
        mfi = denoise_trace(mfi_raw_detrended)
        
        # Spline upsample
        tl_us = np.arange(len(mfi) * up_f) / (fr * up_f)
        
        spline_interp = interp1d(tl_raw, mfi, kind='cubic', fill_value='extrapolate')
        y_us = spline_interp(tl_us)
        
        # Estimate parameters
        init_params, start_idx, end_idx, sd_base, clicks = auto_estimate_params(y_us, tl_us, fr, up_f)
        
        # Fit Gamma Function (evaluate on a time vector starting at 0 to match MATLAB logic)
        # We only fit the bolus phase to prevent baseline duration from biasing the optimizer
        t_fit = tl_us[start_idx:end_idx] - tl_us[start_idx]
        
        m_init = init_params[3]
        m_bound = max(0.5 * sd_base, 0.005 * m_init, 0.2)
        bounds_override = (
            [min_amp, min_t2p, min_fwhm, m_init - m_bound],
            [max_amp, max_t2p, max_fwhm, m_init + m_bound]
        )
        popt, pcov = fit_bolus(t_fit, y_us[start_idx:end_idx], init_params, sd_base, bounds_override=bounds_override)
        
        subj_match = re.search(r'(?:subject[_-]?)(\d+)', tiff_path, re.IGNORECASE)
        if not subj_match:
            subj_match = re.search(r'\b\d{4}\b', tiff_path)
        subj_num = int(subj_match.group(1)) if subj_match else 0
        exp = os.path.splitext(os.path.basename(tiff_path))[0]
        
        init_snr = init_params[3] / sd_base if sd_base > 0 else np.nan
        init_cnr = init_params[0] / sd_base if sd_base > 0 else np.nan
        denoise_rms = np.sqrt(np.mean((mfi_raw_detrended - mfi)**2))
        roi_size = int(np.sum(mask))
        ves_type = 'U'
        
        auc = np.nan
        aucn = np.nan
        ttlb = np.nan
        ttm = np.nan
        tthb = np.nan
        ont = np.nan
        
        if popt is not None:
            f_amp, f_t2p, f_fwhm, f_m = popt
            f_snr = f_m / sd_base if sd_base > 0 else np.nan
            f_cnr = f_amp / sd_base if sd_base > 0 else np.nan
            
            # Evaluate model
            alpha = ((f_t2p ** 2) / (f_fwhm ** 2)) * 8.0 * np.log(2.0)
            beta_param = ((f_fwhm ** 2) / f_t2p) / (8.0 * np.log(2.0))
            
            y_fit_model = np.zeros_like(t_fit)
            for idx, t_val in enumerate(t_fit):
                if t_val > 0:
                    y_fit_model[idx] = f_m + f_amp * (t_val / f_t2p)**alpha * np.exp(-(t_val - f_t2p) / beta_param)
                else:
                    y_fit_model[idx] = f_m
            
            # AUC & AUCn
            auc = np.sum(y_fit_model) - (y_fit_model[0] + y_fit_model[-1]) / 2.0
            
            min_y = np.min(y_fit_model)
            max_y = np.max(y_fit_model)
            range_y = max_y - min_y
            y_fit_model_n = (y_fit_model - min_y) / range_y if range_y > 0 else np.zeros_like(y_fit_model)
            aucn = np.sum(y_fit_model_n) - (y_fit_model_n[0] + y_fit_model_n[-1]) / 2.0
            
            # OnT
            I = np.where(y_fit_model_n < 0.1)[0]
            onset_idx = 0
            if len(I) > 0:
                diffs = np.diff(I)
                contig_idxs = np.where(diffs == 1)[0]
                if len(contig_idxs) > 0:
                    last_idx = contig_idxs[-1]
                    onset_idx = I[last_idx] + 1
                else:
                    onset_idx = I[0]
            ont = onset_idx / (fr * up_f)
            ttm = abs(f_t2p - ont)
            
            # standard error
            se_t2p = 0.0
            if pcov is not None and not np.isinf(pcov).any():
                se = np.sqrt(np.diag(pcov))
                if len(se) > 1:
                    se_t2p = se[1]
            ci_lower = f_t2p - 1.96 * se_t2p
            ci_upper = f_t2p + 1.96 * se_t2p
            ttlb = abs(ci_lower - ont)
            tthb = abs(ci_upper - ont)
            
            results.append({
                'ROI': i+1,
                'SubjNum': subj_num,
                'Exp': exp,
                'InitAmp': init_params[0],
                'InitT2p': init_params[1],
                'InitFWHM': init_params[2],
                'InitM': init_params[3],
                'InitSNR': init_snr,
                'InitCNR': init_cnr,
                'Click1_Start_T': clicks['baseline_start'][0],
                'Click2_Onset_T': clicks['onset'][0],
                'Click3_Peak_T': clicks['peak'][0],
                'Click4_End_T': clicks['end'][0],
                'F_Amp': f_amp,
                'F_T2p': f_t2p,
                'F_FWHM': f_fwhm,
                'F_M': f_m,
                'F_SNR': f_snr,
                'F_CNR': f_cnr,
                'AUC': auc,
                'AUCn': aucn,
                'TTlb': ttlb,
                'TTm': ttm,
                'TThb': tthb,
                'OnT': ont,
                'OnTSc': np.nan, # Set after the loop
                'ROISize': roi_size,
                'Denoise_RMS': denoise_rms,
                'VesType': ves_type
            })
        else:
            results.append({
                'ROI': i+1,
                'SubjNum': subj_num,
                'Exp': exp,
                'InitAmp': init_params[0],
                'InitT2p': init_params[1],
                'InitFWHM': init_params[2],
                'InitM': init_params[3],
                'InitSNR': init_snr,
                'InitCNR': init_cnr,
                'Click1_Start_T': clicks['baseline_start'][0],
                'Click2_Onset_T': clicks['onset'][0],
                'Click3_Peak_T': clicks['peak'][0],
                'Click4_End_T': clicks['end'][0],
                'F_Amp': np.nan, 'F_T2p': np.nan, 'F_FWHM': np.nan, 'F_M': np.nan, 'F_SNR': np.nan, 'F_CNR': np.nan,
                'AUC': np.nan, 'AUCn': np.nan, 'TTlb': np.nan, 'TTm': np.nan, 'TThb': np.nan, 'OnT': np.nan, 'OnTSc': np.nan,
                'ROISize': roi_size,
                'Denoise_RMS': denoise_rms,
                'VesType': ves_type
            })
            
        # --- Plotting/Screenshots ---
        if HAS_MATPLOTLIB:
            plots_dir = os.path.join(out_dir, "plots")
            os.makedirs(plots_dir, exist_ok=True)
            
            fig, ax = plt.subplots(figsize=(10, 6))
            ax.plot(tl_raw, mfi_raw, 'k.', label='Raw Data', alpha=0.4)
            ax.plot(tl_raw, mfi + k * tl_raw, 'g+', label='Denoised Data', alpha=0.6)
            ax.plot(tl_us, y_us + k * tl_us, 'b--', label='Spline Upsampled', alpha=0.6)
            
            # Plot the auto-clicks
            ax.plot(clicks['baseline_start'][0], clicks['baseline_start'][1] + k * clicks['baseline_start'][0], 'go', markersize=8, label='1: Baseline Start')
            ax.plot(clicks['onset'][0], clicks['onset'][1] + k * clicks['onset'][0], 'co', markersize=8, label='2: Bolus Onset')
            ax.plot(clicks['peak'][0], clicks['peak'][1] + k * clicks['peak'][0], 'mo', markersize=8, label='3: Peak')
            ax.plot(clicks['end'][0], clicks['end'][1] + k * clicks['end'][0], 'ro', markersize=8, label='4: Bolus End')
            
            if popt is not None:
                # evaluate the fit over the full window from baseline start to end
                t_plot = tl_us[0:end_idx] - tl_us[start_idx]
                y_fit = gamma_fun(t_plot, *popt) + k * tl_us[0:end_idx]
                ax.plot(tl_us[0:end_idx], y_fit, 'r-', linewidth=2, label='Gamma Fit')
                ax.set_title(f"ROI {i+1} Fit\nAmp={popt[0]:.2f}, T2p={popt[1]:.2f}, FWHM={popt[2]:.2f}, Base={popt[3]:.2f}")
            else:
                ax.set_title(f"ROI {i+1} Fit Failed")
                
            ax.set_xlabel('Time (s)')
            ax.set_ylabel('Fluorescence Intensity (SU)')
            ax.legend()
            
            base_name = os.path.basename(tiff_path).replace('.tif', '')
            plot_name = f"{base_name}_ROI_{i+1}_fit.png"
            plt.savefig(os.path.join(plots_dir, plot_name), dpi=150, bbox_inches='tight')
            plt.close(fig)
            
    # Calculate OnTSc (Onset time in Scan) relative to minimum OnT
    valid_onts = [r['OnT'] for r in results if not np.isnan(r['OnT'])]
    if len(valid_onts) > 0:
        min_ont = np.min(valid_onts)
        for r in results:
            if not np.isnan(r['OnT']):
                r['OnTSc'] = r['OnT'] - min_ont
                
    df = pd.DataFrame(results)
    out_csv = os.path.join(out_dir, os.path.basename(tiff_path).replace('.tif', '_results.csv'))
    df.to_csv(out_csv, index=False)
    print(f"Saved results to {out_csv}")

import re

def find_triplets(folder):
    """
    Automatically finds and pairs (TIFF, MAT, TXT) files in a folder based on 'bolusX_condition' naming.
    """
    tifs = []
    mats = []
    txts = []
    for root, _, files in os.walk(folder):
        for f in files:
            if f.startswith('.'):
                continue
            f_lower = f.lower()
            full_path = os.path.join(root, f)
            if f_lower.endswith('.tif') or f_lower.endswith('.tiff'):
                tifs.append(full_path)
            elif f_lower.startswith('adjusted_') and f_lower.endswith('.mat'):
                mats.append(full_path)
            elif f_lower.endswith('.txt') and 'rois' not in f_lower:
                txts.append(full_path)
    
    def normalize_name(s):
        return s.lower().replace('-', '_')

    triplets = []
    for tif in tifs:
        # Ignore generated mips, shift info, or results if any
        tif_lower = tif.lower()
        name = os.path.basename(tif).lower()
        if 'mips' in tif_lower or 'results' in tif_lower or 'shift_info' in tif_lower or 'max_' in name:
            continue
            
        # Extract the core bolus identifier like 'bolus1_baseline' or 'bolus2_co2'
        match = re.search(r'(bolus\d+[-_][a-z0-9]+)', name)
        if not match:
            continue
            
        norm_id = normalize_name(match.group(1))
        
        # Find matching mat (comparing normalized names)
        matching_mat = [m for m in mats if norm_id in normalize_name(os.path.basename(m))]
        # Find matching txt (comparing normalized names)
        matching_txt = [t for t in txts if norm_id in normalize_name(os.path.basename(t))]
        
        if matching_mat and matching_txt:
            triplets.append((tif, matching_mat[0], matching_txt[0]))
            
    return list(set(triplets)) # remove duplicates if any

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Batch process Bolus Tracking data")
    parser.add_argument("--folder", help="Path to a folder containing TIFFs, Masks, and TXTs to auto-detect and process")
    parser.add_argument("--tiff", help="Path to Bolus TIFF stack (if not using --folder)")
    parser.add_argument("--mask", help="Path to maskObj .mat file (if not using --folder)")
    parser.add_argument("--meta", help="Path to metadata .txt file (if not using --folder)")
    parser.add_argument("--outdir", help="Output directory", default="")
    parser.add_argument("--drift", type=float, help="Baseline duration in seconds for drift correction", default=15.0)
    parser.add_argument("--min-amp", type=float, help="Minimum amplitude constraint", default=1e-6)
    parser.add_argument("--max-amp", type=float, help="Maximum amplitude constraint", default=float('inf'))
    parser.add_argument("--min-t2p", type=float, help="Minimum Time-to-Peak constraint", default=1e-6)
    parser.add_argument("--max-t2p", type=float, help="Maximum Time-to-Peak constraint", default=float('inf'))
    parser.add_argument("--min-fwhm", type=float, help="Minimum FWHM constraint", default=1e-6)
    parser.add_argument("--max-fwhm", type=float, help="Maximum FWHM constraint", default=float('inf'))
    
    args = parser.parse_args()
    
    if args.folder:
        triplets = find_triplets(args.folder)
        if not triplets:
            print(f"No matching (TIFF, MAT, TXT) sets found in {args.folder}.")
        else:
            print(f"Found {len(triplets)} matching datasets to process.")
            for tif, mat, txt in triplets:
                try:
                    process_bolus(tif, mat, txt, args.outdir, drift_window=args.drift,
                                  min_amp=args.min_amp, max_amp=args.max_amp,
                                  min_t2p=args.min_t2p, max_t2p=args.max_t2p,
                                  min_fwhm=args.min_fwhm, max_fwhm=args.max_fwhm)
                except Exception as e:
                    print(f"Failed to process {tif}: {e}")
    elif args.tiff and args.mask and args.meta:
        process_bolus(args.tiff, args.mask, args.meta, args.outdir, drift_window=args.drift,
                      min_amp=args.min_amp, max_amp=args.max_amp,
                      min_t2p=args.min_t2p, max_t2p=args.max_t2p,
                      min_fwhm=args.min_fwhm, max_fwhm=args.max_fwhm)
    else:
        print("Please provide either --folder OR --tiff, --mask, and --meta arguments.")
