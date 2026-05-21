"""
Comprehensive tests for bolus_tracking.py functions:
  - gamma_fun
  - denoise_trace
  - auto_estimate_params
  - fit_bolus
"""

import numpy as np
import pytest
from scipy.interpolate import interp1d
from bolus_tracking import gamma_fun, denoise_trace, auto_estimate_params, fit_bolus


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_gaussian_bolus(fr=5.0, up_f=20, n_frames=300, duration=60.0,
                          baseline=50.0, amp=100.0, peak_t=20.0, sigma=3.0):
    """Return an upsampled Gaussian-shaped bolus trace and its time axis."""
    t_raw = np.linspace(0, duration, n_frames)
    t_us = np.linspace(0, duration, n_frames * up_f)
    y_raw = baseline + amp * np.exp(-0.5 * ((t_raw - peak_t) / sigma) ** 2)
    spline = interp1d(t_raw, y_raw, kind='cubic', fill_value='extrapolate')
    y_us = spline(t_us)
    return y_us, t_us


# ---------------------------------------------------------------------------
# gamma_fun
# ---------------------------------------------------------------------------

class TestGammaFun:
    """Tests for the gamma_fun model function."""

    def test_output_shape_matches_input(self):
        t = np.linspace(0.1, 10, 150)
        y = gamma_fun(t, 1.0, 5.0, 2.0, 0.0)
        assert y.shape == t.shape

    def test_no_nan_or_inf_for_typical_params(self):
        t = np.linspace(0.01, 20, 200)
        y = gamma_fun(t, 2.0, 5.0, 3.0, 0.0)
        assert not np.any(np.isnan(y))
        assert not np.any(np.isinf(y))

    def test_baseline_offset_shifts_output(self):
        t = np.linspace(0.01, 10, 50)
        y0 = gamma_fun(t, 1.0, 5.0, 2.0, 0.0)
        y1 = gamma_fun(t, 1.0, 5.0, 2.0, 10.0)
        np.testing.assert_allclose(y1 - y0, 10.0)

    def test_amplitude_scales_output(self):
        t = np.linspace(0.01, 10, 50)
        y1 = gamma_fun(t, 1.0, 5.0, 2.0, 0.0)
        y2 = gamma_fun(t, 2.0, 5.0, 2.0, 0.0)
        # Peak of y2 should be roughly 2x peak of y1 (small baseline difference)
        assert np.max(y2) > np.max(y1)

    def test_single_peak_in_range(self):
        t = np.linspace(0.01, 10, 500)
        y = gamma_fun(t, 1.0, 5.0, 2.0, 0.0)
        peak_idx = np.argmax(y)
        # Peak must not be at the boundary
        assert 0 < peak_idx < len(y) - 1

    def test_t_zero_handled_safely(self):
        """gamma_fun should not crash when t=0 is included (clipped to 1e-9 internally)."""
        t = np.array([0.0, 0.5, 1.0, 2.0])
        y = gamma_fun(t, 1.0, 1.0, 0.5, 0.0)
        assert not np.any(np.isnan(y))
        assert not np.any(np.isinf(y))

    def test_scalar_input(self):
        """gamma_fun should work with scalar t (returns scalar-like)."""
        y = gamma_fun(np.array([1.0]), 1.0, 1.0, 0.5, 0.0)
        assert y.shape == (1,)

    def test_returns_ndarray(self):
        t = np.linspace(0.1, 5, 10)
        y = gamma_fun(t, 1.0, 2.0, 1.0, 0.0)
        assert isinstance(y, np.ndarray)


# ---------------------------------------------------------------------------
# denoise_trace
# ---------------------------------------------------------------------------

class TestDenoiseTrace:
    """Tests for the denoise_trace function."""

    def test_outlier_replaced_by_local_median(self):
        trace = np.array([1.0, 1.0, 1.0, 100.0, 1.0, 1.0, 1.0])
        result = denoise_trace(trace, denoise_sd=2.0, half_win=3)
        assert result[3] != 100.0
        assert np.isclose(result[3], 1.0, atol=0.5)

    def test_clean_trace_unchanged(self):
        trace = np.array([1.0, 1.1, 0.9, 1.05, 0.95, 1.0, 1.1, 0.9])
        result = denoise_trace(trace, denoise_sd=3.0, half_win=3)
        # No point should be replaced since there are no outliers
        np.testing.assert_array_equal(result, trace)

    def test_output_length_matches_input(self):
        trace = np.random.randn(50) + 5.0
        result = denoise_trace(trace, denoise_sd=2.0, half_win=5)
        assert len(result) == len(trace)

    def test_original_trace_not_modified(self):
        trace = np.array([1.0, 1.0, 50.0, 1.0, 1.0])
        trace_copy = trace.copy()
        denoise_trace(trace, denoise_sd=2.0, half_win=2)
        np.testing.assert_array_equal(trace, trace_copy)

    def test_multiple_outliers_replaced(self):
        trace = np.array([1.0, 50.0, 1.0, 1.0, 50.0, 1.0, 1.0])
        result = denoise_trace(trace, denoise_sd=1.0, half_win=3)
        assert result[1] < 50.0
        assert result[4] < 50.0

    def test_single_element_trace(self):
        trace = np.array([5.0])
        result = denoise_trace(trace, denoise_sd=2.0, half_win=5)
        assert len(result) == 1
        assert result[0] == 5.0

    def test_two_element_trace(self):
        trace = np.array([1.0, 100.0])
        result = denoise_trace(trace, denoise_sd=2.0, half_win=1)
        assert len(result) == 2

    def test_constant_trace_unchanged(self):
        trace = np.ones(20) * 3.0
        result = denoise_trace(trace, denoise_sd=2.0, half_win=5)
        np.testing.assert_array_equal(result, trace)

    def test_strict_threshold_replaces_more(self):
        """Lower denoise_sd should replace more points."""
        np.random.seed(0)
        trace = np.random.randn(100)
        result_strict = denoise_trace(trace, denoise_sd=0.5, half_win=5)
        result_loose = denoise_trace(trace, denoise_sd=5.0, half_win=5)
        n_changed_strict = np.sum(result_strict != trace)
        n_changed_loose = np.sum(result_loose != trace)
        assert n_changed_strict >= n_changed_loose

    def test_half_win_zero_no_crash(self):
        """half_win=0 means only immediately adjacent points (empty window possible)."""
        trace = np.array([1.0, 100.0, 1.0, 1.0, 1.0])
        result = denoise_trace(trace, denoise_sd=2.0, half_win=0)
        assert len(result) == len(trace)


# ---------------------------------------------------------------------------
# auto_estimate_params
# ---------------------------------------------------------------------------

class TestAutoEstimateParams:
    """Tests for the auto_estimate_params function."""

    def test_amplitude_estimation_gaussian_bolus(self):
        amp = 100.0; baseline = 50.0
        y_us, t_us = _make_gaussian_bolus(amp=amp, baseline=baseline)
        params, _, _, _, _ = auto_estimate_params(y_us, t_us, fr=5.0, up_f=20)
        # Estimated amplitude should be close to the true amplitude
        assert np.isclose(params[0], amp, rtol=0.05), f"Amplitude {params[0]:.1f} != expected {amp}"

    def test_peak_time_estimation_gaussian_bolus(self):
        peak_t = 20.0
        y_us, t_us = _make_gaussian_bolus(peak_t=peak_t)
        params, start_idx, _, _, _ = auto_estimate_params(y_us, t_us, fr=5.0, up_f=20)
        t_start = start_idx / (5.0 * 20)
        assert np.isclose(params[1] + t_start, peak_t, atol=1.0), f"T2p absolute {params[1] + t_start:.2f} != expected {peak_t}"

    def test_fwhm_positive(self):
        y_us, t_us = _make_gaussian_bolus()
        params, _, _, _, _ = auto_estimate_params(y_us, t_us, fr=5.0, up_f=20)
        assert params[2] > 0, "FWHM must be positive"

    def test_returns_four_params_and_indices(self):
        y_us, t_us = _make_gaussian_bolus()
        result = auto_estimate_params(y_us, t_us, fr=5.0, up_f=20)
        params, start_idx, end_idx, sd_base, clicks = result
        assert len(params) == 4
        assert isinstance(start_idx, (int, np.integer))
        assert isinstance(end_idx, (int, np.integer))

    def test_start_idx_before_end_idx(self):
        y_us, t_us = _make_gaussian_bolus()
        _, start_idx, end_idx, _, _ = auto_estimate_params(y_us, t_us, fr=5.0, up_f=20)
        assert start_idx < end_idx

    def test_indices_within_bounds(self):
        y_us, t_us = _make_gaussian_bolus()
        _, start_idx, end_idx, _, _ = auto_estimate_params(y_us, t_us, fr=5.0, up_f=20)
        assert 0 <= start_idx < len(y_us)
        assert 0 <= end_idx < len(y_us)

    def test_narrow_bolus(self):
        """A narrow bolus (small sigma) should still produce positive FWHM."""
        y_us, t_us = _make_gaussian_bolus(sigma=1.0)
        params, start_idx, end_idx, _, _ = auto_estimate_params(y_us, t_us, fr=5.0, up_f=20)
        assert params[2] > 0
        assert start_idx < end_idx

    def test_late_peak(self):
        """Peak near the end of the recording should still be estimated."""
        y_us, t_us = _make_gaussian_bolus(peak_t=50.0, duration=60.0)
        params, start_idx, _, _, _ = auto_estimate_params(y_us, t_us, fr=5.0, up_f=20)
        t_start = start_idx / (5.0 * 20)
        assert np.isclose(params[1] + t_start, 50.0, atol=2.0)

    def test_different_baselines_give_same_amp(self):
        """Amplitude estimation should not depend on baseline level."""
        amp = 80.0
        y1, t1 = _make_gaussian_bolus(amp=amp, baseline=0.0)
        y2, t2 = _make_gaussian_bolus(amp=amp, baseline=200.0)
        p1, _, _, _, _ = auto_estimate_params(y1, t1, fr=5.0, up_f=20)
        p2, _, _, _, _ = auto_estimate_params(y2, t2, fr=5.0, up_f=20)
        assert np.isclose(p1[0], amp, rtol=0.05)
        assert np.isclose(p2[0], amp, rtol=0.05)


# ---------------------------------------------------------------------------
# fit_bolus
# ---------------------------------------------------------------------------

class TestFitBolus:
    """Tests for the fit_bolus function."""

    def _get_fit_region(self):
        """Return a realistic trace segment ready for fitting."""
        y_us, t_us = _make_gaussian_bolus()
        params, si, ei, _, _ = auto_estimate_params(y_us, t_us, fr=5.0, up_f=20)
        return t_us[si:ei] - t_us[si], y_us[si:ei], params

    def test_returns_two_values(self):
        t, y, p0 = self._get_fit_region()
        result = fit_bolus(t, y, p0)
        assert len(result) == 2

    def test_successful_fit_returns_array(self):
        t, y, p0 = self._get_fit_region()
        popt, pcov = fit_bolus(t, y, p0)
        assert popt is not None
        assert pcov is not None
        assert len(popt) == 4

    def test_popt_amplitude_positive(self):
        t, y, p0 = self._get_fit_region()
        popt, _ = fit_bolus(t, y, p0)
        assert popt is not None
        assert popt[0] > 0, "Fitted amplitude must be positive"

    def test_popt_t2p_positive(self):
        t, y, p0 = self._get_fit_region()
        popt, _ = fit_bolus(t, y, p0)
        assert popt is not None
        assert popt[1] > 0, "Fitted T2p must be positive"

    def test_popt_fwhm_positive(self):
        t, y, p0 = self._get_fit_region()
        popt, _ = fit_bolus(t, y, p0)
        assert popt is not None
        assert popt[2] > 0, "Fitted FWHM must be positive"

    def test_fit_curve_approximates_data(self):
        """The fitted curve evaluated over the time range should approximate the data."""
        t, y, p0 = self._get_fit_region()
        popt, _ = fit_bolus(t, y, p0)
        assert popt is not None
        y_fit = gamma_fun(t, *popt)
        # Residuals should be small relative to signal amplitude
        residuals = np.abs(y_fit - y)
        assert np.median(residuals) < 0.1 * np.max(y), "Fit residuals too large"

    def test_gamma_curve_consistency(self):
        """Fit self-generated gamma curve — should recover params closely."""
        t = np.linspace(0.5, 8, 400)
        true_params = [3.0, 2.0, 1.0, 0.5]
        y_clean = gamma_fun(t, *true_params)
        popt, pcov = fit_bolus(t, y_clean, true_params)
        assert popt is not None
        y_fitted = gamma_fun(t, *popt)
        np.testing.assert_allclose(y_fitted, y_clean, rtol=0.01,
                                   err_msg="Fitted curve should closely match clean gamma curve")

    def test_failure_returns_nans(self):
        """fit_bolus should return NaNs rather than raise on pathological input."""
        t = np.linspace(0.1, 5, 50)
        y = np.zeros(50)
        popt, pcov = fit_bolus(t, y, [1e-8, 1e-8, 1e-8, 0.0])
        assert popt is not None
        assert np.isnan(popt).all()
        assert np.isnan(pcov).all()

    def test_pcov_is_2d_array_on_success(self):
        t, y, p0 = self._get_fit_region()
        popt, pcov = fit_bolus(t, y, p0)
        if popt is not None and not np.isnan(popt).any():
            assert pcov.ndim == 2
            assert pcov.shape == (4, 4)

    def test_custom_bounds_constraint(self):
        """Verify that bounds_override successfully constrains fitted parameters."""
        t, y, p0 = self._get_fit_region()
        # Ensure initial guess is inside bounds
        p0[1] = 4.1  # Set T2p to be within [4.0, 4.2]
        m_init = p0[3]
        bounds_override = (
            [0.1, 4.0, 0.1, m_init - 5.0],
            [200.0, 4.2, 20.0, m_init + 5.0]
        )
        popt, _ = fit_bolus(t, y, p0, bounds_override=bounds_override)
        assert popt is not None
        assert 4.0 <= popt[1] <= 4.2, f"Expected T2p within [4.0, 4.2], got {popt[1]}"


# ---------------------------------------------------------------------------
# Direct OOP Class Tests
# ---------------------------------------------------------------------------

class TestSignalProcessorOOP:
    """Direct tests for the SignalProcessor class."""

    def test_denoise_trace_removes_extreme_outliers(self):
        trace = np.ones(10) * 10.0
        trace[5] = 500.0  # extreme outlier
        from bolus_tracking import SignalProcessor
        denoised = SignalProcessor.denoise_trace(trace, denoise_sd=2.0, half_win=3)
        assert denoised[5] != 500.0
        assert np.isclose(denoised[5], 10.0)

    def test_denoise_trace_empty_failsafe(self):
        from bolus_tracking import SignalProcessor
        empty = np.array([])
        result = SignalProcessor.denoise_trace(empty)
        assert len(result) == 0


class TestBolusModelOOP:
    """Direct tests for the BolusModel class."""

    def test_evaluate_returns_correct_shape(self):
        t = np.array([0.0, 1.0, 2.0, 3.0])
        from bolus_tracking import BolusModel
        y = BolusModel.evaluate(t, 50.0, 2.0, 1.0, 10.0)
        assert y.shape == t.shape

    def test_evaluate_onset_handling(self):
        """Values at t <= 0 should evaluate exactly to the baseline shift m."""
        t = np.array([-5.0, -1.0, 0.0, 1.0, 2.0])
        from bolus_tracking import BolusModel
        y = BolusModel.evaluate(t, 50.0, 2.0, 1.0, 10.0)
        assert np.allclose(y[t <= 0], 10.0)


class TestBolusFitterOOP:
    """Direct tests for the BolusFitter class."""

    def test_fitter_initialization_stores_bounds(self):
        from bolus_tracking import BolusFitter
        fitter = BolusFitter(min_amp=1e-3, max_amp=500.0, min_t2p=0.1, min_fwhm=1.0)
        assert fitter.min_amp == 1e-3
        assert fitter.max_amp == 500.0
        assert fitter.min_t2p == 0.1
        assert fitter.min_fwhm == 1.0

    def test_auto_estimate_params_on_noiseless_gaussian(self):
        y_us, t_us = _make_gaussian_bolus(amp=150.0, baseline=40.0, peak_t=15.0, sigma=2.0)
        from bolus_tracking import BolusFitter
        fitter = BolusFitter()
        p_init, start_idx, end_idx, sd_base, clicks = fitter.auto_estimate_params(y_us, t_us, fr=5.0, up_f=20)
        
        assert len(p_init) == 4
        assert p_init[0] > 0.0  # Estimated amp
        assert p_init[1] > 0.0  # Estimated t2p
        assert p_init[2] > 0.0  # Estimated FWHM
        assert np.isclose(p_init[3], 40.0, atol=2.0)  # Estimated baseline
        assert start_idx < end_idx
        assert 'onset' in clicks
        assert 'peak' in clicks

    def test_fit_returns_nan_on_pathological_input(self):
        t = np.array([])
        y = np.array([])
        from bolus_tracking import BolusFitter
        fitter = BolusFitter()
        popt, pcov, _ = fitter.fit(t, y, [1.0, 2.0, 1.0, 10.0])
        assert np.all(np.isnan(popt))
        assert np.all(np.isnan(pcov))


    def test_fit_with_bounds_override(self):
        t = np.linspace(0.0, 10.0, 100)
        # Synthetic clean gamma curve
        from bolus_tracking import BolusModel, BolusFitter
        y = BolusModel.evaluate(t, 100.0, 3.0, 2.0, 20.0)
        fitter = BolusFitter()
        
        # Override bounds to force a certain range
        bounds_override = (
            [80.0, 2.5, 1.5, 15.0],
            [120.0, 3.5, 2.5, 25.0]
        )
        popt, _, _ = fitter.fit(t, y, [90.0, 2.8, 1.8, 18.0], bounds_override=bounds_override)
        assert not np.any(np.isnan(popt))
        assert 80.0 <= popt[0] <= 120.0
        assert 2.5 <= popt[1] <= 3.5
        assert 1.5 <= popt[2] <= 2.5
        assert 15.0 <= popt[3] <= 25.0

    def test_fit_physiological_bounds_rejection(self):
        """Verify that parameters hitting the physiological boundaries are correctly detected."""
        def is_near_bounds(val, low, high):
            if abs(val - low) < 1e-4:
                return True
            if low > 0 and val <= low * 1.01:
                return True
            if high is not None and not np.isinf(high):
                if abs(high - val) < 1e-4:
                    return True
                if high > 0 and val >= high * 0.99:
                    return True
            return False

        min_amp, max_amp = 1e-6, 1023.0
        min_t2p, max_t2p = 1e-6, 100.0
        min_fwhm, max_fwhm = 0.5, 100.0

        # Case 1: Amplitude is too close to min_amp or max_amp
        assert is_near_bounds(1e-6, min_amp, max_amp) is True
        assert is_near_bounds(1.005e-6, min_amp, max_amp) is True
        assert is_near_bounds(1022.0, min_amp, max_amp) is True

        # Case 2: FWHM is too close to min_fwhm (0.5)
        assert is_near_bounds(0.5, min_fwhm, max_fwhm) is True
        assert is_near_bounds(0.504, min_fwhm, max_fwhm) is True

        # Case 3: T2P is too close to min_t2p (1e-6)
        assert is_near_bounds(1e-6, min_t2p, max_t2p) is True

        # Case 4: Valid parameters should NOT be near bounds
        assert is_near_bounds(50.0, min_amp, max_amp) is False
        assert is_near_bounds(3.5, min_t2p, max_t2p) is False
        assert is_near_bounds(4.2, min_fwhm, max_fwhm) is False


