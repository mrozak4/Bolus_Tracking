import numpy as np
from scipy.optimize import curve_fit

def gamma_fun(t, a1, peak1, fwhm1, m):
    """
    Python equivalent of gammaFun.m
    """
    t = np.asarray(t)
    out_f = np.full_like(t, m, dtype=float)
    
    pos_mask = t > 0
    t_pos = t[pos_mask]
    
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

def denoise_trace(trace, denoise_sd=2.0, half_win=5):
    """
    Temporal denoising identical to MATLAB's implementation.
    Compares each point to the median of a local window.
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
        local_sd = np.std(window, ddof=1) # ddof=1 matches MATLAB's std()
        
        if np.abs(trace[p] - local_median) > denoise_sd * local_sd:
            clean_trace[p] = local_median
            
    return clean_trace

def auto_estimate_params(tr, t_us, fr, up_f=20):
    """
    Auto-estimates the initial fitting parameters from the trace.
    Returns: [Amplitude, TimeToPeak, FWHM, BaselineShift], start_idx, end_idx, sd_base, clicks
    """
    n_base_frames = min(round(2 * fr * up_f), round(len(tr) * 0.1))
    n_base_frames = max(1, int(n_base_frames))
    
    baseline = np.median(tr[:n_base_frames])
    sd_base = np.std(tr[:n_base_frames], ddof=1) if n_base_frames > 1 else 0
    
    # Ignore points for boundary spline overshoot
    ignore_points = min(int(3.0 * fr * up_f), int(0.05 * len(tr)))
    valid_end = len(tr) - ignore_points
    
    # 1. Steepest Rise to Peak Detection
    from scipy.ndimage import gaussian_filter1d
    smoothed_rise = gaussian_filter1d(tr, 1.0 * fr * up_f)
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
        
    # Also detect onset where the derivative drops below 15% of max_deriv (robust to baseline drift)
    slope_thresh = 0.15 * max_deriv
    deriv_candidates = np.where(deriv_rise[:rise_idx+1] < slope_thresh)[0]
    if len(deriv_candidates) > 0:
        deriv_start_idx = deriv_candidates[-1]
        if deriv_start_idx > start_idx:
            start_idx = deriv_start_idx
        
    t_start = t_us[start_idx]
    start_amp = tr[start_idx]
    
    # In MATLAB, T2p is calculated relative to the bolus onset (t_start)
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
            
    t_end = t_us[end_idx]
    end_amp = tr[end_idx]
    bsln_shift = baseline
    
    clicks = {
        'baseline_start': (t_us[0], tr[0]),
        'onset': (t_start, start_amp),
        'peak': (t_us[max_idx], max_val),
        'end': (t_end, end_amp)
    }
    
    return [amp, t2p, fwhm, bsln_shift], start_idx, end_idx, sd_base, clicks

def fit_bolus(t, y, params_init, sd_base=1.0):
    """
    Fits the gamma function using a 2-pass optimization to match MATLAB's IRLS.
    Pass 1: Linear Least Squares to capture the global shape and estimate the residual MAD.
    Pass 2: Cauchy Robust loss using the MAD as the robust scaling factor.
    """
    try:
        # Tightly constrain the baseline shift 'm' to prevent the optimizer from 
        # jumping to the plateau, and constrain Amplitude and FWHM to positive.
        m_init = params_init[3]
        m_bound = max(0.5 * sd_base, 0.005 * m_init, 0.2)
        bounds = (
            [1e-6, 1e-6, 1e-6, m_init - m_bound], 
            [np.inf, np.inf, np.inf, m_init + m_bound]
        )
        
        # Pass 1: Linear Fit
        try:
            popt_lin, _ = curve_fit(
                gamma_fun, t, y, 
                p0=params_init, 
                method='trf', 
                loss='linear', 
                bounds=bounds,
                maxfev=10000
            )
        except:
            popt_lin = params_init
            
        # Calculate MAD of the residuals to dynamically scale the robust loss.
        # This exactly matches MATLAB's nlinfit dynamic weight scaling.
        res = y - gamma_fun(t, *popt_lin)
        mad = np.median(np.abs(res - np.median(res))) / 0.6745
        dynamic_f_scale = max(2.3849 * mad, 0.1)
        
        # Pass 2: Cauchy Robust Fit
        popt_cauchy, pcov = curve_fit(
            gamma_fun, t, y, 
            p0=popt_lin, 
            method='trf', 
            loss='cauchy', 
            f_scale=dynamic_f_scale,
            bounds=bounds,
            maxfev=10000
        )
        return popt_cauchy, pcov
    except Exception as e:
        return None, None
