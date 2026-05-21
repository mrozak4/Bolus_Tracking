# ApplyRegistrationToMask — README

**[English](README_ApplyRegistrationToMask.md) | [Français (Québec)](README_ApplyRegistrationToMask_FR.md)**

---

## Overview

ApplyRegistrationToMask.m is a MATLAB GUI tool that applies Visual Studio registration transforms to existing maskObj ROI files. When you have ROIs drawn on an unregistered bolus file and then register that bolus to the XYZ volumetric stack, the ROIs no longer match the registered image. This tool transforms the ROI vertex coordinates using the same affine transform that was applied to the image, so the ROIs follow the vessels.

## Why not just use GlobalShiftMask?

GlobalShiftMask applies a uniform XY pixel shift. The Visual Studio registration, however, can include rotation in addition to translation. For example, one bolus in your dataset has a 0.44 degree rotation component — at the image edges this produces ~2 pixel displacement on top of the translation. For vessels that are only 3-5 pixels wide, ignoring the rotation means ROIs at the edges of the FOV will be misaligned even after a "correct" XY shift. ApplyRegistrationToMask handles the full affine transform.

## Transform Format

The Visual Studio registration outputs .mat files (e.g., `bolus1_shift.mat`) containing:

- `AffineTransform_float_2_2`: a 6x1 vector [a00, a01, a10, a11, tx, ty] encoding a 2x2 rotation/scale matrix and a 2D translation vector
- `fixed`: a 2x1 vector specifying the center point of the transform

The full transform applied to each ROI vertex is:

```
output = A * (input - center) + center + translation
```

where A is the 2x2 matrix and translation is [tx, ty].

## Usage

1. Type `ApplyRegistrationToMask` in the MATLAB command window
2. Select the maskObj .mat file containing the ROIs to transform
3. Select the Visual Studio shift .mat file (e.g., `bolus1_shift.mat`) — this must be the transform for the same bolus the ROIs were drawn on
4. The tool displays the parsed transform (translation, rotation angle, center point)
5. Optionally load the registered TIFF or MIP to visually verify the transformed ROI positions
6. Click "Save Transformed Mask" to save the result

## Output

A .mat file containing a `maskObj` struct array with `.Position` fields. The default filename is `originalname_registered_shiftfilename.mat`. This file is compatible with BolusTrack's Import ROIs and with GlobalShiftMask for any additional fine-tuning.

## Where It Fits in the Workflow

This tool is used in Workflow B (fixing already-segmented data) between registering the bolus files and performing bolus tracking:

```
Register bolus TIFFs to XYZ (Visual Studio)
        |
ApplyRegistrationToMask.m — transform old maskObj to match registered TIFF
        |
(Optional) GlobalShiftMask.m — apply additional XY shift if needed
        |
BolusTrack.m — import transformed maskObj, fit, export
```

## Important Notes

- Use the shift file that corresponds to the bolus the ROIs were originally drawn on, not the paired bolus. The goal is to move the ROIs into the registered coordinate space of their own bolus image.
- For the paired bolus (where ROIs were NOT drawn), you would first apply this transform, then use GlobalShiftMask to apply the additional shift needed to align with the other bolus file.
- If the rotation component is negligible (< 0.01 degrees), the tool notes this. In that case GlobalShiftMask alone would have been sufficient.
- The tool handles maskObj files in all three formats: drawROI (.poli), GlobalShiftMask (.Position), and direct polygon objects.

## Dependencies

- MATLAB R2020a or later
- Image Processing Toolbox (for `poly2mask`, `medfilt2` if loading TIFF stacks)
