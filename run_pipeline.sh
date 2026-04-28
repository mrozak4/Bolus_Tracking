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

# 2. Run Python processing via Docker
echo "-> Step 2: Running Python Batch Processing via Docker..."

# Build the docker image if it doesn't exist
if ! docker image inspect bolus_tracking:latest > /dev/null 2>&1; then
    echo "Building docker image 'bolus_tracking:latest'..."
    docker build -t bolus_tracking:latest .
fi

# Run the pipeline on the specified folder using Docker
# We mount the current directory to /data inside the container to access files
echo "Starting docker container..."
docker run --rm -v "$(pwd):/data" bolus_tracking:latest --folder "/data/$TARGET_FOLDER" --outdir "/data"

echo "======================================"
echo "          Pipeline Complete!          "
echo "======================================"
