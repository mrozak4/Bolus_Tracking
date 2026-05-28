# Python vs C++ Implementation Parity & Performance Report

**[English](PARITY_REPORT.md) | [Français (Québec)](PARITY_REPORT_FR.md)**

---

This report compares the Python and C++ implementations of the Bolus Tracking pipeline, analyzing performance, numerical parity, and architectural trade-offs.

## 1. Performance Benchmark

The following benchmark was run using the **Sample Subject 2259** dataset, consisting of **6 TIFF files** (each has 300 frames of $512 \times 512$ images, and 70 distinct ROIs mapped).

| Phase / Metric | Python Pipeline | C++ Pipeline | Speedup / Difference |
| :--- | :--- | :--- | :--- |
| **Docker Setup / Image Build** (One-time) | ~4.3 seconds | ~3.8 seconds (Cached)<br>~45.0 seconds (Uncached build & compile) | Setup only |
| **Raw Compute** (Numerical Fitting) | ~63.71 seconds | ~5.57 seconds | **~11.4x Faster** |
| **Execution Time** (Docker Run + I/O Overhead) | ~63.85 seconds | ~33.32 seconds | **~1.9x Faster** |

### Benchmark Breakdown:
* **Docker Setup (One-time Build)**: The initial step where the Docker container builds. For C++, this includes container environment configuration, CMake build generation, and code compilation (`make`). For Python, it installs standard dependency layers and packages (`matplotlib`).
* **Execution Time (Container Run)**: The runtime overhead of launching Docker, mounting local directories for data-sharing, and executing the compiled binaries. C++ parallel processing speeds up the core computational load, though host-to-container disk mounting adds a static overhead.

### Why C++ is Faster:
1. **Parallel Execution**: C++ implements multi-threading via `std::async` to utilize all available CPU cores when fitting ROIs concurrently. Python is constrained to sequential execution by the Global Interpreter Lock (GIL).
2. **Compiled Execution**: The core calculations (spline interpolation, matrix operations via Eigen, and parameter optimization) compile directly to native machine code.
3. **No Interpreter Overhead**: Eliminates the startup and runtime overhead of the Python virtual machine.

---

## 2. Numerical Parity Analysis

Both pipelines show exact numerical agreement (within standard floating-point precision) across both the initial automated pass and the population-prior rescue pass:

- **Exact Algorithmic Alignment**: 
  - **Functor Copying Issue Resolved (C++)**: In the C++ Cauchy robust pass, the Eigen Levenberg-Marquardt solver and functor are now correctly re-instantiated, ensuring the Cauchy scale factor and robust weights propagate properly.
  - **Single-Pass Bounds Bug Resolved (Python)**: The Python pipeline was corrected so that custom fit bounds overrides do not unconditionally force `single_pass = True`. Instead, the second fallback pass is correctly triggered for noisy/edge-case ROIs, bringing Python and C++ outputs into exact parity.
  - **Optimizer Convergence and Evaluation Budget Parity**: The C++ optimizer function evaluation limit (`maxfev`) was increased to `10000` to match SciPy's default budget, and the success condition was updated to accept status code `5` (maximum evaluations/iterations reached). This ensures that marginal/noisy traces (e.g. ROI 44 in Subject 2259 Bolus 1) that terminate at the evaluation limit return their near-optimal fit parameters and correct `WARN` flags in C++, establishing full parity with Python's curve fits.
- **Parameter Agreement**: Calculated metrics (Amplitude, Time-to-Peak, FWHM, Baseline, SNR, CNR, AUC, OnT, OnTSc, and Transit Time bounds) are fully aligned.
- **Denoising and Spline Parity**: Denoising (1D Gaussian) and cubic spline upsampling are mathematically identical, feeding identical values to both fitting engines.

---

## 3. Recommendation

### We Recommend the **C++ Implementation** for Production, Batch Analysis, and Reviews (GUI)
For daily research, cohort studies, and manual review triage, the **C++ ecosystem** is highly recommended:
* **Time Efficiency**: Parallel fitting reduces cohort-scale computational loads from hours to seconds.
* **Bolus Tracking Studio**: The **native C++ Bolus Tracking Studio** (`cpp/src/bolus_gui.cpp`) is a single-binary desktop app built with Dear ImGui, ImPlot, and GLFW. It provides a modern triage interface for TIFF loading, fitting, and interactive plot rendering. Operators can review, filter (PASS/WARN/FAIL/REVIEW/STALL), sort by fit quality (ROI#, QC severity, or CNR), manually re-fit (draggable onset, peak, end markers), override QC status (Force PASS/FAIL/STALL), crop fitting ranges, and save directly — all with the signature MCM dark theme and 44-language localization.
* **Single Backend / High Portability**: The C++ backend can be built on Windows, Linux, and macOS. The native app requires only cmake and native dependencies (Eigen, libtiff, GLFW).

### We Recommend the **Python Implementation** for Prototyping and Scripting Extensions
* **Rapid Prototyping**: If you are experimenting with new signal-processing methods, loss weights (e.g. Huber vs. Cauchy loss), or custom visualization backends, the Python environment is fast and doesn't require compilation.
* **Matplotlib Customization**: Helpful if you need to build highly customized scripts to generate specific figures or integrate with other data science pipelines.
