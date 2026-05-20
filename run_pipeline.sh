#!/bin/bash
set -e

echo "======================================"
echo "   Bolus Tracking Automated Pipeline  "
echo "======================================"

TARGET_FOLDER=""
DRIFT_FLAG=""
MIN_AMP_FLAG=""
MAX_AMP_FLAG=""
MIN_T2P_FLAG=""
MAX_T2P_FLAG=""
MIN_FWHM_FLAG=""
MAX_FWHM_FLAG=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --drift|--drift-window)
            DRIFT_FLAG="--drift $2"
            shift 2
            ;;
        --min-amp)
            MIN_AMP_FLAG="--min-amp $2"
            shift 2
            ;;
        --max-amp)
            MAX_AMP_FLAG="--max-amp $2"
            shift 2
            ;;
        --min-t2p)
            MIN_T2P_FLAG="--min-t2p $2"
            shift 2
            ;;
        --max-t2p)
            MAX_T2P_FLAG="--max-t2p $2"
            shift 2
            ;;
        --min-fwhm)
            MIN_FWHM_FLAG="--min-fwhm $2"
            shift 2
            ;;
        --max-fwhm)
            MAX_FWHM_FLAG="--max-fwhm $2"
            shift 2
            ;;
        *)
            TARGET_FOLDER="$1"
            shift
            ;;
    esac
done

if [ -z "$TARGET_FOLDER" ]; then
    echo "No target folder specified. Defaulting to current directory to process ALL subjects."
    TARGET_FOLDER="."
fi

# 1. Convert Masks via MATLAB
echo "-> Step 1: Converting MATLAB masks..."
# Try to find MATLAB command depending on if we are on Mac or Linux
if command -v matlab &> /dev/null; then
    MATLAB_CMD="matlab"
elif command -v matlab.exe &> /dev/null; then
    MATLAB_CMD="matlab.exe"
else
    MATLAB_CMD=""
    # Check standard macOS installation path
    if [ -d "/Applications" ]; then
        MATLAB_APP=$(ls -rd /Applications/MATLAB_*.app 2>/dev/null | head -n 1)
        if [ -n "$MATLAB_APP" ] && [ -f "$MATLAB_APP/bin/matlab" ]; then
            MATLAB_CMD="$MATLAB_APP/bin/matlab"
        fi
    fi
    # Check standard Linux installation path
    if [ -z "$MATLAB_CMD" ] && [ -d "/usr/local/MATLAB" ]; then
        MATLAB_APP=$(ls -rd /usr/local/MATLAB/R* 2>/dev/null | head -n 1)
        if [ -n "$MATLAB_APP" ] && [ -f "$MATLAB_APP/bin/matlab" ]; then
            MATLAB_CMD="$MATLAB_APP/bin/matlab"
        fi
    fi
    # Check standard Windows installation paths (Git Bash & WSL)
    if [ -z "$MATLAB_CMD" ]; then
        WINDOWS_MATLAB_DIR=""
        if [ -d "/c/Program Files/MATLAB" ]; then
            WINDOWS_MATLAB_DIR="/c/Program Files/MATLAB"
        elif [ -d "/mnt/c/Program Files/MATLAB" ]; then
            WINDOWS_MATLAB_DIR="/mnt/c/Program Files/MATLAB"
        fi
        
        if [ -n "$WINDOWS_MATLAB_DIR" ]; then
            MATLAB_APP=$(ls -rd "$WINDOWS_MATLAB_DIR"/R* 2>/dev/null | head -n 1)
            if [ -n "$MATLAB_APP" ]; then
                if [ -f "$MATLAB_APP/bin/matlab.exe" ]; then
                    MATLAB_CMD="$MATLAB_APP/bin/matlab.exe"
                elif [ -f "$MATLAB_APP/bin/matlab" ]; then
                    MATLAB_CMD="$MATLAB_APP/bin/matlab"
                fi
            fi
        fi
    fi
fi

if [ -z "$MATLAB_CMD" ]; then
    echo "WARNING: MATLAB not found in PATH or standard installation directories. Assuming masks are already converted."
fi

if [ -n "$MATLAB_CMD" ]; then
    if [[ "$MATLAB_CMD" == *.exe ]]; then
        # On Windows, run in batch mode to output directly to the console and terminate properly
        "$MATLAB_CMD" -batch "run('scratch/convert_masks_for_python.m');"
    else
        $MATLAB_CMD -nodesktop -nosplash -r "run('scratch/convert_masks_for_python.m'); quit;"
    fi
fi

# 2. Run Python processing
echo "-> Step 2: Running Python Batch Processing..."

if docker info > /dev/null 2>&1; then
    echo "Docker is running. Building docker image 'bolus_tracking:latest'..."
    docker build -t bolus_tracking:latest .
    
    echo "Starting docker container..."
    docker run --rm -v "$(pwd):/data" bolus_tracking:latest --folder "/data/$TARGET_FOLDER" $DRIFT_FLAG \
        $MIN_AMP_FLAG $MAX_AMP_FLAG $MIN_T2P_FLAG $MAX_T2P_FLAG $MIN_FWHM_FLAG $MAX_FWHM_FLAG
else
    echo "WARNING: Docker is not running or not accessible."
    echo "Falling back to local Python virtual environment (.venv)..."
    
    if [ -d ".venv" ]; then
        source .venv/bin/activate
        # Quietly ensure latest requirements (like matplotlib) are installed
        pip install -q -r requirements.txt
        
        python batch_process.py --folder "$TARGET_FOLDER" $DRIFT_FLAG \
            $MIN_AMP_FLAG $MAX_AMP_FLAG $MIN_T2P_FLAG $MAX_T2P_FLAG $MIN_FWHM_FLAG $MAX_FWHM_FLAG
    else
        echo "ERROR: Docker is not running and .venv was not found."
        echo "Please open Docker Desktop and try again!"
        exit 1
    fi
fi

echo "======================================"
echo "          Pipeline Complete!          "
echo "======================================"
