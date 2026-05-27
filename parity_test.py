#!/usr/bin/env python3
"""Parity test: compare GUI server batch_fit vs CLI pipeline output."""

import subprocess
import json
import csv
import math
import sys
import os
import time

BASE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.join(BASE, "build", "bolus_server")
TIFF = os.path.join(BASE, "sample-subject-2259", "bolus1_baseline.tif")
ROIS = os.path.join(BASE, "sample-subject-2259", "old_masks_drawROI", "2259_bolus1_baseline_maskObj_rois.txt")
META = os.path.join(BASE, "sample-subject-2259", "bolus1_baseline.txt")
OUT_GUI = os.path.join(BASE, "sample-subject-2259", "bolus1_baseline_results_gui_TEST.csv")
OUT_CLI = os.path.join(BASE, "sample-subject-2259", "bolus1_baseline_results_cpp_TEST.csv")

_msg_id = 0
def server_cmd(proc, action, params=None):
    global _msg_id
    _msg_id += 1
    msg = json.dumps({"id": _msg_id, "action": action, "params": params or {}})
    proc.stdin.write(msg + "\n")
    proc.stdin.flush()
    line = proc.stdout.readline().strip()
    return json.loads(line)

print("=" * 60)
print("  PARITY TEST: GUI Server batch_fit vs CLI Pipeline")
print("=" * 60)
print()

# Step 0: Run CLI pipeline to generate fresh reference output
CLI_BIN = os.path.join(BASE, "build", "bolus_tracking_cpp")
print("0. Running CLI pipeline...")
cli_result = subprocess.run(
    [CLI_BIN, TIFF, ROIS, "5.08", "10", OUT_CLI],
    capture_output=True, text=True, cwd=BASE, timeout=120
)
if cli_result.returncode != 0:
    print(f"   CLI FAILED: {cli_result.stderr[:200]}")
    sys.exit(1)
print(f"   CLI pipeline complete")
print()

# Start server
print("Starting bolus_server...")
proc = subprocess.Popen(
    [SERVER],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    text=True, bufsize=1,
    cwd=BASE
)

# Read and discard the greeting/handshake line
greeting = proc.stdout.readline().strip()
print(f"   Server greeting: {json.loads(greeting).get('data',{}).get('status','?')}")

try:
    # 1. Load TIFF
    print("1. Loading TIFF...")
    resp = server_cmd(proc, "load_tiff", {"path": TIFF})
    if not resp.get('ok'):
        print(f"   ERROR: {json.dumps(resp)}")
    else:
        print(f"   ok={resp.get('ok')}, frames={resp.get('data',{}).get('n_frames','?')}, size={resp.get('data',{}).get('width','?')}x{resp.get('data',{}).get('height','?')}")

    # 2. Load ROIs
    print("2. Loading ROIs...")
    resp = server_cmd(proc, "load_rois", {"path": ROIS})
    if not resp.get('ok'):
        print(f"   ERROR: {json.dumps(resp)}")
    else:
        print(f"   ok={resp.get('ok')}, count={resp.get('data',{}).get('count','?')}")

    # 3. Parse framerate
    print("3. Parsing framerate...")
    resp = server_cmd(proc, "parse_framerate", {"path": META})
    fr = resp.get("data", {}).get("framerate", 5.08)
    print(f"   framerate={fr}")

    # 4. Compute traces
    print("4. Computing traces...")
    resp = server_cmd(proc, "compute_traces", {"framerate": fr, "denoise_strength": 1.0})
    roi_ids = resp.get("data", {}).get("roi_ids", [])
    print(f"   ok={resp.get('ok')}, n_rois={len(roi_ids)}")

    # 5. Run batch_fit
    print("5. Running batch_fit (full pipeline)...")
    resp = server_cmd(proc, "batch_fit", {})
    data = resp.get("data", {})
    print(f"   ok={resp.get('ok')}, pass={data.get('pass','?')}, warn={data.get('warn','?')}, fail={data.get('fail','?')}, stall={data.get('stall','?')}")

    # 6. Save CSV
    print("6. Saving GUI results to CSV...")
    resp = server_cmd(proc, "save_csv", {"path": OUT_GUI})
    print(f"   ok={resp.get('ok')}")

finally:
    proc.stdin.close()
    proc.terminate()
    proc.wait(timeout=5)

print()
print("=" * 60)
print("  COMPARING OUTPUTS")
print("=" * 60)
print()

def read_csv_records(path):
    records = {}
    with open(path) as f:
        reader = csv.DictReader(f)
        cols = reader.fieldnames
        for row in reader:
            roi = row.get('ROI', row.get('roi_id', ''))
            records[str(roi)] = row
    return records, cols

cli, cli_cols = read_csv_records(OUT_CLI)
gui, gui_cols = read_csv_records(OUT_GUI)

print(f"CLI: {len(cli)} ROIs, {len(cli_cols)} columns")
print(f"GUI: {len(gui)} ROIs, {len(gui_cols)} columns")
print()
print(f"CLI columns: {cli_cols}")
print(f"GUI columns: {gui_cols}")
print()

# Column name mapping (CLI uses these names, GUI may use different ones)
field_map = {
    'ROI': 'ROI',
    'Fitted_Amp': 'Fitted_Amp',
    'Fitted_T2p': 'Fitted_T2p',
    'Fitted_FWHM': 'Fitted_FWHM',
    'Fitted_M': 'Fitted_M',
    'Fitted_SNR': 'Fitted_SNR',
    'Fitted_CNR': 'Fitted_CNR',
    'AUC': 'AUC',
    'AUCn': 'AUCn',
    'OnT': 'OnT',
    'OnTSc': 'OnTSc',
    'QC_Flag': 'QC_Flag',
    'VesType': 'VesType',
    'StallFlag': 'StallFlag',
}

numeric_fields = ['F_Amp', 'F_T2p', 'F_FWHM', 'F_M',
                  'F_SNR', 'F_CNR', 'AUC', 'AUCn', 'OnT', 'OnTSc']
string_fields = ['QC_Flag', 'VesType']

mismatches = []
matches = 0
perfect = 0

cli_ids = sorted(cli.keys(), key=lambda x: int(x) if x.isdigit() else 0)

for roi_id in cli_ids:
    cli_rec = cli[roi_id]
    gui_rec = gui.get(roi_id)
    if gui_rec is None:
        mismatches.append(f"ROI {roi_id}: MISSING in GUI output")
        continue
    
    matches += 1
    roi_ok = True
    
    for field in numeric_fields:
        cv_str = cli_rec.get(field, '')
        gv_str = gui_rec.get(field, '')
        if not cv_str or not gv_str:
            continue
        try:
            cv = float(cv_str)
            gv = float(gv_str)
        except ValueError:
            if cv_str.lower() == 'nan' and gv_str.lower() == 'nan':
                continue
            mismatches.append(f"ROI {roi_id} {field}: CLI='{cv_str}' GUI='{gv_str}' (parse error)")
            roi_ok = False
            continue
        
        if math.isnan(cv) and math.isnan(gv):
            continue
        if math.isnan(cv) != math.isnan(gv):
            mismatches.append(f"ROI {roi_id} {field}: CLI={cv_str} GUI={gv_str} (NaN mismatch)")
            roi_ok = False
            continue
        
        if abs(cv) > 1e-10:
            rel_diff = abs(cv - gv) / abs(cv) * 100
        else:
            rel_diff = abs(cv - gv) * 100
        
        if rel_diff > 1.0:  # >1% difference
            mismatches.append(f"ROI {roi_id} {field}: CLI={cv:.6f} GUI={gv:.6f} diff={rel_diff:.2f}%")
            roi_ok = False
    
    for field in string_fields:
        cv = cli_rec.get(field, '').strip()
        gv = gui_rec.get(field, '').strip()
        if cv and gv and cv != gv:
            mismatches.append(f"ROI {roi_id} {field}: CLI='{cv}' GUI='{gv}'")
            roi_ok = False
    
    if roi_ok:
        perfect += 1

print(f"Matched {matches}/{len(cli_ids)} ROIs")
print(f"Perfect match: {perfect}/{matches}")
print()

if mismatches:
    print(f"=== {len(mismatches)} DISCREPANCIES FOUND ===")
    for m in mismatches:
        print(f"  {m}")
else:
    print("=== PERFECT PARITY — No discrepancies found! ===")

# Summary table: show a few ROIs side by side
print()
print("=" * 60)
print("  SAMPLE COMPARISON (first 5 ROIs)")
print("=" * 60)
fmt = "{:<6} {:<10} {:<10} {:<10} {:<10} {:<8}"
print(fmt.format("ROI", "CLI_Amp", "GUI_Amp", "CLI_T2p", "GUI_T2p", "CLI_QC/GUI_QC"))
print("-" * 60)
for roi_id in cli_ids[:5]:
    cr = cli[roi_id]
    gr = gui.get(roi_id, {})
    print(fmt.format(
        roi_id,
        cr.get('F_Amp','?')[:9],
        gr.get('F_Amp','?')[:9],
        cr.get('F_T2p','?')[:9],
        gr.get('F_T2p','?')[:9],
        f"{cr.get('QC_Flag','?')}/{gr.get('QC_Flag','?')}"
    ))
