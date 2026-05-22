#!/bin/bash
set -e

# Script to convert a PNG image to macOS .icns format
PNG_IMAGE="resources/app_icon.png"
OUTPUT_ICNS="resources/AppIcon.icns"

if [ ! -f "$PNG_IMAGE" ]; then
    echo "Error: $PNG_IMAGE not found!"
    exit 1
fi

echo "Creating iconset directory..."
rm -rf AppIcon.iconset
mkdir AppIcon.iconset

echo "Resizing images using sips..."
sips -s format png -z 16 16     "$PNG_IMAGE" --out AppIcon.iconset/icon_16x16.png
sips -s format png -z 32 32     "$PNG_IMAGE" --out AppIcon.iconset/icon_16x16@2x.png
sips -s format png -z 32 32     "$PNG_IMAGE" --out AppIcon.iconset/icon_32x32.png
sips -s format png -z 64 64     "$PNG_IMAGE" --out AppIcon.iconset/icon_32x32@2x.png
sips -s format png -z 128 128   "$PNG_IMAGE" --out AppIcon.iconset/icon_128x128.png
sips -s format png -z 256 256   "$PNG_IMAGE" --out AppIcon.iconset/icon_128x128@2x.png
sips -s format png -z 256 256   "$PNG_IMAGE" --out AppIcon.iconset/icon_256x256.png
sips -s format png -z 512 512   "$PNG_IMAGE" --out AppIcon.iconset/icon_256x256@2x.png
sips -s format png -z 512 512   "$PNG_IMAGE" --out AppIcon.iconset/icon_512x512.png
sips -s format png -z 1024 1024 "$PNG_IMAGE" --out AppIcon.iconset/icon_512x512@2x.png

echo "Building AppIcon.icns..."
iconutil -c icns AppIcon.iconset
mv AppIcon.icns resources/AppIcon.icns

echo "Cleaning up..."
rm -rf AppIcon.iconset

echo "AppIcon.icns created successfully!"
