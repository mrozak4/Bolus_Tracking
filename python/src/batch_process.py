"""
Object-Oriented Batch Processing Pipeline Module.
Contains MetadataParser, Rasterizer, BolusVisualizer, DatasetProcessor, and BatchProcessor classes,
along with backward-compatible function wrappers.
"""

import os
import re
import warnings
import numpy as np
import scipy.io as sio
import tifffile
import pandas as pd
from skimage.draw import polygon
from scipy.interpolate import interp1d
from bolus_tracking import SignalProcessor, BolusModel, BolusFitter

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("WARNING: matplotlib is not installed. Skipping screenshot generation.")

warnings.filterwarnings("ignore")


class MetadataParser:
    """
    Parser for Fluoview metadata files (.txt format) to extract scan frame rate.
    """
    
    @staticmethod
    def parse_frame_rate(meta_path):
        """
        Parses the Fluoview .txt metadata file to extract the camera frame rate.
        Looks for the 'T Dimension' line.
        
        Args:
            meta_path (str): Path to the metadata text file.
            
        Returns:
            float: Pre-calculated frame rate (Hz).
            
        Raises:
            ValueError: If the frame rate cannot be successfully parsed.
        """
        with open(meta_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Look for: "T Dimension" "300, 0.000 - 59.004 [s], Interval FreeRun"
        match = re.search(r'"T Dimension"\s+"(\d+),\s+([\d.]+)\s*-\s*([\d.]+)\s*\[s\]', content)
        if match:
            frames = int(match.group(1))
            t_start = float(match.group(2))
            t_end = float(match.group(3))
            fr = round(frames / (t_end - t_start), 2)
            return fr
        else:
            raise ValueError(f"Could not parse frame rate from {meta_path}")


class Rasterizer:
    """
    Converts polygon ROI vertices to binary pixel mask maps.
    """
    
    @staticmethod
    def get_mask_from_poly(poly_verts, shape):
        """
        Converts polygon vertices (x, y) to a boolean raster mask.
        
        Args:
            poly_verts (np.ndarray): N x 2 array of polygon vertices.
            shape (tuple): The image height and width (rows, cols).
            
        Returns:
            np.ndarray: Boolean 2D mask.
        """
        # poly_verts are (x, y) coordinates, so (col, row)
        # Ensure they are contiguous float arrays to avoid Cython segfaults under Rosetta
        r = np.ascontiguousarray(poly_verts[:, 1], dtype=np.float64)
        c = np.ascontiguousarray(poly_verts[:, 0], dtype=np.float64)
        
        rr, cc = polygon(r, c, shape)
        mask = np.zeros(shape, dtype=bool)
        mask[rr, cc] = True
        return mask


class BolusVisualizer:
    """
    Handles plotting and screenshot generation for raw traces, denoised traces,
    detected events, and fitted Gamma-variate curves.
    """
    
    @staticmethod
    def save_plot(tiff_path, out_dir, roi_id, tl_raw, mfi_raw, mfi, k, tl_us, y_us, clicks, popt, end_idx):
        """
        Generates and saves a matplotlib plot showing the fitting stages.
        
        Args:
            tiff_path (str): Path to original TIFF image stack.
            out_dir (str): Output directory for plots.
            roi_id (int): Logical ID of the ROI.
            tl_raw (np.ndarray): Original raw time points.
            mfi_raw (np.ndarray): Original raw MFI signal.
            mfi (np.ndarray): Denoised, detrended signal.
            k (float): Estimated linear baseline drift slope.
            tl_us (np.ndarray): Upsampled time points.
            y_us (np.ndarray): Upsampled, denoised, detrended signal.
            clicks (dict): Auto-estimated event times/intensities.
            popt (list): Optimized fit parameters.
            end_idx (int): Fitting window end index.
        """
        if not HAS_MATPLOTLIB:
            return
            
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
        
        from bolus_tracking import gamma_fun
        if popt is not None and not np.isnan(popt).any():
            # evaluate the fit over the full window from baseline start to end
            t_plot = tl_us[0:end_idx] - tl_us[clicks['onset_idx'] if 'onset_idx' in clicks else 0]
            # Wait, clicks dict holds start_idx/onset_idx or we can use the time onset to align
            # Let's search onset_idx or use a fallback
            onset_t = clicks['onset'][0]
            start_idx = np.where(tl_us >= onset_t)[0][0]
            t_plot = tl_us[0:end_idx] - tl_us[start_idx]
            y_fit = gamma_fun(t_plot, *popt) + k * tl_us[0:end_idx]
            ax.plot(tl_us[0:end_idx], y_fit, 'r-', linewidth=2, label='Gamma Fit')
            ax.set_title(f"ROI {roi_id} Fit\nAmp={popt[0]:.2f}, T2p={popt[1]:.2f}, FWHM={popt[2]:.2f}, Base={popt[3]:.2f}")
        else:
            ax.set_title(f"ROI {roi_id} Fit Failed")
            
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Fluorescence Intensity (SU)')
        ax.legend()
        
        base_name = os.path.basename(tiff_path).replace('.tif', '')
        plot_name = f"{base_name}_ROI_{roi_id}_fit.png"
        plt.savefig(os.path.join(plots_dir, plot_name), dpi=150, bbox_inches='tight')
        plt.close(fig)


class DatasetProcessor:
    """
    Main pipeline processor for loading, fitting, and exporting results for a single dataset.
    """
    
    def __init__(self, drift_window=15.0, min_amp=1e-6, max_amp=1023.0,
                 min_t2p=1e-6, max_t2p=np.inf, min_fwhm=0.5, max_fwhm=np.inf,
                 qc_cnr_min=5.0, qc_fwhm_max=15.0, qc_t2p_max=10.0,
                 qc_cnr_fail=3.0, qc_fwhm_fail=100.0, qc_t2p_fail=50.0, qc_amp_fail=1.0):
        """
        Initializes the DatasetProcessor.
        """
        self.drift_window = drift_window
        self.fitter = BolusFitter(min_amp=min_amp, max_amp=max_amp, min_t2p=min_t2p, min_fwhm=min_fwhm)
        self.max_t2p = max_t2p
        self.max_fwhm = max_fwhm
        
        self.qc_cnr_min = qc_cnr_min
        self.qc_fwhm_max = qc_fwhm_max
        self.qc_t2p_max = qc_t2p_max
        
        self.qc_cnr_fail = qc_cnr_fail
        self.qc_fwhm_fail = qc_fwhm_fail
        self.qc_t2p_fail = qc_t2p_fail
        self.qc_amp_fail = qc_amp_fail

    def process_single_roi(self, roi_id, pos, tiff_stack, img_shape, fr, up_f, tiff_path,
                           prior_t2p=None, prior_fwhm=None):
        mask = Rasterizer.get_mask_from_poly(pos, img_shape)
        
        # Calculate mean fluorescence intensity (MFI)
        mfi_raw = np.array([np.mean(frame[mask]) for frame in tiff_stack])
        
        # Create time vector
        tl_raw = np.arange(len(mfi_raw)) / fr
        
        # Compute linear drift slope k using first drift_window seconds
        drift_mask = tl_raw <= self.drift_window
        k = 0.0
        if np.sum(drift_mask) > 1:
            t_drift = tl_raw[drift_mask]
            y_drift = mfi_raw[drift_mask]
            cov = np.cov(t_drift, y_drift)
            if cov[0, 0] > 1e-9:
                k = cov[0, 1] / cov[0, 0]
                
        # Detrend raw signal before running fitting pipeline
        mfi_raw_detrended = mfi_raw - k * tl_raw
        
        # Calculate raw trace CNR
        n_base = min(round(2.0 * fr), round(len(mfi_raw) * 0.1))
        n_base = max(2, n_base)
        raw_base_win = mfi_raw_detrended[:n_base]
        raw_baseline = np.median(raw_base_win)
        raw_sd_base = np.std(raw_base_win, ddof=1) if len(raw_base_win) > 1 else 0.0
        raw_amp = np.max(mfi_raw_detrended) - raw_baseline
        raw_cnr = raw_amp / raw_sd_base if raw_sd_base > 0.0 else 0.0
        
        denoise_thresh = 2.0
        denoise_half_win = 5
        is_low_cnr = False
        if raw_cnr < 4.0:
            denoise_thresh = 1.5
            denoise_half_win = 7
            is_low_cnr = True
        elif raw_cnr >= 15.0:
            denoise_thresh = 3.0
            denoise_half_win = 3
        elif raw_cnr >= 8.0:
            denoise_thresh = 2.5
            denoise_half_win = 5
            
        mfi = SignalProcessor.denoise_trace(mfi_raw_detrended, denoise_sd=denoise_thresh, half_win=denoise_half_win)
        
        # Spline upsample
        tl_us = np.arange(len(mfi) * up_f) / (fr * up_f)
        
        spline_interp = interp1d(tl_raw, mfi, kind='cubic', fill_value='extrapolate')
        y_us = spline_interp(tl_us)
        
        # Estimate parameters
        init_params, start_idx, end_idx, sd_base, clicks = self.fitter.auto_estimate_params(y_us, tl_us, fr, up_f, low_cnr=is_low_cnr)
        
        # Adaptive Wider-Denoising Fallback for Low CNR
        init_cnr = init_params[0] / sd_base if sd_base > 0 else np.nan
        if not np.isnan(init_cnr) and init_cnr < 5.0 and not is_low_cnr:
            mfi = SignalProcessor.denoise_trace(mfi_raw_detrended, denoise_sd=1.5, half_win=7)
            spline_interp = interp1d(tl_raw, mfi, kind='cubic', fill_value='extrapolate')
            y_us = spline_interp(tl_us)
            init_params, start_idx, end_idx, sd_base, clicks = self.fitter.auto_estimate_params(y_us, tl_us, fr, up_f, low_cnr=True)
        
        # Fit Gamma Function
        t_fit = tl_us[start_idx:end_idx] - tl_us[start_idx]
        t_duration = t_fit[-1] if len(t_fit) > 0 else 1.0
        
        # Determine bounds
        if prior_t2p is not None and prior_fwhm is not None:
            # Narrow prior-based bounds
            actual_max_amp = self.fitter.max_amp
            actual_min_t2p = 0.5 * prior_t2p
            actual_max_t2p = 1.5 * prior_t2p
            actual_min_fwhm = 0.5 * prior_fwhm
            actual_max_fwhm = 1.5 * prior_fwhm
            
            fit_init_params = list(init_params)
            fit_init_params[1] = prior_t2p
            fit_init_params[2] = prior_fwhm
            
            m_init = init_params[3]
            m_bound = max(0.5 * sd_base, 0.005 * m_init, 0.2)
            bounds_override = (
                [self.fitter.min_amp, actual_min_t2p, actual_min_fwhm, m_init - m_bound],
                [actual_max_amp, actual_max_t2p, actual_max_fwhm, m_init + m_bound]
            )
        else:
            fit_init_params = init_params
            actual_max_amp = self.fitter.max_amp
            actual_min_t2p = self.fitter.min_t2p
            actual_max_t2p = min(self.max_t2p, t_duration) if self.max_t2p is not None and not np.isinf(self.max_t2p) else t_duration
            actual_min_fwhm = self.fitter.min_fwhm
            actual_max_fwhm = min(self.max_fwhm, t_duration) if self.max_fwhm is not None and not np.isinf(self.max_fwhm) else t_duration
            
            m_init = init_params[3]
            m_bound = max(0.5 * sd_base, 0.005 * m_init, 0.2)
            bounds_override = (
                [self.fitter.min_amp, actual_min_t2p, actual_min_fwhm, m_init - m_bound],
                [actual_max_amp, actual_max_t2p, actual_max_fwhm, m_init + m_bound]
            )
            
        popt, pcov, fit_info = self.fitter.fit(t_fit, y_us[start_idx:end_idx], fit_init_params, sd_base, bounds_override=bounds_override, single_pass=(prior_t2p is not None and prior_fwhm is not None))
        
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
        
        fit_valid = popt is not None and not np.isnan(popt).any()
        
        qc_flag = "FAIL"
        f_amp, f_t2p, f_fwhm, f_m, f_snr, f_cnr = [np.nan] * 6
        
        if fit_valid:
            f_amp, f_t2p, f_fwhm, f_m = popt
            f_snr = f_m / sd_base if sd_base > 0 else np.nan
            f_cnr = f_amp / sd_base if sd_base > 0 else np.nan
            
            # Use static helper for QC Flag
            qc_flag = BolusFitter.determine_qc_flag(
                f_amp, f_t2p, f_fwhm, f_m, f_cnr,
                self.fitter.min_amp, self.fitter.max_amp,
                bounds_override[0][1], bounds_override[1][1],
                bounds_override[0][2], bounds_override[1][2],
                fit_valid,
                pass2_run=fit_info.get('pass2_run', False)
            )
            
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
            
            if np.isnan([f_amp, f_t2p, f_fwhm, f_m, f_snr, f_cnr, auc, aucn, ttlb, ttm, tthb, ont]).any():
                qc_flag = "FAIL"
                
            ves_type = BolusFitter.suggest_vessel_type(ont, f_t2p, f_fwhm, f_amp, qc_flag)
            
        fit_source = 'population_prior' if prior_t2p is not None else 'auto'
        
        return {
            'ROI': roi_id,
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
            'OnTSc': np.nan,
            'ROISize': roi_size,
            'Denoise_RMS': denoise_rms,
            'VesType': ves_type,
            'QC_Flag': qc_flag,
            'Fit_Source': fit_source,
            '_popt': popt,
            '_end_idx': end_idx,
            '_tl_raw': tl_raw,
            '_mfi_raw': mfi_raw,
            '_mfi': mfi,
            '_k': k,
            '_tl_us': tl_us,
            '_y_us': y_us,
            '_clicks': clicks
        }

    def process(self, tiff_path, mask_path, meta_path, out_dir=None):
        """
        Processes a single bolus dataset.
        """
        if not out_dir:
            out_dir = os.path.dirname(tiff_path)
            
        print(f"Processing {os.path.basename(tiff_path)}...")
        print(f"Drift window duration: {self.drift_window} seconds.")
        
        # 1. Load Data
        fr = MetadataParser.parse_frame_rate(meta_path)
        tiff_stack = tifffile.imread(tiff_path)
        
        mat_data = sio.loadmat(mask_path, struct_as_record=False, squeeze_me=True)
        if 'maskObj' in mat_data:
            mask_objs = mat_data['maskObj']
        else:
            raise ValueError(f"No maskObj found in {mask_path}.")
            
        # Ensure it's iterable
        if not isinstance(mask_objs, np.ndarray):
            mask_objs = [mask_objs]
            
        img_shape = tiff_stack.shape[1:]
        up_f = 20
        
        results = []
        
        # 2. Process each ROI (Pass 1 & 2)
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
                
            rec = self.process_single_roi(i+1, pos, tiff_stack, img_shape, fr, up_f, tiff_path)
            results.append(rec)
            
        # --- Pass 3: Quality-Aware Population Priors Refitting ---
        high_quality_t2ps = []
        high_quality_fwhms = []
        for r in results:
            if r['QC_Flag'] == "PASS" and r['F_CNR'] is not None and not np.isnan(r['F_CNR']) and r['F_CNR'] > 10.0:
                high_quality_t2ps.append(r['F_T2p'])
                high_quality_fwhms.append(r['F_FWHM'])
                
        if len(high_quality_t2ps) > 0:
            median_t2p = np.median(high_quality_t2ps)
            median_fwhm = np.median(high_quality_fwhms)
            print(f"[Population Priors] Calculated scan-wide medians: median_t2p = {median_t2p:.2f} s, median_fwhm = {median_fwhm:.2f} s from {len(high_quality_t2ps)} vessels.")
            
            for i, r in enumerate(results):
                if r['QC_Flag'] in ["FAIL", "WARN"]:
                    obj = mask_objs[r['ROI'] - 1]
                    if hasattr(obj, 'poli'):
                        pos = obj.poli.Position
                    elif hasattr(obj, 'Position'):
                        pos = obj.Position
                    else:
                        continue
                    
                    refit_rec = self.process_single_roi(r['ROI'], pos, tiff_stack, img_shape, fr, up_f, tiff_path,
                                                         prior_t2p=median_t2p, prior_fwhm=median_fwhm)
                    
                    improvement = False
                    if refit_rec['QC_Flag'] == "PASS" and r['QC_Flag'] != "PASS":
                        improvement = True
                    elif refit_rec['QC_Flag'] == "WARN" and r['QC_Flag'] == "FAIL":
                        improvement = True
                    elif refit_rec['QC_Flag'] == "WARN" and r['QC_Flag'] == "WARN":
                        first_outside = (r['F_T2p'] < 0.1 or r['F_FWHM'] < 0.5)
                        refit_inside = (refit_rec['F_T2p'] >= 0.1 and refit_rec['F_FWHM'] >= 0.5)
                        if first_outside and refit_inside:
                            improvement = True
                        
                    if improvement:
                        results[i] = refit_rec
                        print(f"  ROI {r['ROI']} successfully refit with population priors: QC -> {refit_rec['QC_Flag']}")
                        
        # 3. Save plots and clean up temporary plotting keys from dict
        for r in results:
            BolusVisualizer.save_plot(tiff_path, out_dir, r['ROI'],
                                      r['_tl_raw'], r['_mfi_raw'], r['_mfi'], r['_k'],
                                      r['_tl_us'], r['_y_us'], r['_clicks'], r['_popt'], r['_end_idx'])
            # Delete private keys to avoid cluttering CSV
            for key in list(r.keys()):
                if key.startswith('_'):
                    del r[key]
                    
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
        return df


class BatchProcessor:
    """
    Crawler to detect triplets (TIFF, MAT, TXT) and process them in a batch pipeline.
    """
    
    def __init__(self, folder_path, drift_window=15.0, min_amp=1e-6, max_amp=1023.0,
                 min_t2p=1e-6, max_t2p=np.inf, min_fwhm=0.5, max_fwhm=np.inf,
                 qc_cnr_min=5.0, qc_fwhm_max=15.0, qc_t2p_max=10.0,
                 qc_cnr_fail=3.0, qc_fwhm_fail=100.0, qc_t2p_fail=50.0, qc_amp_fail=1.0):
        """
        Initializes the BatchProcessor.
        """
        self.folder_path = folder_path
        self.processor = DatasetProcessor(
            drift_window=drift_window,
            min_amp=min_amp,
            max_amp=max_amp,
            min_t2p=min_t2p,
            max_t2p=max_t2p,
            min_fwhm=min_fwhm,
            max_fwhm=max_fwhm,
            qc_cnr_min=qc_cnr_min,
            qc_fwhm_max=qc_fwhm_max,
            qc_t2p_max=qc_t2p_max,
            qc_cnr_fail=qc_cnr_fail,
            qc_fwhm_fail=qc_fwhm_fail,
            qc_t2p_fail=qc_t2p_fail,
            qc_amp_fail=qc_amp_fail
        )

    @staticmethod
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
        
        def get_top_relative_dir(file_path, base_folder):
            rel = os.path.relpath(os.path.abspath(file_path), os.path.abspath(base_folder))
            parts = rel.split(os.sep)
            if len(parts) > 1 and parts[0] != '..':
                return parts[0]
            return ""

        def normalize_name(s):
            return s.lower().replace('-', '_')

        triplets = []
        for tif in tifs:
            tif_lower = tif.lower()
            name = os.path.basename(tif).lower()
            if 'mips' in tif_lower or 'results' in tif_lower or 'shift_info' in tif_lower or 'max_' in name:
                continue
                
            match = re.search(r'(bolus\d+[-_][a-z0-9]+)', name)
            if not match:
                continue
                
            norm_id = normalize_name(match.group(1))
            tif_subj = get_top_relative_dir(tif, folder)
            
            matching_mat = [m for m in mats if norm_id in normalize_name(os.path.basename(m)) 
                            and (not tif_subj or get_top_relative_dir(m, folder) == tif_subj)]
            matching_txt = [t for t in txts if norm_id in normalize_name(os.path.basename(t)) 
                            and (not tif_subj or get_top_relative_dir(t, folder) == tif_subj)]
            
            if matching_mat and matching_txt:
                triplets.append((tif, matching_mat[0], matching_txt[0]))
                
        return list(set(triplets))

    def run(self, out_dir=""):
        """
        Runs the batch processor over the configured folder.
        """
        triplets = self.find_triplets(self.folder_path)
        if not triplets:
            print(f"No matching (TIFF, MAT, TXT) sets found in {self.folder_path}.")
        else:
            print(f"Found {len(triplets)} matching datasets to process.")
            for tif, mat, txt in triplets:
                try:
                    self.processor.process(tif, mat, txt, out_dir)
                except Exception as e:
                    print(f"Failed to process {tif}: {e}")


# ---------------------------------------------------------------------------
# Backward-Compatible Function Wrappers (Procedural Interface)
# ---------------------------------------------------------------------------

def parse_metadata(meta_path):
    """Procedural wrapper for MetadataParser.parse_frame_rate."""
    return MetadataParser.parse_frame_rate(meta_path)


def get_mask_from_poly(poly_verts, shape):
    """Procedural wrapper for Rasterizer.get_mask_from_poly."""
    return Rasterizer.get_mask_from_poly(poly_verts, shape)


def find_triplets(folder):
    """Procedural wrapper for BatchProcessor.find_triplets."""
    return BatchProcessor.find_triplets(folder)


def process_bolus(tiff_path, mask_path, meta_path, out_dir, drift_window=15.0,
                  min_amp=1e-6, max_amp=1023.0, min_t2p=1e-6, max_t2p=np.inf,
                  min_fwhm=0.5, max_fwhm=np.inf,
                  qc_cnr_min=5.0, qc_fwhm_max=15.0, qc_t2p_max=10.0,
                  qc_cnr_fail=3.0, qc_fwhm_fail=100.0, qc_t2p_fail=50.0, qc_amp_fail=1.0):
    """Procedural wrapper for DatasetProcessor.process."""
    dp = DatasetProcessor(
        drift_window=drift_window,
        min_amp=min_amp,
        max_amp=max_amp,
        min_t2p=min_t2p,
        max_t2p=max_t2p,
        min_fwhm=min_fwhm,
        max_fwhm=max_fwhm,
        qc_cnr_min=qc_cnr_min,
        qc_fwhm_max=qc_fwhm_max,
        qc_t2p_max=qc_t2p_max,
        qc_cnr_fail=qc_cnr_fail,
        qc_fwhm_fail=qc_fwhm_fail,
        qc_t2p_fail=qc_t2p_fail,
        qc_amp_fail=qc_amp_fail
    )
    return dp.process(tiff_path, mask_path, meta_path, out_dir)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Batch process Bolus Tracking data")
    parser.add_argument("--folder", help="Path to a folder containing TIFFs, Masks, and TXTs to auto-detect and process")
    parser.add_argument("--tiff", help="Path to Bolus TIFF stack (if not using --folder)")
    parser.add_argument("--mask", help="Path to maskObj .mat file (if not using --folder)")
    parser.add_argument("--meta", help="Path to metadata .txt file (if not using --folder)")
    parser.add_argument("--outdir", help="Output directory", default="")
    parser.add_argument("--drift", type=float, help="Baseline duration in seconds for drift correction", default=15.0)
    parser.add_argument("--min-amp", type=float, help="Minimum amplitude constraint", default=1e-6)
    parser.add_argument("--max-amp", type=float, help="Maximum amplitude constraint", default=1023.0)
    parser.add_argument("--min-t2p", type=float, help="Minimum Time-to-Peak constraint", default=1e-6)
    parser.add_argument("--max-t2p", type=float, help="Maximum Time-to-Peak constraint", default=float('inf'))
    parser.add_argument("--min-fwhm", type=float, help="Minimum FWHM constraint", default=0.5)
    parser.add_argument("--max-fwhm", type=float, help="Maximum FWHM constraint", default=float('inf'))
    
    # QC threshold arguments
    parser.add_argument("--qc-cnr-min", type=float, help="QC flag warning threshold for CNR", default=5.0)
    parser.add_argument("--qc-fwhm-max", type=float, help="QC flag warning threshold for FWHM", default=15.0)
    parser.add_argument("--qc-t2p-max", type=float, help="QC flag warning threshold for T2p", default=10.0)
    
    parser.add_argument("--qc-cnr-fail", type=float, help="QC flag failure threshold for CNR", default=3.0)
    parser.add_argument("--qc-fwhm-fail", type=float, help="QC flag failure threshold for FWHM", default=100.0)
    parser.add_argument("--qc-t2p-fail", type=float, help="QC flag failure threshold for T2p", default=50.0)
    parser.add_argument("--qc-amp-fail", type=float, help="QC flag failure threshold for amplitude", default=1.0)
    
    args = parser.parse_args()
    
    if args.folder:
        bp = BatchProcessor(
            args.folder,
            drift_window=args.drift,
            min_amp=args.min_amp,
            max_amp=args.max_amp,
            min_t2p=args.min_t2p,
            max_t2p=args.max_t2p,
            min_fwhm=args.min_fwhm,
            max_fwhm=args.max_fwhm,
            qc_cnr_min=args.qc_cnr_min,
            qc_fwhm_max=args.qc_fwhm_max,
            qc_t2p_max=args.qc_t2p_max,
            qc_cnr_fail=args.qc_cnr_fail,
            qc_fwhm_fail=args.qc_fwhm_fail,
            qc_t2p_fail=args.qc_t2p_fail,
            qc_amp_fail=args.qc_amp_fail
        )
        bp.run(args.outdir)
    elif args.tiff and args.mask and args.meta:
        process_bolus(
            args.tiff, args.mask, args.meta, args.outdir,
            drift_window=args.drift,
            min_amp=args.min_amp, max_amp=args.max_amp,
            min_t2p=args.min_t2p, max_t2p=args.max_t2p,
            min_fwhm=args.min_fwhm, max_fwhm=args.max_fwhm,
            qc_cnr_min=args.qc_cnr_min,
            qc_fwhm_max=args.qc_fwhm_max,
            qc_t2p_max=args.qc_t2p_max,
            qc_cnr_fail=args.qc_cnr_fail,
            qc_fwhm_fail=args.qc_fwhm_fail,
            qc_t2p_fail=args.qc_t2p_fail,
            qc_amp_fail=args.qc_amp_fail
        )
    else:
        print("Please provide either --folder OR --tiff, --mask, and --meta arguments.")
