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
COPY bolus_tracking_cpp.cpp .
COPY CMakeLists.txt .

# Build executable
RUN mkdir build && cd build && cmake .. && make -j$(nproc)

# Set entrypoint
ENTRYPOINT ["/app/build/bolus_tracking_cpp"]
