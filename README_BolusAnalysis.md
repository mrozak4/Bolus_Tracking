# Bolus Analysis Toolkit — README

**[English](README_BolusAnalysis.md) | [Français (Québec)](README_BolusAnalysis_FR.md)**

---

## Overview

This toolkit performs fluorescence bolus tracking analysis on two-photon microscopy time-series data. It segments vascular regions of interest (ROIs) from maximum intensity projections (MIPs), extracts fluorescence time-courses from each ROI, and fits gamma functions to the bolus passage curve to derive hemodynamic parameters.

**Original BolusTrack.m and drawROI.m:** Paolo Bazzigaluppi (January 2019)  
**Modifications:** Adrienne Dorr (April 2026)

---

## Files

| File | Purpose |
|------|---------|
| `drawROI.m` | Draw and save polygon ROIs on a MIP image |
| `ApplyRegistrationToMask.m` | Apply Visual Studio affine transforms (translation + rotation) to existing maskObj files |
| `GlobalShiftMask.m` | GUI tool to apply a uniform XY pixel shift to all ROIs and save a new mask file |
| `BolusTrack_InteractiveEdit.m` | Bolus fitting GUI with pop-out ROI editor, metadata loading, denoising, and auto-save |
| `BolusTrack.m` (original) | Paolo's original bolus fitting GUI (kept as backup) |

---

## Workflow A) Fresh data

1. Register both bolus TIFFs to XYZ stack (Visual Studio, Linux). Produces shifted TIFFs + transform files.
2. Generate MIP from one registered bolus (ImageJ). Alternate which bolus across subjects.
3. Draw ROIs on MIP (drawROI.m). Save maskObj.
4. Fit the bolus ROIs were drawn on (BolusTrack.m) — load registered TIFF, load metadata, import original maskObj. ROIs should match. Show ROIs tc, fit, export.
5. Shift ROIs for the paired bolus (GlobalShiftMask.m) — load paired registered TIFF, import original maskObj, apply XY shift in Pop-out View, verify, save shifted maskObj.
6. Fit the paired bolus (BolusTrack.m) — import shifted maskObj from step 5. If individual ROIs still need tweaking (Z-drift, swelling), use Pop-out View before Show ROIs tc.

## Workflow B) Fixing old maskObj files

1. Register both bolus TIFFs to XYZ stack (Visual Studio, Linux).
2. Transform old maskObj to match registered image (ApplyRegistrationToMask.m) — select the maskObj, select the corresponding shift .mat file, verify, save. Handles full affine transform including rotation.
3. If transformed ROIs still don't perfectly align with the paired bolus, apply additional XY correction (GlobalShiftMask.m).
4. Fit (BolusTrack.m) — import transformed/shifted maskObj, Pop-out View for per-vessel tweaks if needed, Show ROIs tc, fit, export.

## Workflow C) Testing without Visual Studio registration

1. Work with original unregistered cropped TIFFs.
2. Draw ROIs on MIP of one bolus (drawROI.m).
3. Fit that bolus (BolusTrack.m) with original maskObj.
4. Shift ROIs for paired bolus (GlobalShiftMask.m).
5. Fit paired bolus (BolusTrack.m). Pop-out View for fine-tuning if needed.

---

## File-by-File Documentation

### drawROI.m

**Syntax:**
```matlab
maskObj = drawROI(img, vnum, stype)
```

| Parameter | Description |
|-----------|-------------|
| `img` | MIP image matrix |
| `vnum` | Number of ROIs (typically 50-70) |
| `stype` | `0` = zoom once, draw all; `1` = re-zoom before each ROI |

**Saving:**
```matlab
save('MAX_4755_bolus3_maskObj.mat', 'maskObj')
```

**ROI sizing:** Segment the full visible vessel lumen but stop 1-2 pixels short of the apparent edge. Larger ROIs give better SNR.

---

### ApplyRegistrationToMask.m

Applies the full affine transform from Visual Studio registration (translation + rotation) to an existing maskObj.

**Syntax:**
```matlab
ApplyRegistrationToMask
```

1. Select maskObj, 2. Select Visual Studio shift .mat file, 3. Verify on registered TIFF, 4. Save.

See README_ApplyRegistrationToMask.md for full details.

---

### GlobalShiftMask.m

Uniform XY pixel shift for all ROIs. Use when shifting ROIs between bolus pairs in the same registered coordinate space.

**Syntax:**
```matlab
GlobalShiftMask
```

| Button | Function |
|--------|----------|
| Load Data | Load bolus TIFF, display MIP |
| Import ROIs | Overlay maskObj on MIP |
| Pop-out View | Enter XY shift, verify |
| Save Mask | Save shifted maskObj |

---

### BolusTrack_InteractiveEdit.m

Full bolus tracking GUI. Rename to `BolusTrack.m` for daily use.

**Syntax:**
```matlab
BolusTrack
```

**Left panel buttons:**

| Button | Function |
|--------|----------|
| Load Data | Load cropped bolus TIFF |
| Load Metadata | Auto-populate frame rate from Fluoview .txt file |
| Frame Rate | Manual entry/override |
| Import ROIs | Load maskObj |
| Pop-out View | Full-size interactive ROI editor |
| Save ROIs | Save adjusted ROI positions as a new maskObj before fitting |
| Show ROIs tc | Extract raw time-courses, display first trace |
| Denoise (SD) | Set outlier threshold (default 2.0) |
| Apply Denoise | Apply denoising to the current trace |
| Toggle Raw | Switch between raw and denoised display |
| Resume Session | Recover progress after a crash from autosave_progress.mat |

**Load Metadata:** Use the ORIGINAL unclipped metadata .txt file from Fluoview, not a version from a cropped TIFF. The frame rate is an acquisition parameter that does not change with time cropping.

**Save ROIs:** Saves the current ROI positions (after any adjustments via Pop-out View or manual dragging) as a new maskObj .mat file. Use this AFTER finalizing ROI positions and BEFORE clicking Show ROIs tc. This gives you a saved copy of the adjusted ROIs independent of the fitting process — if your computer crashes during fitting, you won't need to redo the ROI adjustments.

**Auto-save:** Progress is automatically saved every 5 fitted traces to `autosave_progress.mat` in the data directory. After a successful Export, the auto-save file is cleaned up automatically.

**Resume Session — recovering after a crash:**

1. Relaunch BolusTrack
2. Load Data (same bolus TIFF you were working on)
3. Load Metadata (or set frame rate manually)
4. Import ROIs (same maskObj — use the Save ROIs output if you saved one)
5. Show ROIs tc (regenerates time-courses from the TIFF)
6. Click Resume Session — select autosave_progress.mat — completed fits are restored and the display jumps to the next unfitted trace

Note: `gammaFun.m` must be on the MATLAB path for fitting to work. If MATLAB restarted after a crash, it may have lost the path. Check with `which gammaFun` and add the folder if needed: `addpath('/path/to/folder')`. To make this permanent: Home > Set Path > Add Folder > Save.

**Denoising — recommended approach:**

Each new trace always defaults to the RAW view. This is deliberate — always attempt to fit the raw trace first. The gamma fitter uses Cauchy robust weighting which already down-weights outlier points. Only apply denoising when the raw trace is too noisy to identify the initial parameters or when the fitter fails to converge. The recommended procedure:

1. Inspect the raw trace. If the bolus onset, peak, and return to baseline are identifiable, fit it as-is.
2. If too noisy, click Apply Denoise at the default 2.0 SD. This denoises only the current trace — it does not affect other traces.
3. If 2.0 SD is not enough, lower the threshold progressively (try 1.5). Avoid going below 1.5 as this risks clipping the bolus ramp on traces with small amplitudes.
4. Use Toggle Raw to compare raw vs denoised before committing to a fit.
5. Whichever version is displayed when you click Fit Gamma function is what gets fitted.

The tool records whether each trace was fitted on raw or denoised data (and at what SD) in the `fitOut(n).fittedOn` field. This is important for reporting: in your methods section, note which traces required denoising and at what threshold.

**SD threshold reference:**

| Value | Effect |
|-------|--------|
| 1.5 | Aggressive — may clip bolus ramp on clean/small-amplitude traces |
| 2.0 | Default — good balance for moderately noisy data |
| 2.5-3.0 | Permissive — only removes largest spikes |
| >10 | Effectively no denoising |

**When NOT to denoise:**

- The raw trace is clean enough to fit — denoise adds no value and introduces a processing step that must be reported and justified.
- The bolus amplitude is small relative to baseline noise (common in aged animals). The algorithm may clip the onset/peak because the rising signal looks like an "outlier" relative to the preceding flat baseline.
- The denoising + robust fitting combination applies two sequential outlier-removal steps, which could compound and over-smooth. If in doubt, rely on the fitter's built-in robustness alone.

**Fitting procedure:**

1. Click trace peak → Amplitude → Enter
2. Click time of peak → Time to Peak → Enter
3. Click bolus onset → Fit Start → Enter
4. Click return to baseline → Fit End → Enter
5. FWHM → Enter (auto-calculated)
6. Baseline shift → Enter (auto-calculated)
7. Fit Gamma function
8. If good → Save values/next trace
9. If bad → adjust parameters, re-fit

**Pop-out View:** Full-size zoomable window for ROI editing. Drag entire ROIs or individual vertices. Click Apply Edits when done, then Save ROIs, then Show ROIs tc.

**Output files:**

| File | Contents |
|------|----------|
| `.csv` | Gamma fit parameters for all ROIs |
| `.mat` | fitOut struct, mask, maskNum |
| `_MaskObj.mat` | Updated maskObj |

**CSV columns:**

`subj_num, ves_num, exp, InitAmp, InitT2p, InitFWHM, InitM, F_Amp, F_T2p, F_FWHM, F_M, AUC, AUCn, TTlb, TTm, TThb, OnTSc, ROI size`

---

## Vessel Type Classification

| Code | Type | Criteria |
|------|------|----------|
| A | Arteriole | Penetrating; <5 branches; deep branching (>400 um); right-angle branches |
| V | Venule | Penetrating; >5 branches; superficial branching; obtuse angles; thicker at surface |
| C | Capillary | Non-penetrating surface vessel |
| U | Unknown | Penetrating, type indeterminate |

---

## Troubleshooting

**ROIs don't match paired bolus:** GlobalShiftMask for bulk correction, Pop-out View for per-vessel tweaks.

**Old ROIs don't match registered images:** ApplyRegistrationToMask for full affine transform, then GlobalShiftMask if needed.

**Noisy traces:** Try raw first. Apply Denoise at 2.0 SD if needed, then progressively lower. Toggle Raw to compare.

**Gamma fit won't converge:** Adjust initial parameters. Move Time to Peak slightly earlier. Extend Fit Start. Regenerate FWHM and Baseline shift.

**"The model function 'gammaFun' was not found":** gammaFun.m is not on your MATLAB path. Run `which gammaFun` to check. Add the containing folder with `addpath('/path/to/folder')`. Make it permanent via Home > Set Path > Save.

**Computer crashed mid-session:** Load Data > Load Metadata > Import ROIs > Show ROIs tc > Resume Session > select autosave_progress.mat. All completed fits are restored.

**"Reference to non-existent field 'poli'":** maskObj is from GlobalShiftMask or ApplyRegistrationToMask (.Position format). Current BolusTrack handles both.

**Changes not taking effect:** Close GUI, relaunch with `BolusTrack` in command window.

**Which file is MATLAB using?** `which BolusTrack`

---

## Inter-Rater Variability Testing

To assess inter-rater reliability, a second operator can independently fit the same data. Provide them with:

- Time-cropped bolus TIFF files (registered or unregistered, matching your analysis)
- The maskObj .mat files (original from drawROI and/or shifted from GlobalShiftMask)
- The metadata .txt files for frame rate
- All .m files: BolusTrack_InteractiveEdit.m (renamed to BolusTrack.m), gammaFun.m, and optionally GlobalShiftMask.m
- This README and the Bolus_Analysis_Processing_2026 document

The second rater should use the same maskObj and the same bolus TIFF — the only difference should be their selection of initial fitting parameters (Amplitude, Time to Peak, Fit Start, Fit End) and their decision of whether to denoise each trace. Compare the resulting fitted parameters (F_Amp, F_T2p, F_FWHM, AUC, TTm, OnTSc) between raters.

---

## Dependencies

- MATLAB R2020a or later
- Image Processing Toolbox (`images.roi.Polygon`, `poly2mask`, `medfilt2`)
- Statistics and Machine Learning Toolbox (`nlinfit`, `nlparci`, `statset`)
- `gammaFun.m` (must be on MATLAB path — check with `which gammaFun`)

---

## File Naming Conventions

| Type | Convention | Example |
|------|-----------|---------|
| Cropped TIFF | `subjectID_bolus#_condition_start_end.tif` | `4755_bolus3_baseline_110_360.tif` |
| MIP | `MAX_subjectID_bolus#_condition.tif` | `MAX_4755_bolus3_baseline.tif` |
| ROI mask (drawROI) | `subjectID_bolus#_condition_MAX_maskObj.mat` | `4755_bolus3_baseline_MAX_maskObj.mat` |
| Registered mask | `*_registered_bolus#_shift.mat` | `...maskObj_registered_bolus1_shift.mat` |
| Shifted mask | `*_shifted_X#_Y#.mat` | `...maskObj_shifted_X3_Y-1.mat` |
| Adjusted mask | `adjusted_maskObj.mat` | (from Save ROIs in BolusTrack) |
| Metadata | `bolus#_condition.txt` | `bolus2_co2.txt` |
| Auto-save | `autosave_progress.mat` | (in data directory) |
| Fit results | `subjectID_bolus#_condition.csv / .mat` | `4755_bolus3_baseline.csv` |
