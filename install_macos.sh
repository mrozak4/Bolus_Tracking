#!/bin/bash
set -e

# macOS Installation and App Bundle Builder for Bolus Tracking Studio

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_NAME="BolusTrackingStudio"
APP_BUNDLE="$REPO_DIR/$APP_NAME.app"

echo "============================================="
echo "   Installing Bolus Tracking Studio App      "
echo "============================================="

# 1. Compile C++ Code
echo "Step 1: Compiling C++ GUI application..."
mkdir -p "$REPO_DIR/build"
cd "$REPO_DIR/build"
cmake -DBUILD_GUI=ON ..
make -j4

# 2. Generate AppIcon.icns if not present or rebuild it
cd "$REPO_DIR"
if [ ! -f "AppIcon.icns" ] && [ -f "app_icon.png" ]; then
    echo "Step 2: Generating AppIcon.icns from app_icon.png..."
    bash create_app_icon.sh
elif [ -f "AppIcon.icns" ]; then
    echo "Step 2: AppIcon.icns already exists. Skipping icon generation."
else
    echo "Step 2 Warning: app_icon.png not found. Continuing without icon."
fi

# 3. Create .app Bundle Directory Structure
echo "Step 3: Creating macOS .app bundle structure..."
rm -rf "$APP_BUNDLE"
mkdir -p "$APP_BUNDLE/Contents/MacOS"
mkdir -p "$APP_BUNDLE/Contents/Resources"

# 4. Copy Executable and Icon
echo "Step 4: Copying binaries and resources..."
cp "$REPO_DIR/build/bolus_tracking_gui" "$APP_BUNDLE/Contents/MacOS/bolus_tracking_gui"

if [ -f "AppIcon.icns" ]; then
    cp "$REPO_DIR/AppIcon.icns" "$APP_BUNDLE/Contents/Resources/AppIcon.icns"
fi

# 5. Create Info.plist
echo "Step 5: Writing Info.plist metadata configuration..."
cat <<EOF > "$APP_BUNDLE/Contents/Info.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>bolus_tracking_gui</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon.icns</string>
    <key>CFBundleIdentifier</key>
    <string>com.bolustracking.studio</string>
    <key>CFBundleName</key>
    <string>Bolus Tracking Studio</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.15</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
EOF

# 6. Touch the bundle to notify Finder of the update
touch "$APP_BUNDLE"

echo "============================================="
echo "   Build completed successfully!             "
echo "============================================="
echo "You can launch the GUI by double-clicking:"
echo "   $APP_BUNDLE"
echo ""
echo "Or, if you want to copy it to your Applications folder, run:"
echo "   cp -R \"$APP_BUNDLE\" /Applications/"
echo "============================================="
