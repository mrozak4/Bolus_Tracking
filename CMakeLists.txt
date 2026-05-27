cmake_minimum_required(VERSION 3.14)
project(BolusTrackingCpp CXX)

# Default to native architecture on macOS if not specified (CI passes its own)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE MACOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(MACOS_SDK_PATH)
        add_compile_options("-isystem" "${MACOS_SDK_PATH}/usr/include/c++/v1")
    endif()
endif()



# Default to Release build type if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Eigen3
find_package(Eigen3 QUIET)
if(NOT Eigen3_FOUND)
    message(WARNING "-------------------------------------------------------------")
    message(WARNING " Eigen3 was not found on your system!")
    message(WARNING " To resolve this dependency, please install Eigen3:")
    if(APPLE)
        message(WARNING "   -> macOS: Run 'brew install eigen'")
    elseif(UNIX)
        message(WARNING "   -> Linux (Ubuntu/Debian): Run 'sudo apt-get install libeigen3-dev'")
    elseif(WIN32)
        message(WARNING "   -> Windows: Download from https://eigen.tuxfamily.org/ or use vcpkg:")
        message(WARNING "               'vcpkg install eigen3:x64-windows'")
    endif()
    message(WARNING "-------------------------------------------------------------")
    message(FATAL_ERROR "Missing dependency: Eigen3 is required to build the Bolus Tracking C++ application.")
endif()

# Find TIFF
find_package(TIFF QUIET)
if(NOT TIFF_FOUND)
    message(WARNING "-------------------------------------------------------------")
    message(WARNING " LibTIFF was not found on your system!")
    message(WARNING " To resolve this dependency, please install LibTIFF:")
    if(APPLE)
        message(WARNING "   -> macOS: Run 'brew install libtiff'")
    elseif(UNIX)
        message(WARNING "   -> Linux (Ubuntu/Debian): Run 'sudo apt-get install libtiff-dev' or 'libtiff5-dev'")
    elseif(WIN32)
        message(WARNING "   -> Windows: Download from http://www.libtiff.org/ or use vcpkg:")
        message(WARNING "               'vcpkg install tiff:x64-windows'")
    endif()
    message(WARNING "-------------------------------------------------------------")
    message(FATAL_ERROR "Missing dependency: LibTIFF is required to build the Bolus Tracking C++ application.")
endif()

# Find ZLIB
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    message(WARNING "-------------------------------------------------------------")
    message(WARNING " ZLIB was not found on your system!")
    message(WARNING " To resolve this dependency, please install ZLIB:")
    if(APPLE)
        message(WARNING "   -> macOS: macOS has ZLIB built-in. Make sure Command Line Tools are installed.")
    elseif(UNIX)
        message(WARNING "   -> Linux (Ubuntu/Debian): Run 'sudo apt-get install zlib1g-dev'")
    elseif(WIN32)
        message(WARNING "   -> Windows: Use vcpkg: 'vcpkg install zlib:x64-windows'")
    endif()
    message(WARNING "-------------------------------------------------------------")
    message(FATAL_ERROR "Missing dependency: ZLIB is required to build the Bolus Tracking C++ application.")
endif()

set(PIPELINE_SRCS
    cpp/src/signal_processing.cpp
    cpp/src/bolus_fitting.cpp
    cpp/src/roi_rasterizer.cpp
    cpp/src/bolus_visualizer.cpp
    cpp/src/dataset_processor.cpp
    cpp/src/batch_processor.cpp
)

# Core pipeline command-line executable
add_executable(bolus_tracking_cpp cpp/src/main.cpp ${PIPELINE_SRCS} cpp/include/bolus_tracking_cpp.hpp)
target_include_directories(bolus_tracking_cpp PRIVATE cpp/include ${TIFF_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})
target_link_libraries(bolus_tracking_cpp PRIVATE ${TIFF_LIBRARIES} Eigen3::Eigen ZLIB::ZLIB)

# Core pipeline test executable
add_executable(test_bolus_tracking_cpp cpp/tests/test_bolus_tracking_cpp.cpp ${PIPELINE_SRCS} cpp/include/bolus_tracking_cpp.hpp)
target_compile_definitions(test_bolus_tracking_cpp PRIVATE BUILD_TESTS=1)
target_include_directories(test_bolus_tracking_cpp PRIVATE cpp/include ${TIFF_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})
target_link_libraries(test_bolus_tracking_cpp PRIVATE ${TIFF_LIBRARIES} Eigen3::Eigen ZLIB::ZLIB)

# ========================================================
# IPC Server for Electron GUI
# ========================================================
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(json)

add_executable(bolus_server cpp/src/bolus_server.cpp ${PIPELINE_SRCS} cpp/include/bolus_tracking_cpp.hpp)
target_include_directories(bolus_server PRIVATE cpp/include ${TIFF_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})
target_link_libraries(bolus_server PRIVATE ${TIFF_LIBRARIES} Eigen3::Eigen ZLIB::ZLIB nlohmann_json::nlohmann_json)

# ========================================================
# GUI Build Configuration
# ========================================================
option(BUILD_GUI "Build the visual triage GUI" ON)

if(BUILD_GUI)
    include(FetchContent)
    
    # 1. Fetch GLFW
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.8
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
    
    # 2. Fetch Dear ImGui
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.89.9
    )
    FetchContent_MakeAvailable(imgui)
    
    # 3. Fetch ImPlot
    FetchContent_Declare(
        implot
        GIT_REPOSITORY https://github.com/epezent/implot.git
        GIT_TAG v0.16
    )
    FetchContent_MakeAvailable(implot)
    
    # Define Dear ImGui and ImPlot sources
    set(IMGUI_SRCS
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    
    set(IMPLOT_SRCS
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
        ${implot_SOURCE_DIR}/implot_demo.cpp
    )
    
    # Add OpenGL
    find_package(OpenGL REQUIRED)
    
    # Add GUI executable
    add_executable(bolus_tracking_gui
        cpp/src/bolus_gui.cpp
        cpp/include/bolus_gui.hpp
        cpp/src/bolus_locale.cpp
        ${PIPELINE_SRCS}
        cpp/include/bolus_tracking_cpp.hpp
        ${IMGUI_SRCS}
        ${IMPLOT_SRCS}
    )
    
    # Include directories
    target_include_directories(bolus_tracking_gui PRIVATE
        cpp/include
        ${TIFF_INCLUDE_DIRS}
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${implot_SOURCE_DIR}
    )
    
    # Define GUI macro and 32-bit Wchar support for ImGui
    target_compile_definitions(bolus_tracking_gui PRIVATE BUILD_GUI=1 IMGUI_USE_WCHAR32=1)
    
    # Link libraries
    target_link_libraries(bolus_tracking_gui PRIVATE
        ${TIFF_LIBRARIES}
        Eigen3::Eigen
        glfw
        OpenGL::GL
        ZLIB::ZLIB
    )
    
    # Platform-specific framework linking for GLFW on macOS
    if(APPLE)
        target_link_libraries(bolus_tracking_gui PRIVATE
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
        )
        
        # macOS app bundle configuration
        set_target_properties(bolus_tracking_gui PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/macos/Info.plist"
            MACOSX_BUNDLE_BUNDLE_NAME "Bolus Tracking Studio"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.mcm.bolus-tracking"
            MACOSX_BUNDLE_BUNDLE_VERSION "2.3.1"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "2.3.1"
            OUTPUT_NAME "Bolus Tracking Studio"
        )
    endif()
endif()
