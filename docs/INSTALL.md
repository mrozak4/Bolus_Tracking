# Installation Guide: Bolus Tracking Studio

**[English](INSTALL.md) | [Français (Québec)](INSTALL_FR.md)**

---

This guide describes how to install and configure **Bolus Tracking Studio** on macOS, Linux, and Windows.

---

## macOS Installation (Recommended)

### Option A: Download the DMG (Easiest)

1. Download the DMG from the [latest release](https://github.com/mrozak4/Bolus_Tracking/releases/latest):
   - **Apple Silicon** (M1/M2/M3/M4): `BolusTrackingStudio-3.0.0-arm64.dmg`
   - **Intel**: `BolusTrackingStudio-3.0.0-x86_64.dmg`
2. Open the `.dmg` and drag **Bolus Tracking Studio** to **Applications**.
3. On first launch, macOS Gatekeeper may block the app. Fix with:
   ```bash
   xattr -cr "/Applications/Bolus Tracking Studio.app"
   ```

### Option B: Build from Source

#### Prerequisites:
```bash
brew install eigen libtiff
```

#### Build:
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target bolus_tracking_gui -j8
```

#### Launch:
```bash
open "Bolus Tracking Studio.app"
```

#### Create a DMG:
```bash
./macos/create_dmg.sh              # Builds for current architecture
./macos/create_dmg.sh --arch arm64  # Apple Silicon
./macos/create_dmg.sh --arch x86_64 # Intel
```

---

## Linux Installation

### Prerequisites:
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libeigen3-dev libtiff5-dev \
    libglfw3-dev libgl1-mesa-dev zlib1g-dev -y
```

### Build:
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target bolus_tracking_gui -j$(nproc)
```

### Create a Desktop Entry Launcher:
Create a file at `~/.local/share/applications/bolus_tracking.desktop`:
```ini
[Desktop Entry]
Type=Application
Name=Bolus Tracking Studio
Comment=Capillary Bolus Tracking & Gamma Curve Fitting Studio
Exec=/path/to/Bolus_Tracking/build/bolus_tracking_gui
Icon=/path/to/Bolus_Tracking/resources/app_icon.png
Terminal=false
Categories=Science;ScientificVisualization;
```
*(Replace `/path/to/Bolus_Tracking` with the absolute path to your repository).*

---

## Windows Installation

### Prerequisites:
1. Install **Visual Studio Community** (2019 or newer) with the **C++ Desktop Development** workload.
2. Install **CMake for Windows** (add to PATH during setup).
3. Install **vcpkg** and use it to install dependencies:
   ```cmd
   vcpkg install eigen3 tiff glfw3 zlib
   ```

### Build:
```cmd
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --target bolus_tracking_gui --config Release
```

### Create a Desktop Shortcut:
1. Right-click the desktop → **New > Shortcut**.
2. Browse to `build/Release/bolus_tracking_gui.exe`.
3. Name it **Bolus Tracking Studio**.

---

## CLI Pipeline Only (No GUI)

To build only the command-line pipeline tool:
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target bolus_tracking_cpp -j8
```

Run it with:
```bash
./bolus_tracking_cpp --folder /path/to/subject/data
```

---

## Deprecated GUIs

> ⚠️ The native C++ app (`gui/`) and Python GUI (`python/src/bolus_gui.py`) are **deprecated**. Use the native C++ app instead.
