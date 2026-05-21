cmake_minimum_required(VERSION 3.14)
project(BolusTrackingCpp CXX)

set(CMAKE_OSX_ARCHITECTURES "x86_64")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Eigen3
find_package(Eigen3 REQUIRED)

# Find TIFF
find_package(TIFF REQUIRED)

# Core pipeline command-line executable
add_executable(bolus_tracking_cpp bolus_tracking_cpp.cpp bolus_tracking_cpp.hpp)
target_include_directories(bolus_tracking_cpp PRIVATE ${TIFF_INCLUDE_DIRS})
target_link_libraries(bolus_tracking_cpp PRIVATE ${TIFF_LIBRARIES} Eigen3::Eigen)

# Core pipeline test executable
add_executable(test_bolus_tracking_cpp test_bolus_tracking_cpp.cpp bolus_tracking_cpp.cpp bolus_tracking_cpp.hpp)
target_compile_definitions(test_bolus_tracking_cpp PRIVATE BUILD_TESTS=1)
target_include_directories(test_bolus_tracking_cpp PRIVATE ${TIFF_INCLUDE_DIRS})
target_link_libraries(test_bolus_tracking_cpp PRIVATE ${TIFF_LIBRARIES} Eigen3::Eigen)

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
        bolus_gui.cpp
        bolus_tracking_cpp.cpp
        bolus_tracking_cpp.hpp
        ${IMGUI_SRCS}
        ${IMPLOT_SRCS}
    )
    
    # Include directories
    target_include_directories(bolus_tracking_gui PRIVATE
        ${TIFF_INCLUDE_DIRS}
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${implot_SOURCE_DIR}
    )
    
    # Define GUI macro
    target_compile_definitions(bolus_tracking_gui PRIVATE BUILD_GUI=1)
    
    # Link libraries
    target_link_libraries(bolus_tracking_gui PRIVATE
        ${TIFF_LIBRARIES}
        Eigen3::Eigen
        glfw
        OpenGL::GL
    )
    
    # Platform-specific framework linking for GLFW on macOS
    if(APPLE)
        target_link_libraries(bolus_tracking_gui PRIVATE
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
        )
    endif()
endif()
