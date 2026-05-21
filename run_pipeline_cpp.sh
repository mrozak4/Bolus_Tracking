#!/bin/bash
set -e

echo "=================================================="
echo "   Bolus Tracking C++ Automated Pipeline          "
echo "=================================================="

PLOT_FLAG=""
TARGET_FOLDER=""
DRIFT_FLAG=""
MIN_AMP_FLAG=""
MAX_AMP_FLAG=""
MIN_T2P_FLAG=""
MAX_T2P_FLAG=""
MIN_FWHM_FLAG=""
MAX_FWHM_FLAG=""
QC_AMP_FAIL_FLAG=""
QC_T2P_MAX_FLAG=""
QC_T2P_FAIL_FLAG=""
QC_FWHM_MAX_FLAG=""
QC_FWHM_FAIL_FLAG=""
QC_CNR_MIN_FLAG=""
QC_CNR_FAIL_FLAG=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --plot)
            PLOT_FLAG="--plot"
            shift
            ;;
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
        --qc-amp-fail)
            QC_AMP_FAIL_FLAG="--qc-amp-fail $2"
            shift 2
            ;;
        --qc-t2p-max)
            QC_T2P_MAX_FLAG="--qc-t2p-max $2"
            shift 2
            ;;
        --qc-t2p-fail)
            QC_T2P_FAIL_FLAG="--qc-t2p-fail $2"
            shift 2
            ;;
        --qc-fwhm-max)
            QC_FWHM_MAX_FLAG="--qc-fwhm-max $2"
            shift 2
            ;;
        --qc-fwhm-fail)
            QC_FWHM_FAIL_FLAG="--qc-fwhm-fail $2"
            shift 2
            ;;
        --qc-cnr-min)
            QC_CNR_MIN_FLAG="--qc-cnr-min $2"
            shift 2
            ;;
        --qc-cnr-fail)
            QC_CNR_FAIL_FLAG="--qc-cnr-fail $2"
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

TARGET_ABS_FOLDER=$(cd "$TARGET_FOLDER" && pwd)

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
    echo "Running MATLAB mask conversion..."
    if [[ "$MATLAB_CMD" == *.exe ]]; then
        # On Windows, run in batch mode to output directly to the console and terminate properly
        "$MATLAB_CMD" -batch "run('scratch/convert_masks_for_python.m');"
    else
        $MATLAB_CMD -nodesktop -nosplash -r "run('scratch/convert_masks_for_python.m'); quit;"
    fi
fi

# Check if Docker is installed and running
DOCKER_RUNNING=false
if command -v docker &> /dev/null; then
    if docker info &> /dev/null; then
        DOCKER_RUNNING=true
    fi
fi

if [ "$DOCKER_RUNNING" = true ]; then
    echo "-> Step 2: Running C++ Parallel Pipeline inside Docker..."
    echo "Building Docker container from Dockerfile.cpp..."
    docker build -t bolus_tracking_cpp -f Dockerfile.cpp .
    
    echo "Running C++ Batch Processing..."
    docker run --rm -v "$TARGET_ABS_FOLDER:/data" bolus_tracking_cpp --folder /data $PLOT_FLAG $DRIFT_FLAG \
        $MIN_AMP_FLAG $MAX_AMP_FLAG $MIN_T2P_FLAG $MAX_T2P_FLAG $MIN_FWHM_FLAG $MAX_FWHM_FLAG \
        $QC_AMP_FAIL_FLAG $QC_T2P_MAX_FLAG $QC_T2P_FAIL_FLAG $QC_FWHM_MAX_FLAG $QC_FWHM_FAIL_FLAG \
        $QC_CNR_MIN_FLAG $QC_CNR_FAIL_FLAG
else
    echo "-> Step 2: Running C++ Parallel Pipeline locally..."
    
    # A. Compile C++ Code
    echo "Sub-step A: Compiling C++ binary..."
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macOS detected. Configuring SDK include paths for compilation..."
        SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null || echo "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
        export CXXFLAGS="-isysroot $SDK_PATH -I$SDK_PATH/usr/include/c++/v1"
    fi

    mkdir -p build
    cd build
    cmake ..
    make -j4
    cd ..
    
    # B. Run C++ Batch Processing directly
    echo "Sub-step B: Running C++ Parallel Batch Processing..."
    ./build/bolus_tracking_cpp --folder "$TARGET_ABS_FOLDER" $PLOT_FLAG $DRIFT_FLAG \
        $MIN_AMP_FLAG $MAX_AMP_FLAG $MIN_T2P_FLAG $MAX_T2P_FLAG $MIN_FWHM_FLAG $MAX_FWHM_FLAG \
        $QC_AMP_FAIL_FLAG $QC_T2P_MAX_FLAG $QC_T2P_FAIL_FLAG $QC_FWHM_MAX_FLAG $QC_FWHM_FAIL_FLAG \
        $QC_CNR_MIN_FLAG $QC_CNR_FAIL_FLAG
fi

echo "=================================================="
echo "          C++ Pipeline Complete!                  "
echo "=================================================="
