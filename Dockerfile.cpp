FROM ubuntu:22.04

# Avoid interactive prompt during apt-get
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libtiff-dev \
    libeigen3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy files
COPY CMakeLists.txt .
COPY bolus_tracking_cpp.hpp .
COPY signal_processing.cpp .
COPY bolus_fitting.cpp .
COPY roi_rasterizer.cpp .
COPY bolus_visualizer.cpp .
COPY dataset_processor.cpp .
COPY batch_processor.cpp .
COPY main.cpp .
COPY test_bolus_tracking_cpp.cpp .

# Build executable
RUN mkdir build && cd build && cmake -DBUILD_GUI=OFF .. && make -j$(nproc) && ./test_bolus_tracking_cpp

# Set entrypoint
ENTRYPOINT ["/app/build/bolus_tracking_cpp"]
