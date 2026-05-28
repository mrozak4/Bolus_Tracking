#!/usr/bin/env bash
# =============================================================================
# create_dmg.sh
# Build Bolus Tracking Studio as a macOS .app bundle and package it into a DMG.
#
# Usage:
#   ./create_dmg.sh              # Build for the current architecture
#   ./create_dmg.sh --arch arm64 # Build for Apple Silicon
#   ./create_dmg.sh --arch x86_64 # Build for Intel
# =============================================================================

set -e  # Exit immediately on any error

# ---- Configuration ----------------------------------------------------------
APP_NAME='Bolus Tracking Studio'
VERSION='3.0.0'
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ---- Parse arguments --------------------------------------------------------
ARCH="$(uname -m)"  # Default to current machine architecture

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            ARCH="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--arch arm64|x86_64]"
            exit 1
            ;;
    esac
done

# Validate architecture
if [[ "$ARCH" != "arm64" && "$ARCH" != "x86_64" ]]; then
    echo "Error: Unsupported architecture '${ARCH}'. Must be arm64 or x86_64."
    exit 1
fi

echo "==> Building ${APP_NAME} v${VERSION} for ${ARCH}"

# ---- Build with CMake -------------------------------------------------------
BUILD_DIR="${PROJECT_ROOT}/build_${ARCH}"
mkdir -p "${BUILD_DIR}"

# Detect Homebrew prefix for finding dependencies (TIFF, Eigen, ZLIB)
# Use the actual brew command rather than guessing by target architecture,
# since the host machine's Homebrew provides the libraries regardless of
# whether we're cross-compiling or building natively.
if command -v brew &>/dev/null; then
    BREW_PREFIX="$(brew --prefix)"
elif [[ -d "/opt/homebrew" ]]; then
    BREW_PREFIX="/opt/homebrew"
else
    BREW_PREFIX="/usr/local"
fi
echo "    Homebrew prefix: ${BREW_PREFIX}"

echo "==> Configuring CMake (Release, ${ARCH})..."
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="${ARCH}" \
    -DCMAKE_PREFIX_PATH="${BREW_PREFIX}"

echo "==> Building bolus_tracking_gui..."
cmake --build "${BUILD_DIR}" --target bolus_tracking_gui --config Release -j8

# ---- Locate the .app bundle built by CMake ----------------------------------
# CMake with MACOSX_BUNDLE creates the .app structure automatically
BUNDLE_DIR="${BUILD_DIR}/${APP_NAME}.app"

if [[ ! -d "${BUNDLE_DIR}" ]]; then
    echo "Error: App bundle not found at ${BUNDLE_DIR}"
    echo "CMake should have created it with MACOSX_BUNDLE."
    exit 1
fi

echo "==> App bundle found: ${BUNDLE_DIR}"

# Copy the icon file if it exists and isn't already in Resources
RESOURCES_DIR="${BUNDLE_DIR}/Contents/Resources"
mkdir -p "${RESOURCES_DIR}"
if [[ -f "${PROJECT_ROOT}/macos/app.icns" ]]; then
    cp "${PROJECT_ROOT}/macos/app.icns" "${RESOURCES_DIR}/app.icns"
    echo "    Icon copied."
elif [[ -f "${PROJECT_ROOT}/resources/AppIcon.icns" ]]; then
    cp "${PROJECT_ROOT}/resources/AppIcon.icns" "${RESOURCES_DIR}/AppIcon.icns"
    echo "    Icon copied from resources/."
else
    echo "    Warning: No app.icns found; bundle will use default icon."
fi

# Copy runtime resources (fonts, sounds) into the bundle
if [[ -d "${PROJECT_ROOT}/resources" ]]; then
    cp -R "${PROJECT_ROOT}/resources" "${RESOURCES_DIR}/resources"
    echo "    Runtime resources copied into bundle."
fi

# ---- Bundle dynamic libraries (libtiff + deps) -----------------------------
EXECUTABLE="${BUNDLE_DIR}/Contents/MacOS/${APP_NAME}"
LIBS_DIR="${BUNDLE_DIR}/Contents/libs"
echo "==> Bundling dynamic libraries into app..."

if command -v dylibbundler &>/dev/null; then
    mkdir -p "${LIBS_DIR}"
    dylibbundler -od -b \
        -x "${EXECUTABLE}" \
        -d "${LIBS_DIR}" \
        -p @executable_path/../libs/
    echo "    dylibbundler: done."
else
    echo "    WARNING: dylibbundler not found. Bundling manually..."
    mkdir -p "${LIBS_DIR}"
    # Copy all non-system dylibs referenced by the executable
    for lib in $(otool -L "${EXECUTABLE}" | awk '/\/usr\/local\/|\/opt\/homebrew\//{print $1}'); do
        echo "    Copying ${lib}..."
        cp -L "${lib}" "${LIBS_DIR}/"
        lib_name="$(basename "${lib}")"
        chmod 644 "${LIBS_DIR}/${lib_name}"
        # Rewrite the reference in the main executable
        install_name_tool -change "${lib}" "@executable_path/../libs/${lib_name}" "${EXECUTABLE}"
        # Also rewrite the id of the copied lib
        install_name_tool -id "@executable_path/../libs/${lib_name}" "${LIBS_DIR}/${lib_name}"
        # Fix transitive deps inside the copied lib
        for dep in $(otool -L "${LIBS_DIR}/${lib_name}" | awk '/\/usr\/local\/|\/opt\/homebrew\//{print $1}'); do
            dep_name="$(basename "${dep}")"
            if [[ ! -f "${LIBS_DIR}/${dep_name}" ]]; then
                cp -L "${dep}" "${LIBS_DIR}/"
                chmod 644 "${LIBS_DIR}/${dep_name}"
                install_name_tool -id "@executable_path/../libs/${dep_name}" "${LIBS_DIR}/${dep_name}"
            fi
            install_name_tool -change "${dep}" "@executable_path/../libs/${dep_name}" "${LIBS_DIR}/${lib_name}"
        done
    done
    echo "    Manual bundling: done."
fi

# ---- Code-sign the bundle (ad-hoc) -----------------------------------------
echo "==> Ad-hoc code-signing the app bundle..."
codesign --force --deep -s - "${BUNDLE_DIR}"

# ---- Create the DMG ---------------------------------------------------------
DMG_NAME="BolusTrackingStudio-${VERSION}-${ARCH}.dmg"
DMG_PATH="${BUILD_DIR}/${DMG_NAME}"
TMP_DMG_DIR="${BUILD_DIR}/tmp_dmg"

echo "==> Creating DMG: ${DMG_NAME}"

# Clean up any previous temp folder or DMG
rm -rf "${TMP_DMG_DIR}"
rm -f "${DMG_PATH}"

# Set up temp folder with .app and Applications symlink
mkdir -p "${TMP_DMG_DIR}"
cp -R "${BUNDLE_DIR}" "${TMP_DMG_DIR}/"
ln -s /Applications "${TMP_DMG_DIR}/Applications"

# Create the compressed DMG
hdiutil create \
    -volname "${APP_NAME}" \
    -srcfolder "${TMP_DMG_DIR}" \
    -ov \
    -format UDZO \
    "${DMG_PATH}"

# ---- Clean up ---------------------------------------------------------------
echo "==> Cleaning up temporary files..."
rm -rf "${TMP_DMG_DIR}"

# ---- Done -------------------------------------------------------------------
echo ""
echo "============================================="
echo "  SUCCESS!"
echo "  DMG created at:"
echo "  ${DMG_PATH}"
echo "============================================="
