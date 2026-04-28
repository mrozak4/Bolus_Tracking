import numpy as np
from scipy.optimize import curve_fit

def gamma_fun(t, a1, peak1, fwhm1, m):
    """
    Python equivalent of gammaFun.m
    """
    # Prevent division by zero or negative values for fractional power
    t_safe = np.maximum(t, 1e-9)
    peak1_safe = np.maximum(peak1, 1e-9)
    fwhm1_safe = np.maximum(fwhm1, 1e-9)
    
    alpha1 = ((peak1_safe**2) / (fwhm1_safe**2)) * 8 * np.log(2)
    beta1 = ((fwhm1_safe**2) / peak1_safe) / (8 * np.log(2))
    beta1_safe = np.maximum(beta1, 1e-9)
    
    base = t_safe / peak1_safe
    out_f = a1 * (base ** alpha1) * np.exp(-(t - peak1) / beta1_safe) + m
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
    Returns: [Amplitude, TimeToPeak, FWHM, BaselineShift], start_idx, end_idx, sd_base
    """
    n_base_frames = min(round(2 * fr * up_f), round(len(tr) * 0.1))
    n_base_frames = max(1, int(n_base_frames))
    
    baseline = np.median(tr[:n_base_frames])
    max_idx = np.argmax(tr)
    max_val = tr[max_idx]
    amp = max_val - baseline
    t2p = t_us[max_idx]
    
    sd_base = np.std(tr[:n_base_frames], ddof=1) if n_base_frames > 1 else 0
    thresh = baseline + 2 * sd_base
    if sd_base == 0 or thresh >= max_val:
        thresh = baseline + 0.1 * amp
        
    start_candidates = np.where(tr[:max_idx+1] < thresh)[0]
    start_idx = start_candidates[-1] if len(start_candidates) > 0 else 0
    t_start = t_us[start_idx]
    start_amp = tr[start_idx]
    
    end_thresh = baseline + 0.1 * amp
    end_candidates = np.where(tr[max_idx:] < end_thresh)[0]
    if len(end_candidates) == 0:
        end_idx = len(tr) - 1
    else:
        end_idx = end_candidates[0] + max_idx
    t_end = t_us[end_idx]
    end_amp = tr[end_idx]
    
    half_max = baseline + 0.5 * amp
    idx_up_candidates = np.where(tr[:max_idx+1] >= half_max)[0]
    idx_up = idx_up_candidates[0] if len(idx_up_candidates) > 0 else start_idx
    
    idx_down_candidates = np.where(tr[max_idx:] <= half_max)[0]
    if len(idx_down_candidates) == 0:
        t_down = t_end
    else:
        t_down = t_us[idx_down_candidates[0] + max_idx]
        
    t_up = t_us[idx_up]
    fwhm = t_down - t_up
    if fwhm <= 0:
        fwhm = 0.5
        
    bsln_shift = end_amp - start_amp
    
    return [amp, t2p, fwhm, bsln_shift], start_idx, end_idx, sd_base

def fit_bolus(t, y, params_init):
    """
    Fits the gamma function using scipy curve_fit with cauchy loss.
    This mimics MATLAB's nlinfit with statset('RobustWgtFun','cauchy').
    """
    try:
        # MATLAB default bounds for parameters are usually unconstrained, 
        # but constraining amplitude and fwhm to positive helps stability.
        bounds = (
            [1e-6, 1e-6, 1e-6, -np.inf], # Lower bounds: [Amp, T2p, FWHM, Baseline]
            [np.inf, np.inf, np.inf, np.inf] # Upper bounds
        )
        
        popt, pcov = curve_fit(
            gamma_fun, t, y, 
            p0=params_init, 
            method='trf', 
            loss='cauchy', # Matches RobustWgtFun = 'cauchy'
            f_scale=2.3849, # MATLAB default tuning constant for Cauchy
            bounds=bounds,
            maxfev=10000
        )
        return popt, pcov
    except Exception as e:
        return None, None
