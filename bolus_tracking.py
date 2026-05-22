"""
Object-Oriented Bolus Tracking Core Pipeline Module.
Contains SignalProcessor, BolusModel, and BolusFitter classes, along with backward-compatible function wrappers.
"""

from typing import Union, Dict, Tuple, List, Optional
import numpy as np
from scipy.optimize import curve_fit


class SignalProcessor:
    """
    Utility class for signal processing tasks, including denoising, filtering,
    and statistical operations on time-series traces.
    """
    
    @staticmethod
    def denoise_trace(trace: np.ndarray, denoise_sd: float = 2.0, half_win: int = 5) -> np.ndarray:
        """
        Applies temporal denoising identical to MATLAB's implementation.
        Compares each data point to the median of a surrounding local window
        and replaces statistical outliers with the local median.
        
        Args:
            trace (np.ndarray): The input raw signal trace.
            denoise_sd (float): Standard deviation multiplier threshold.
            half_win (int): Half-width of the sliding local window.
            
        Returns:
            np.ndarray: The denoised trace.
        """
        n_pts = len(trace)
        clean_trace = np.copy(trace)
        
        for p in range(n_pts):
            w_start = max(0, p - half_win)
            w_end = min(n_pts, p + half_win + 1)
            
            # Exclude the current point from the window
            window = np.concatenate([trace[w_start:p], trace[p+1:w_end]])
            if len(window) == 0:
                continue
                
            local_median = np.median(window)
            local_sd = np.std(window, ddof=1)  # ddof=1 matches MATLAB's std()
            
            if np.abs(trace[p] - local_median) > denoise_sd * local_sd:
                clean_trace[p] = local_median
                
        return clean_trace


class BolusModel:
    """
    Represents the mathematical formulation of the Gamma-variate bolus curve model.
    """
    
    @staticmethod
    def evaluate(t: Union[np.ndarray, float, List[float]], a1: float, peak1: float, fwhm1: float, m: float) -> np.ndarray:
        """
        Evaluates the Gamma function at time(s) t.
        
        Formula:
            f(t) = a1 * (t / peak1) ^ alpha1 * exp(-(t - peak1) / beta1) + m
        
        Args:
            t (np.ndarray or float or list): Time points to evaluate.
            a1 (float): Amplitude of the peak above baseline.
            peak1 (float): Time-to-peak (T2P).
            fwhm1 (float): Full Width at Half Maximum (FWHM).
            m (float): Baseline shift parameter.
            
        Returns:
            np.ndarray: The evaluated model values.
        """
        t_arr = np.asarray(t)
        out_f = np.full_like(t_arr, m, dtype=float)
        
        pos_mask = t_arr > 0
        t_pos = t_arr[pos_mask]
        
        if len(t_pos) > 0:
            # Prevent division by zero or negative values for fractional power
            peak1_safe = np.maximum(peak1, 1e-9)
            fwhm1_safe = np.maximum(fwhm1, 1e-9)
            
            alpha1 = ((peak1_safe**2) / (fwhm1_safe**2)) * 8 * np.log(2)
            beta1 = ((fwhm1_safe**2) / peak1_safe) / (8 * np.log(2))
            beta1_safe = np.maximum(beta1, 1e-9)
            
            base = t_pos / peak1_safe
            out_f[pos_mask] = a1 * (base ** alpha1) * np.exp(-(t_pos - peak1) / beta1_safe) + m
            
        return out_f


class BolusFitter:
    """
    Manages automated initial parameter estimation and two-pass robust curve fitting
    for Bolus tracking.
    """
    
    @staticmethod
    def is_near_bounds(val: float, low: float, high: float) -> bool:
        if np.isnan(val):
            return True
        if np.abs(val - low) < 1e-4:
            return True
        if low > 0.0 and val <= low * 1.01:
            return True
        if high > 0.0 and not np.isinf(high) and high < 99999.0:
            if np.abs(high - val) < 1e-4:
                return True
            if val >= high * 0.99:
                return True
        return False

    @staticmethod
    def determine_qc_flag(
        f_amp: float, f_t2p: float, f_fwhm: float, f_m: float, f_cnr: float,
        min_amp: float, max_amp: float, min_t2p: float, max_t2p: float,
        min_fwhm: float, max_fwhm: float, fit_success: bool, pass2_run: bool = False
    ) -> str:
        if not fit_success or np.isnan([f_amp, f_t2p, f_fwhm, f_m, f_cnr]).any():
            return "FAIL"
        if f_cnr < 3.0:
            return "FAIL"
        if pass2_run:
            near_bounds = (BolusFitter.is_near_bounds(f_amp, 1.0, max_amp) or
                           BolusFitter.is_near_bounds(f_t2p, 0.01, 12.0) or
                           BolusFitter.is_near_bounds(f_fwhm, 0.1, 20.0))
        else:
            near_bounds = (BolusFitter.is_near_bounds(f_amp, min_amp, max_amp) or
                           BolusFitter.is_near_bounds(f_t2p, min_t2p, max_t2p) or
                           BolusFitter.is_near_bounds(f_fwhm, min_fwhm, max_fwhm))
        inside_pass_ranges = (0.5 <= f_fwhm <= 15.0) and (0.1 <= f_t2p <= 10.0)
        
        if not near_bounds and f_cnr > 5.0 and inside_pass_ranges:
            return "PASS"
        return "WARN"

    @staticmethod
    def suggest_vessel_type(ont: float, t2p: float, fwhm: float, amp: float, qc_flag: str) -> str:
        if qc_flag == "FAIL" or np.isnan(ont) or np.isnan(t2p):
            return "U"
        ttm = np.abs(t2p - ont)
        if ont < 1.8 and ttm < 3.0:
            return "A"
        if ont > 3.0 or ttm > 4.5:
            return "V"
        return "C"

    def __init__(self, min_amp: float = 1e-6, max_amp: float = 1023.0, min_t2p: float = 1e-6, min_fwhm: float = 0.5):
        """
        Initializes the BolusFitter with constraints.
        
        Args:
            min_amp (float): Minimum allowed amplitude.
            max_amp (float): Maximum allowed amplitude.
            min_t2p (float): Minimum allowed Time-to-peak.
            min_fwhm (float): Minimum allowed FWHM.
        """
        self.min_amp = min_amp
        self.max_amp = max_amp
        self.min_t2p = min_t2p
        self.min_fwhm = min_fwhm

    def auto_estimate_params(
        self, tr: np.ndarray, t_us: np.ndarray, fr: float, up_f: int = 20, low_cnr: bool = False
    ) -> Tuple[List[float], int, int, float, Dict[str, Tuple[float, float]]]:
        """
        Auto-estimates initial guess parameters and search boundaries from the upsampled trace.
        
        Args:
            tr (np.ndarray): The upsampled raw trace.
            t_us (np.ndarray): The upsampled time points.
            fr (float): Camera frame rate.
            up_f (int): Upsampling factor.
            low_cnr (bool): Flag indicating if trace has low CNR to use wider pre-smoothing.
            
        Returns:
            tuple: (init_params, start_idx, end_idx, sd_base, clicks)
        """
        n_base_frames = min(round(2 * fr * up_f), round(len(tr) * 0.1))
        n_base_frames = max(1, int(n_base_frames))
        
        baseline = np.median(tr[:n_base_frames])
        sd_base = float(np.std(tr[:n_base_frames], ddof=1)) if n_base_frames > 1 else 0.0
        
        # Ignore points for boundary spline overshoot
        ignore_points = min(int(3.0 * fr * up_f), int(0.05 * len(tr)))
        valid_end = len(tr) - ignore_points
        
        # 1. Steepest Rise to Peak Detection
        from scipy.ndimage import gaussian_filter1d
        rise_sigma = 2.0 * fr * up_f if low_cnr else 1.0 * fr * up_f
        smoothed_rise = gaussian_filter1d(tr, rise_sigma)
        deriv_rise = np.gradient(smoothed_rise)
        
        rise_idx = np.argmax(deriv_rise[:valid_end])
        
        # Find peak in [rise_idx, rise_idx + 8s]
        search_win = round(8.0 * fr * up_f)
        peak_search_end = min(valid_end, rise_idx + search_win)
        max_idx = rise_idx + np.argmax(tr[rise_idx:peak_search_end])
        max_val = tr[max_idx]
        amp = max_val - baseline
        
        # 2. Walk backward for Onset
        thresh = baseline + max(3 * sd_base, 0.05 * amp)
        below_thresh_candidates = np.where(tr[:max_idx] < thresh)[0]
        if len(below_thresh_candidates) == 0:
            start_idx = 0
        else:
            start_idx = below_thresh_candidates[-1]
            
        # Also detect onset where the derivative drops below 15% of max_deriv
        max_deriv = deriv_rise[rise_idx]
        slope_thresh = 0.15 * max_deriv
        deriv_candidates = np.where(deriv_rise[:rise_idx+1] < slope_thresh)[0]
        if len(deriv_candidates) > 0:
            deriv_start_idx = deriv_candidates[-1]
            if deriv_start_idx > start_idx:
                if (deriv_start_idx - start_idx) / (fr * up_f) > 2.0:
                    # Fallback to amplitude threshold alone
                    pass
                else:
                    start_idx = deriv_start_idx
            
        t_start = t_us[start_idx]
        start_amp = tr[start_idx]
        
        # T2P is calculated relative to the bolus onset (t_start)
        t2p = t_us[max_idx] - t_start
        t2p = max(t2p, 0.01)
        
        # 3. Calculate FWHM
        half_max = baseline + 0.5 * amp
        idx_up_candidates = np.where(tr[:max_idx+1] >= half_max)[0]
        idx_up = idx_up_candidates[0] if len(idx_up_candidates) > 0 else start_idx
        
        idx_down_candidates = np.where(tr[max_idx:] <= half_max)[0]
        if len(idx_down_candidates) == 0:
            # Curve never returns to half max (plateau/recirculation).
            # Use derivative "knee" detection to find exactly where the rapid downslope flattens out.
            deriv1 = np.gradient(smoothed_rise)
            
            search_window_frames = round(15.0 * fr * up_f)
            end_search_idx = min(len(tr), max_idx + search_window_frames)
            
            downslope_window = deriv1[max_idx:end_search_idx]
            min_deriv = np.min(downslope_window)
            min_deriv_idx = max_idx + np.argmin(downslope_window)
            
            flatten_thresh = 0.2 * min_deriv
            flatten_candidates = np.where(deriv1[min_deriv_idx:] > flatten_thresh)[0]
            
            if len(flatten_candidates) > 0:
                knee_idx = min_deriv_idx + flatten_candidates[0]
            else:
                knee_idx = min_deriv_idx + round(2.0 * fr * up_f)
                
            knee_idx = min(knee_idx, len(tr) - 1)
            t_down = t_us[knee_idx]
            t_up = t_us[idx_up]
            fwhm = t_down - t_up
        else:
            t_down = t_us[idx_down_candidates[0] + max_idx]
            t_up = t_us[idx_up]
            fwhm = t_down - t_up
            
        if fwhm <= 0:
            fwhm = 0.5
            
        # 4. Fit End (Offset): Hybrid local-minimum capped end detection
        sigma_end = 0.8 * fr * up_f
        smoothed_end = gaussian_filter1d(tr, sigma_end)
        deriv_end = np.gradient(smoothed_end)
        
        # 4a. Find local minimum to cap the search window
        downslope_candidates = np.where(deriv_end[max_idx:valid_end] < 0)[0]
        if len(downslope_candidates) == 0:
            local_min_idx = valid_end - 1
        else:
            downslope_start = downslope_candidates[0] + max_idx
            non_decaying_candidates = np.where(deriv_end[downslope_start:valid_end] >= 0)[0]
            if len(non_decaying_candidates) == 0:
                local_min_idx = valid_end - 1
            else:
                local_min_idx = non_decaying_candidates[0] + downslope_start
                
        if local_min_idx <= max_idx:
            local_min_idx = valid_end - 1
            
        # 4b. Thresholding in the capped window [max_idx, local_min_idx]
        sigma_baseline = 1.0 * fr * up_f
        smoothed_tr = gaussian_filter1d(tr, sigma_baseline)
        n_end_frames = min(round(2 * fr * up_f), round(valid_end * 0.1))
        n_end_frames = max(1, int(n_end_frames))
        
        end_baseline = np.median(smoothed_tr[valid_end-n_end_frames:valid_end])
        end_sd_base = np.std(smoothed_tr[valid_end-n_end_frames:valid_end], ddof=1) if n_end_frames > 1 else 0
        
        end_thresh = end_baseline + max(3 * end_sd_base, 0.03 * amp)
        if end_sd_base == 0 or end_thresh >= max_val:
            end_thresh = end_baseline + 0.1 * amp
            
        above_end_thresh_candidates = np.where(smoothed_end[max_idx:local_min_idx] > end_thresh)[0]
        if len(above_end_thresh_candidates) == 0:
            end_idx = local_min_idx
        else:
            last_above_idx = above_end_thresh_candidates[-1]
            end_idx = last_above_idx + max_idx
            if end_idx >= valid_end:
                end_idx = valid_end - 1
                
        # Extend fit window by 25% of the fit duration
        fit_dur = end_idx - start_idx
        end_idx = min(len(tr) - 1, end_idx + int(np.round(0.25 * fit_dur)))
        
        t_end = t_us[end_idx]
        end_amp = tr[end_idx]
        bsln_shift = baseline
        
        clicks = {
            'baseline_start': (float(t_us[0]), float(tr[0])),
            'onset': (float(t_start), float(start_amp)),
            'peak': (float(t_us[max_idx]), float(max_val)),
            'end': (float(t_end), float(end_amp))
        }
        
        return [float(amp), float(t2p), float(fwhm), float(bsln_shift)], start_idx, end_idx, sd_base, clicks

    def fit(
        self, t: np.ndarray, y: np.ndarray, params_init: List[float], sd_base: float = 1.0,
        bounds_override: Optional[Tuple[List[float], List[float]]] = None, single_pass: Optional[bool] = None
    ) -> Tuple[np.ndarray, np.ndarray, Dict[str, bool]]:
        """
        Fits the gamma function to the raw trace using a 2-pass optimization.
        - Pass 1: Linear Least Squares to fit raw signal and estimate outlier MAD.
        - Pass 2: Cauchy robust loss scaling based on Pass 1 MAD to reduce noise influence.
        Also supports a second pass with physiological hard clamping if Pass 1 results are unphysiological.
        """
        t = np.asarray(t)
        y = np.asarray(y)
        
        if single_pass is None:
            single_pass = (bounds_override is not None)
            
        def fit_once(b_min_amp: float, b_max_amp: float, b_min_t2p: float, b_max_t2p: float, b_min_fwhm: float, b_max_fwhm: float) -> Tuple[np.ndarray, np.ndarray]:
            m_init = params_init[3]
            m_bound = max(0.5 * sd_base, 0.005 * m_init, 0.2)
            bounds = (
                [b_min_amp, b_min_t2p, b_min_fwhm, m_init - m_bound],
                [b_max_amp, b_max_t2p, b_max_fwhm, m_init + m_bound]
            )
            # Pass 1: Linear Fit
            try:
                popt_lin, _ = curve_fit(
                    BolusModel.evaluate, t, y,
                    p0=params_init,
                    method='trf',
                    loss='linear',
                    bounds=bounds,
                    maxfev=10000
                )
            except Exception:
                popt_lin = params_init
                
            # Calculate MAD of the residuals to dynamically scale the robust loss.
            res = y - BolusModel.evaluate(t, *popt_lin)
            mad = np.median(np.abs(res - np.median(res))) / 0.6745
            dynamic_f_scale = max(2.3849 * mad, 0.1)
            
            # Pass 2: Cauchy Robust Fit
            try:
                popt_cauchy, pcov = curve_fit(
                    BolusModel.evaluate, t, y,
                    p0=popt_lin,
                    method='trf',
                    loss='cauchy',
                    f_scale=dynamic_f_scale,
                    bounds=bounds,
                    maxfev=10000
                )
                return popt_cauchy, pcov
            except Exception:
                return np.full(4, np.nan), np.full((4, 4), np.nan)

        def compute_rss(p: np.ndarray) -> float:
            if np.isnan(p).any():
                return 1e30
            y_fit = BolusModel.evaluate(t, *p)
            return float(np.sum((y - y_fit) ** 2))

        # Get default limits
        t_duration = t[-1] if len(t) > 0 else 1.0
        
        max_amp_val = getattr(self, 'max_amp', 1023.0)
        max_t2p_val = min(getattr(self, 'max_t2p', t_duration), t_duration)
        max_fwhm_val = min(getattr(self, 'max_fwhm', t_duration), t_duration)
        
        if bounds_override is not None:
            min_amp_val = bounds_override[0][0]
            min_t2p_val = bounds_override[0][1]
            min_fwhm_val = bounds_override[0][2]
            max_amp_val = bounds_override[1][0]
            max_t2p_val = bounds_override[1][1]
            max_fwhm_val = bounds_override[1][2]
        else:
            min_amp_val = self.min_amp
            min_t2p_val = self.min_t2p
            min_fwhm_val = self.min_fwhm
            
        popt1, pcov1 = fit_once(min_amp_val, max_amp_val, min_t2p_val, max_t2p_val, min_fwhm_val, max_fwhm_val)
        
        if single_pass:
            return popt1, pcov1, {'pass2_run': False}
            
        near_bounds = (np.isnan(popt1).any() or
                       BolusFitter.is_near_bounds(popt1[0], min_amp_val, max_amp_val) or
                       BolusFitter.is_near_bounds(popt1[1], min_t2p_val, max_t2p_val) or
                       BolusFitter.is_near_bounds(popt1[2], min_fwhm_val, max_fwhm_val))
        
        trigger_pass2 = np.isnan(popt1).any() or near_bounds or popt1[2] > 20.0 or popt1[1] > 15.0
        if trigger_pass2:
            clamp_min_amp = 1.0
            clamp_max_amp = max(10.0 * params_init[0], 100.0)
            clamp_min_t2p = 0.01
            clamp_max_t2p = 12.0
            clamp_min_fwhm = 0.1
            clamp_max_fwhm = 20.0
            
            popt2, pcov2 = fit_once(clamp_min_amp, clamp_max_amp, clamp_min_t2p, clamp_max_t2p, clamp_min_fwhm, clamp_max_fwhm)
            
            if not np.isnan(popt2).any():
                rss1 = compute_rss(popt1)
                rss2 = compute_rss(popt2)
                use_pass2 = np.isnan(popt1).any() or near_bounds or (popt1[2] > 20.0 or popt1[1] > 15.0) or (rss2 < rss1)
                if use_pass2:
                    return popt2, pcov2, {'pass2_run': True}
                    
        return popt1, pcov1, {'pass2_run': False}


# ---------------------------------------------------------------------------
# Backward-Compatible Function Wrappers (Procedural Interface)
# ---------------------------------------------------------------------------

def gamma_fun(t: Union[np.ndarray, float, List[float]], a1: float, peak1: float, fwhm1: float, m: float) -> np.ndarray:
    """Procedural wrapper for BolusModel.evaluate."""
    return BolusModel.evaluate(t, a1, peak1, fwhm1, m)


def denoise_trace(trace: np.ndarray, denoise_sd: float = 2.0, half_win: int = 5) -> np.ndarray:
    """Procedural wrapper for SignalProcessor.denoise_trace."""
    return SignalProcessor.denoise_trace(trace, denoise_sd, half_win)


def auto_estimate_params(
    tr: np.ndarray, t_us: np.ndarray, fr: float, up_f: int = 20, low_cnr: bool = False
) -> Tuple[List[float], int, int, float, Dict[str, Tuple[float, float]]]:
    """Procedural wrapper for BolusFitter.auto_estimate_params."""
    fitter = BolusFitter()
    return fitter.auto_estimate_params(tr, t_us, fr, up_f, low_cnr)


def fit_bolus(
    t: np.ndarray, y: np.ndarray, params_init: List[float], sd_base: float = 1.0,
    bounds_override: Optional[Tuple[List[float], List[float]]] = None
) -> Tuple[np.ndarray, np.ndarray]:
    """Procedural wrapper for BolusFitter.fit."""
    fitter = BolusFitter()
    popt, pcov, _ = fitter.fit(t, y, params_init, sd_base, bounds_override)
    return popt, pcov
