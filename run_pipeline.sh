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
        
        python python/src/batch_process.py --folder "$TARGET_FOLDER" $DRIFT_FLAG \
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
