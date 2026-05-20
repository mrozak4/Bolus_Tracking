cmake_minimum_required(VERSION 3.12)
project(BolusTrackingCpp CXX)

set(CMAKE_OSX_ARCHITECTURES "x86_64")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Eigen3
find_package(Eigen3 REQUIRED)

# Find TIFF
find_package(TIFF REQUIRED)

add_executable(bolus_tracking_cpp bolus_tracking_cpp.cpp)

target_include_directories(bolus_tracking_cpp PRIVATE ${TIFF_INCLUDE_DIRS})
target_link_libraries(bolus_tracking_cpp PRIVATE ${TIFF_LIBRARIES} Eigen3::Eigen)
