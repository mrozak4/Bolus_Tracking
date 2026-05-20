import os
import glob
import argparse
import subprocess
import scipy.io as sio
import numpy as np
import time
import re

def parse_metadata(meta_path):
    """
    Parses the Fluoview .txt metadata file to extract frame rate.
    """
    with open(meta_path, 'r') as f:
        content = f.read()
    
    match = re.search(r'"T Dimension"\s+"(\d+),\s+([\d.]+)\s*-\s*([\d.]+)\s*\[s\]', content)
    if match:
        frames = int(match.group(1))
        t_start = float(match.group(2))
        t_end = float(match.group(3))
        fr = round(frames / (t_end - t_start), 2)
        return fr
    else:
        raise ValueError(f"Could not parse frame rate from {meta_path}")

def export_rois_to_txt(mask_path, txt_path):
    """
    Loads MATLAB .mat file containing maskObj and exports coordinates to a simple text format.
    """
    mat_data = sio.loadmat(mask_path, struct_as_record=False, squeeze_me=True)
    if 'maskObj' in mat_data:
        mask_objs = mat_data['maskObj']
    else:
        raise ValueError(f"No maskObj found in {mask_path}.")
        
    if not isinstance(mask_objs, np.ndarray):
        mask_objs = [mask_objs]
        
    valid_objs = []
    for i, obj in enumerate(mask_objs):
        if hasattr(obj, 'poli'):
            pos = obj.poli.Position
        elif hasattr(obj, 'Position'):
            pos = obj.Position
        else:
            continue
            
        if len(pos) < 3:
            continue
            
        valid_objs.append((i, pos))
        
    with open(txt_path, 'w') as f:
        f.write(f"{len(valid_objs)}\n")
        for i, pos in valid_objs:
            # Format: ROI_index Number_of_points
            f.write(f"{i} {len(pos)}\n")
            for pt in pos:
                f.write(f"{pt[0]} {pt[1]}\n")
                
    return len(valid_objs)

def find_triplets(folder):
    tifs = [f for f in glob.glob(os.path.join(folder, '**/*.tif'), recursive=True) if not os.path.basename(f).startswith('.')]
    mats = [f for f in glob.glob(os.path.join(folder, '**/adjusted_*.mat'), recursive=True) if not os.path.basename(f).startswith('.')]
    txts = [f for f in glob.glob(os.path.join(folder, '**/*.txt'), recursive=True) if not os.path.basename(f).startswith('.') and 'rois' not in os.path.basename(f).lower()]
    
    triplets = []
    for tif in tifs:
        tif_lower = tif.lower()
        name = os.path.basename(tif).lower()
        if 'mips' in tif_lower or 'results' in tif_lower or 'shift_info' in tif_lower or 'max_' in name:
            continue
            
        match = re.search(r'(bolus\d+_[a-z0-9]+)', name)
        if not match:
            continue
            
        identifier = match.group(1)
        matching_mat = [m for m in mats if identifier in os.path.basename(m).lower()]
        matching_txt = [t for t in txts if identifier in os.path.basename(t).lower()]
        
        if matching_mat and matching_txt:
            triplets.append((tif, matching_mat[0], matching_txt[0]))
            
    return list(set(triplets))

def main():
    parser = argparse.ArgumentParser(description="Parallelized C++ Bolus Tracking Batch Pipeline Wrapper")
    parser.add_argument("--folder", type=str, default=".", help="Root folder to search for subject directories")
    parser.add_argument("--docker", action="store_true", help="Run the C++ pipeline inside Docker instead of locally")
    
    args = parser.parse_args()
    
    # Locate all triplets (TIFF, MAT, TXT)
    search_dir = os.path.abspath(args.folder)
    triplets = find_triplets(search_dir)
    
    if not triplets:
        print(f"No matching datasets found in {search_dir}")
        return
        
    print(f"Found {len(triplets)} dataset(s) to process via parallel C++.")
    
    # Locate C++ binary path
    script_dir = os.path.dirname(os.path.abspath(__file__))
    cpp_binary = os.path.join(script_dir, "build", "bolus_tracking_cpp")
    
    if not args.docker and not os.path.exists(cpp_binary):
        print(f"C++ binary not found at {cpp_binary}. Please compile the C++ implementation using CMake or run_pipeline_cpp.sh first.")
        return
        
    total_start = time.time()
    
    for tiff_path, mask_path, meta_path in triplets:
        base_dir = os.path.dirname(tiff_path)
        base_name = os.path.splitext(os.path.basename(tiff_path))[0]
        
        print(f"\nProcessing {base_name}...")
        
        # 1. Parse metadata and export ROIs
        try:
            fr = parse_metadata(meta_path)
        except Exception as e:
            print(f"Error parsing metadata: {e}")
            continue
            
        rois_txt_path = os.path.join(base_dir, f"{base_name}_rois_cpp.txt")
        try:
            n_rois = export_rois_to_txt(mask_path, rois_txt_path)
            print(f"Exported {n_rois} ROIs to {os.path.basename(rois_txt_path)}")
        except Exception as e:
            print(f"Error exporting ROIs: {e}")
            continue
            
        if n_rois == 0:
            print(f"No valid ROIs found in {mask_path}. Skipping.")
            continue
            
        out_csv = os.path.join(base_dir, f"{base_name}_results_cpp.csv")
        up_f = 20
        
        # 2. Run parallel C++ program
        start_t = time.time()
        if args.docker:
            # Mount directory to /data and run inside container
            docker_cmd = [
                "docker", "run", "--rm",
                "-v", f"{base_dir}:/data",
                "bolus_tracking_cpp:latest",
                f"/data/{os.path.basename(tiff_path)}",
                f"/data/{os.path.basename(rois_txt_path)}",
                str(fr), str(up_f),
                f"/data/{os.path.basename(out_csv)}"
            ]
            print(f"Running via Docker: {' '.join(docker_cmd)}")
            res = subprocess.run(docker_cmd, capture_output=True, text=True)
        else:
            # Run locally
            local_cmd = [
                cpp_binary,
                tiff_path,
                rois_txt_path,
                str(fr), str(up_f),
                out_csv
            ]
            print(f"Running locally: {' '.join(local_cmd)}")
            res = subprocess.run(local_cmd, capture_output=True, text=True)
            
        end_t = time.time()
        
        if res.returncode == 0:
            print(f"Successfully processed {base_name} in {end_t - start_t:.3f} seconds.")
            # Print output from binary
            print(res.stdout.strip())
        else:
            print(f"Error running C++ binary for {base_name}:")
            print(res.stderr)
            
        # Clean up temporary rois file
        if os.path.exists(rois_txt_path):
            os.remove(rois_txt_path)
            
    print(f"\nTotal pipeline time: {time.time() - total_start:.2f} seconds.")

if __name__ == "__main__":
    main()
