#!/bin/bash
set -e

echo "======================================"
echo "   Bolus Tracking Automated Pipeline  "
echo "======================================"

if [ "$#" -eq 0 ]; then
    echo "Usage: ./run_pipeline.sh <target_folder>"
    echo "Example: ./run_pipeline.sh sample-subject-2259"
    exit 1
fi

TARGET_FOLDER=$1

# 1. Convert Masks via MATLAB
echo "-> Step 1: Converting MATLAB masks..."
# Try to find MATLAB command depending on if we are on Mac or Linux
if command -v matlab &> /dev/null; then
    MATLAB_CMD="matlab"
elif [ -f "/Applications/MATLAB_R2025a.app/bin/matlab" ]; then
    MATLAB_CMD="/Applications/MATLAB_R2025a.app/bin/matlab"
else
    echo "WARNING: MATLAB not found in PATH. Assuming masks are already converted."
    MATLAB_CMD=""
fi

if [ -n "$MATLAB_CMD" ]; then
    $MATLAB_CMD -nodesktop -nosplash -r "run('scratch/convert_masks_for_python.m'); quit;"
fi

# 2. Run Python processing
echo "-> Step 2: Running Python Batch Processing..."

# Activate virtual environment if it exists (for local Mac testing)
if [ -d ".venv" ]; then
    source .venv/bin/activate
fi

# Run the pipeline on the specified folder
python batch_process.py --folder "$TARGET_FOLDER" --outdir ./

echo "======================================"
echo "          Pipeline Complete!          "
echo "======================================"
