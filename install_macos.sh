#!/bin/bash
set -e

# ══════════════════════════════════════════════════════════════════════════════
#  Bolus Tracking Studio — macOS One-Click Installer
#
#  Builds and installs the Electron-based Bolus Tracking Studio as a native
#  macOS .app bundle with the C++ bolus_server backend embedded inside.
#
#  After installation, the user can launch from Launchpad, Finder, or Spotlight.
#  No Homebrew, CMake, Node.js, or terminal knowledge required after install.
#
#  Prerequisites are auto-installed with user confirmation.
# ══════════════════════════════════════════════════════════════════════════════

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_NAME="BolusTrackingStudio"

echo ""
echo "══════════════════════════════════════════════════"
echo "   Bolus Tracking Studio — macOS Installer        "
echo "══════════════════════════════════════════════════"
echo ""
echo "This script will:"
echo "  1. Install any missing build tools (with your permission)"
echo "  2. Compile the C++ analysis engine"
echo "  3. Package the Electron GUI as a native macOS app"
echo "  4. Install it to your Applications folder"
echo ""

# ── Helper: ask yes/no ──────────────────────────────────────────────────────

confirm() {
    local prompt="$1"
    if [ ! -t 0 ]; then
        # Non-interactive: auto-yes
        return 0
    fi
    echo -n "$prompt [Y/n] "
    read -r response
    [[ "$response" =~ ^([yY][eE][sS]|[yY]|"")$ ]]
}

# ── Step 1: Check and install prerequisites ─────────────────────────────────

echo "Step 1: Checking prerequisites..."
echo ""

NEED_INSTALL=()

# 1a. Xcode Command Line Tools (required for any compilation on macOS)
if ! xcode-select -p &>/dev/null; then
    echo "  ✗ Xcode Command Line Tools not found"
    echo "    Installing... (Apple will prompt you to confirm)"
    xcode-select --install 2>/dev/null || true
    echo ""
    echo "    ⏳ Please complete the Xcode Command Line Tools installation"
    echo "       in the popup window, then re-run this script."
    echo ""
    exit 1
else
    echo "  ✓ Xcode Command Line Tools"
fi

# 1b. Homebrew
if ! command -v brew &>/dev/null; then
    echo "  ✗ Homebrew not found"
    echo ""
    echo "    Homebrew is a package manager that installs developer tools."
    echo "    It's the standard way to install libraries on macOS."
    echo "    Learn more: https://brew.sh"
    echo ""
    if confirm "    Install Homebrew now?"; then
        echo "    Installing Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
        # Add brew to PATH for Apple Silicon Macs
        if [ -f "/opt/homebrew/bin/brew" ]; then
            eval "$(/opt/homebrew/bin/brew shellenv)"
        fi
        echo "  ✓ Homebrew installed"
    else
        echo "    Skipping. Please install Homebrew manually: https://brew.sh"
        exit 1
    fi
else
    echo "  ✓ Homebrew"
fi

# 1c. CMake
if ! command -v cmake &>/dev/null; then
    echo "  ✗ CMake not found"
    NEED_INSTALL+=("cmake")
else
    echo "  ✓ CMake"
fi

# 1d. Eigen3
EIGEN_FOUND=false
for d in /opt/homebrew/include/eigen3 /usr/local/include/eigen3 /usr/include/eigen3; do
    [ -d "$d" ] && EIGEN_FOUND=true && break
done
if ! $EIGEN_FOUND && ! pkg-config --exists eigen3 2>/dev/null; then
    echo "  ✗ Eigen3 (linear algebra library) not found"
    NEED_INSTALL+=("eigen")
else
    echo "  ✓ Eigen3"
fi

# 1e. libtiff
TIFF_FOUND=false
for f in /opt/homebrew/include/tiff.h /usr/local/include/tiff.h /usr/include/tiff.h; do
    [ -f "$f" ] && TIFF_FOUND=true && break
done
if ! $TIFF_FOUND && ! pkg-config --exists libtiff-4 2>/dev/null; then
    echo "  ✗ libtiff (TIFF image library) not found"
    NEED_INSTALL+=("libtiff")
else
    echo "  ✓ libtiff"
fi

# 1f. Node.js (for Electron packaging)
if ! command -v node &>/dev/null; then
    echo "  ✗ Node.js not found"
    NEED_INSTALL+=("node")
else
    NODE_VER=$(node --version 2>/dev/null | sed 's/v//' | cut -d. -f1)
    if [ "$NODE_VER" -lt 18 ] 2>/dev/null; then
        echo "  ✗ Node.js version too old (need ≥ 18, found v$NODE_VER)"
        NEED_INSTALL+=("node")
    else
        echo "  ✓ Node.js $(node --version)"
    fi
fi

# Install missing packages
if [ ${#NEED_INSTALL[@]} -ne 0 ]; then
    echo ""
    echo "  Missing packages: ${NEED_INSTALL[*]}"
    echo ""
    if confirm "  Install them via Homebrew?"; then
        echo "  Installing ${NEED_INSTALL[*]}..."
        brew install "${NEED_INSTALL[@]}"
        echo "  ✓ All packages installed"
    else
        echo "  Skipping. Please install manually:"
        echo "    brew install ${NEED_INSTALL[*]}"
        exit 1
    fi
fi

echo ""
echo "  All prerequisites satisfied!"
echo ""

# ── Step 2: Compile C++ backend (bolus_server + bolus_tracking_cpp) ─────────

echo "Step 2: Compiling C++ analysis engine..."

# Handle macOS SDK paths
if [[ "$OSTYPE" == "darwin"* ]]; then
    SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null || echo "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
    export CXXFLAGS="-isysroot $SDK_PATH"
fi

mkdir -p "$REPO_DIR/build"
cd "$REPO_DIR/build"
cmake -DCMAKE_BUILD_TYPE=Release ..
make bolus_server -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
make bolus_tracking_cpp -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
cd "$REPO_DIR"

if [ ! -f "$REPO_DIR/build/bolus_server" ]; then
    echo "ERROR: bolus_server binary not found after compilation."
    exit 1
fi
echo "  ✓ C++ backend compiled successfully"
echo ""

# ── Step 3: Install npm dependencies and package Electron app ───────────────

echo "Step 3: Packaging Electron application..."

cd "$REPO_DIR/gui"

# Install npm dependencies (electron, electron-builder)
echo "  Installing npm dependencies..."
npm install --no-audit --no-fund 2>&1 | tail -1

# Run electron-builder to create the .app bundle
echo "  Building macOS .app bundle..."
npx electron-builder --mac --config.mac.target=dir 2>&1 | grep -E "^  •|Building|packing"

cd "$REPO_DIR"

# Find the built app
BUILT_APP=$(find "$REPO_DIR/gui/dist/mac"* -name "*.app" -maxdepth 1 2>/dev/null | head -1)
if [ -z "$BUILT_APP" ]; then
    # Fallback: try alternate output paths
    BUILT_APP=$(find "$REPO_DIR/gui/dist" -name "*.app" -maxdepth 2 2>/dev/null | head -1)
fi

if [ -z "$BUILT_APP" ] || [ ! -d "$BUILT_APP" ]; then
    echo "ERROR: Could not find built .app bundle in gui/dist/"
    echo "  Attempting manual packaging as fallback..."

    # ── Fallback: manually construct the .app bundle ────────────────────
    APP_BUNDLE="$REPO_DIR/$APP_NAME.app"
    rm -rf "$APP_BUNDLE"

    mkdir -p "$APP_BUNDLE/Contents/MacOS"
    mkdir -p "$APP_BUNDLE/Contents/Resources/app"
    mkdir -p "$APP_BUNDLE/Contents/Resources/bin"
    mkdir -p "$APP_BUNDLE/Contents/Resources/sounds"
    mkdir -p "$APP_BUNDLE/Contents/Resources/fonts"

    # Copy Electron binary (use the local electron installation)
    ELECTRON_PATH="$REPO_DIR/gui/node_modules/electron/dist/Electron.app/Contents/MacOS/Electron"
    if [ -f "$ELECTRON_PATH" ]; then
        cp "$ELECTRON_PATH" "$APP_BUNDLE/Contents/MacOS/$APP_NAME"
    else
        echo "ERROR: Electron binary not found at $ELECTRON_PATH"
        exit 1
    fi

    # Copy Electron framework
    ELECTRON_FRAMEWORK="$REPO_DIR/gui/node_modules/electron/dist/Electron.app/Contents/Frameworks"
    if [ -d "$ELECTRON_FRAMEWORK" ]; then
        cp -R "$ELECTRON_FRAMEWORK" "$APP_BUNDLE/Contents/"
    fi

    # Copy app source files
    for f in main.js preload.js renderer.js index.html style.css package.json; do
        cp "$REPO_DIR/gui/$f" "$APP_BUNDLE/Contents/Resources/app/"
    done
    cp -R "$REPO_DIR/gui/locales" "$APP_BUNDLE/Contents/Resources/app/"
    cp -R "$REPO_DIR/gui/node_modules" "$APP_BUNDLE/Contents/Resources/app/" 2>/dev/null || true

    # Copy C++ backend
    cp "$REPO_DIR/build/bolus_server" "$APP_BUNDLE/Contents/Resources/bin/"
    cp "$REPO_DIR/build/bolus_tracking_cpp" "$APP_BUNDLE/Contents/Resources/bin/" 2>/dev/null || true
    chmod +x "$APP_BUNDLE/Contents/Resources/bin/"*

    # Copy resources
    [ -f "$REPO_DIR/resources/thx_crescendo.wav" ] && cp "$REPO_DIR/resources/thx_crescendo.wav" "$APP_BUNDLE/Contents/Resources/sounds/"
    [ -f "$REPO_DIR/resources/minion_squeak.wav" ] && cp "$REPO_DIR/resources/minion_squeak.wav" "$APP_BUNDLE/Contents/Resources/sounds/"
    [ -f "$REPO_DIR/resources/hallelujah.mp3" ] && cp "$REPO_DIR/resources/hallelujah.mp3" "$APP_BUNDLE/Contents/Resources/sounds/"
    [ -d "$REPO_DIR/resources/fonts" ] && cp -R "$REPO_DIR/resources/fonts/"* "$APP_BUNDLE/Contents/Resources/fonts/" 2>/dev/null || true

    # Copy icon
    [ -f "$REPO_DIR/resources/AppIcon.icns" ] && cp "$REPO_DIR/resources/AppIcon.icns" "$APP_BUNDLE/Contents/Resources/"

    # Write Info.plist
    cat <<EOF > "$APP_BUNDLE/Contents/Info.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>$APP_NAME</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon.icns</string>
    <key>CFBundleIdentifier</key>
    <string>com.stefanovic-lab.bolus-tracking-studio</string>
    <key>CFBundleName</key>
    <string>Bolus Tracking Studio</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>2.0.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.15</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
EOF

    BUILT_APP="$APP_BUNDLE"
    echo "  ✓ Manual app bundle created"
fi

echo "  ✓ App bundle ready: $(basename "$BUILT_APP")"
echo ""

# ── Step 4: Install to Applications ─────────────────────────────────────────

echo "Step 4: Installing to Applications folder..."

# Remove old installations
for loc in "/Applications/$APP_NAME.app" "$HOME/Applications/$APP_NAME.app" \
           "/Applications/Bolus Tracking Studio.app" "$HOME/Applications/Bolus Tracking Studio.app"; do
    if [ -d "$loc" ]; then
        echo "  Removing old version: $loc"
        rm -rf "$loc" 2>/dev/null || sudo rm -rf "$loc" 2>/dev/null || true
    fi
done

INSTALL_DEST="/Applications"
if [ -w "$INSTALL_DEST" ]; then
    cp -R "$BUILT_APP" "$INSTALL_DEST/"
    FINAL_APP="$INSTALL_DEST/$(basename "$BUILT_APP")"
else
    INSTALL_DEST="$HOME/Applications"
    mkdir -p "$INSTALL_DEST"
    cp -R "$BUILT_APP" "$INSTALL_DEST/"
    FINAL_APP="$INSTALL_DEST/$(basename "$BUILT_APP")"
fi

# Touch to update Finder/Spotlight/Launchpad
touch "$FINAL_APP"

echo "  ✓ Installed to: $FINAL_APP"
echo ""

# ── Done ────────────────────────────────────────────────────────────────────

echo "══════════════════════════════════════════════════"
echo "   ✓ Installation Complete!                       "
echo "══════════════════════════════════════════════════"
echo ""
echo "  Launch Bolus Tracking Studio from:"
echo "    • Launchpad (search for 'Bolus Tracking Studio')"
echo "    • Finder → Applications → Bolus Tracking Studio"
echo "    • Spotlight (Cmd+Space → 'Bolus Tracking')"
echo ""
echo "  Or from Terminal:"
echo "    open '$FINAL_APP'"
echo ""
echo "══════════════════════════════════════════════════"
