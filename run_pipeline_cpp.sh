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
PREFLIGHT_FLAG=""
PREPARE_FLAG=""
APPLY_FLAG=""
FORCE_FLAG=""
VERBOSE_FLAG=""

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
        --preflight|--validate)
            PREFLIGHT_FLAG="--preflight"
            shift
            ;;
        --prepare)
            PREPARE_FLAG="--prepare"
            shift
            ;;
        --apply)
            APPLY_FLAG="--apply"
            shift
            ;;
        --force)
            FORCE_FLAG="--force"
            shift
            ;;
        --verbose|--debug)
            VERBOSE_FLAG="--verbose"
            shift
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

# Check if Docker is installed and running
DOCKER_RUNNING=false
if command -v docker &> /dev/null; then
    if docker info &> /dev/null; then
        DOCKER_RUNNING=true
    fi
fi

# Build the common flags string
COMMON_FLAGS="$PLOT_FLAG $DRIFT_FLAG $VERBOSE_FLAG \
    $MIN_AMP_FLAG $MAX_AMP_FLAG $MIN_T2P_FLAG $MAX_T2P_FLAG $MIN_FWHM_FLAG $MAX_FWHM_FLAG \
    $QC_AMP_FAIL_FLAG $QC_T2P_MAX_FLAG $QC_T2P_FAIL_FLAG $QC_FWHM_MAX_FLAG $QC_FWHM_FAIL_FLAG \
    $QC_CNR_MIN_FLAG $QC_CNR_FAIL_FLAG"

# Determine the execution mode
if [ -n "$PREFLIGHT_FLAG" ]; then
    MODE_FLAGS="--preflight"
    echo "-> Mode: Pre-flight validation scan"
elif [ -n "$PREPARE_FLAG" ]; then
    MODE_FLAGS="--prepare $APPLY_FLAG $FORCE_FLAG"
    echo "-> Mode: File preparation (MAT → ROI conversion)"
else
    MODE_FLAGS=""
    echo "-> Mode: Full batch processing"
fi

if [ "$DOCKER_RUNNING" = true ]; then
    echo "-> Running inside Docker..."
    echo "Building Docker container from Dockerfile.cpp..."
    docker build -t bolus_tracking_cpp -f Dockerfile.cpp .
    
    if [ -n "$PREFLIGHT_FLAG" ] || [ -n "$PREPARE_FLAG" ]; then
        docker run --rm -v "$TARGET_ABS_FOLDER:/data" bolus_tracking_cpp \
            --folder /data $MODE_FLAGS $COMMON_FLAGS
    else
        echo "Running C++ Batch Processing..."
        docker run --rm -v "$TARGET_ABS_FOLDER:/data" bolus_tracking_cpp \
            --folder /data $COMMON_FLAGS
    fi
else
    echo "-> Running locally..."
    
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
    
    # B. Run
    if [ -n "$PREFLIGHT_FLAG" ] || [ -n "$PREPARE_FLAG" ]; then
        ./build/bolus_tracking_cpp --folder "$TARGET_ABS_FOLDER" $MODE_FLAGS $COMMON_FLAGS
    else
        echo "Sub-step B: Running C++ Parallel Batch Processing..."
        ./build/bolus_tracking_cpp --folder "$TARGET_ABS_FOLDER" $COMMON_FLAGS
    fi
fi

echo "=================================================="
echo "          C++ Pipeline Complete!                  "
echo "=================================================="
