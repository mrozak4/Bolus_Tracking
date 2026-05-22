# Installation Guide: Bolus Tracking Studio

**[English](INSTALL.md) | [Français (Québec)](INSTALL_FR.md)**

---

This guide describes how to install and configure **Bolus Tracking Studio** on macOS, Linux, and Windows.

---

## macOS Installation (Recommended)

On macOS, you can build a native clickable `.app` bundle with a custom icon.

### Automatic Installer:
Open your terminal, navigate to the project directory, and run the macOS installer script:
```bash
bash install_macos.sh
```

This script will:
1. Compile the high-performance C++ GUI application.
2. Build the macOS application bundle structure `BolusTrackingStudio.app`.
3. Create a beautiful app icon from `app_icon.png`.
4. Copy the bundle to your Applications directory (`/Applications` or fallback to user-local `~/Applications` if not writable) for automatic indexing in **Launchpad**.

### Launching the Application:
* **Option A (Launchpad)**: Press `F4` or click the Launchpad icon on your Dock, search for **Bolus Tracking Studio**, and click the application icon.
* **Option B (Finder)**: Open your Applications folder and double-click **Bolus Tracking Studio** (represented by the custom capillary and mathematical curve icon).
* **Option C (Terminal)**: Run it from the terminal:
  ```bash
  open /Applications/BolusTrackingStudio.app
  # or user-local fallback path:
  open ~/Applications/BolusTrackingStudio.app
  ```

---

## Linux Installation

On Linux, you can compile the app and add a desktop launcher shortcut.

### Prerequisites:
Make sure you have CMake, a C++17 compiler, and the development libraries for GLFW, OpenGL, and LibTIFF installed.
On Ubuntu/Debian, install them via:
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev libtiff5-dev -y
```

### Build the App:
```bash
mkdir -p build && cd build
cmake -DBUILD_GUI=ON ..
make -j$(nproc)
```

### Create a Desktop Entry Launcher:
Create a file at `~/.local/share/applications/bolus_tracking.desktop` with the following content:
```ini
[Desktop Entry]
Type=Application
Name=Bolus Tracking Studio
Comment=Capillary Bolus Tracking & Gamma Curve Fitting Studio
Exec=/path/to/Bolus_Tracking/build/bolus_tracking_gui
Icon=/path/to/Bolus_Tracking/app_icon.png
Terminal=false
Categories=Science;ScientificVisualization;
```
*(Replace `/path/to/Bolus_Tracking` with the absolute path to your repository).*

---

## Windows Installation

On Windows, you can compile the GUI using Visual Studio and set up a clickable desktop shortcut.

### Prerequisites:
1. Install **Visual Studio Community** (2019 or newer) with the **C++ Desktop Development** workload selected.
2. Download and install **CMake for Windows** (make sure to add CMake to your system PATH during setup).

### Build the App:
1. Open Developer PowerShell / Command Prompt for Visual Studio.
2. Navigate to your project folder:
   ```cmd
   mkdir build
   cd build
   cmake -G "Visual Studio 17 2022" -A x64 -DBUILD_GUI=ON ..
   cmake --build . --config Release
   ```
3. The executable will be created at `build/Release/bolus_tracking_gui.exe`.

### Create a Desktop Shortcut:
1. Right-click the desktop and choose **New > Shortcut**.
2. Browse to the path of `build/Release/bolus_tracking_gui.exe`.
3. Name it **Bolus Tracking Studio**.
4. To set the custom icon:
   * Right-click the shortcut and select **Properties**.
   * Click **Change Icon...** and browse to `app_icon.png` (or convert `app_icon.png` to `app_icon.ico` using a web converter and select it).
