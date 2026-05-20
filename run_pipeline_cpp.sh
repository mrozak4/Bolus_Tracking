#!/bin/bash
set -e

echo "=================================================="
echo "   Bolus Tracking C++ Automated Pipeline          "
echo "=================================================="

PLOT_FLAG=""
TARGET_FOLDER=""

for arg in "$@"; do
    if [ "$arg" = "--plot" ]; then
        PLOT_FLAG="--plot"
    else
        TARGET_FOLDER="$arg"
    fi
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

if [ "$DOCKER_RUNNING" = true ]; then
    echo "-> Running C++ Parallel Pipeline inside Docker..."
    echo "Building Docker container from Dockerfile.cpp..."
    docker build -t bolus_tracking_cpp -f Dockerfile.cpp .
    
    echo "Running C++ Batch Processing..."
    docker run --rm -v "$TARGET_ABS_FOLDER:/data" bolus_tracking_cpp --folder /data $PLOT_FLAG
else
    echo "-> Running C++ Parallel Pipeline locally..."
    
    # 1. Compile C++ Code
    echo "Step 1: Compiling C++ binary..."
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
    
    # 2. Run C++ Batch Processing directly
    echo "Step 2: Running C++ Parallel Batch Processing..."
    ./build/bolus_tracking_cpp --folder "$TARGET_ABS_FOLDER" $PLOT_FLAG
fi

echo "=================================================="
echo "          C++ Pipeline Complete!                  "
echo "=================================================="
