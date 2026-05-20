#!/bin/bash
set -e

echo "=================================================="
echo "   Bolus Tracking C++ Automated Pipeline          "
echo "=================================================="

if [ "$#" -eq 0 ]; then
    echo "No target folder specified. Defaulting to current directory to process ALL subjects."
    TARGET_FOLDER="."
else
    TARGET_FOLDER=$1
fi

# 1. Compile C++ Code
echo "-> Step 1: Compiling C++ binary..."

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

# 2. Run Batch Processing Wrapper
echo "-> Step 2: Running C++ Parallel Batch Processing..."

if [ -d ".venv" ]; then
    source .venv/bin/activate
    python batch_process_cpp.py --folder "$TARGET_FOLDER"
else
    echo "WARNING: Python virtual environment (.venv) not found. Running with system python3..."
    python3 batch_process_cpp.py --folder "$TARGET_FOLDER"
fi

echo "=================================================="
echo "          C++ Pipeline Complete!                  "
echo "=================================================="
