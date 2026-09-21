#!/bin/bash

# Script to build VolumeDeck plugin for all platforms using Docker

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

echo "======================================"
echo "Building VolumeDeck Plugin"
echo "Multi-Platform Docker Build"
echo "======================================"
echo ""

# Check for required environment variables
if [ -z "$XPLANE_SDK_DIR" ]; then
    echo "ERROR: XPLANE_SDK_DIR environment variable not set"
    echo "Please set it in your .bashrc:"
    echo "  export XPLANE_SDK_DIR=/path/to/xplane/SDK"
    exit 1
fi

echo "Using X-Plane SDK: $XPLANE_SDK_DIR"

if [ -n "$MACOS_OPENGL_HEADERS_DIR" ]; then
    echo "Using custom OpenGL headers: $MACOS_OPENGL_HEADERS_DIR"
fi

# Build Docker image
echo ""
echo "Building Docker image..."
docker build -t volumedeck-builder .

echo ""
echo "Running multi-platform build..."
docker run --rm \
    -v "$SCRIPT_DIR:/workspace" \
    -v "$XPLANE_SDK_DIR:/xplane-sdk:ro" \
    -e XPLANE_SDK_DIR=/xplane-sdk \
    -e MACOS_OPENGL_HEADERS_DIR="$MACOS_OPENGL_HEADERS_DIR" \
    volumedeck-builder

echo ""
echo "======================================"
echo "Creating X-Plane Plugin Package"
echo "======================================"

# Fix ownership of Docker-created files
sudo chown -R $(id -u):$(id -g) dist/ 2>/dev/null || true

# Create proper X-Plane plugin structure
PACKAGE_DIR="dist/VolumeDeck"
rm -rf "$PACKAGE_DIR" 2>/dev/null || true
mkdir -p "$PACKAGE_DIR"

# Copy platform binaries
cp -r dist/lin_x64 "$PACKAGE_DIR/" 2>/dev/null || true
cp -r dist/win_x64 "$PACKAGE_DIR/" 2>/dev/null || true
cp -r dist/mac_x64 "$PACKAGE_DIR/" 2>/dev/null || true

# Copy documentation
cp README.md "$PACKAGE_DIR/" 2>/dev/null || echo "No README found"

# Get version from source
VERSION=$(grep "Version 1.0" src/main.cpp | head -1 | sed 's/.*Version \([0-9.]*\).*/\1/')
if [ -z "$VERSION" ]; then
    VERSION="1.0"
fi

# Create zip package (use tar if zip not available)
cd dist
if command -v zip &> /dev/null; then
    ZIP_NAME="VolumeDeck-v${VERSION}-XPlane.zip"
    zip -r "$ZIP_NAME" VolumeDeck/
    echo "Created: $ZIP_NAME"
else
    ZIP_NAME="VolumeDeck-v${VERSION}-XPlane.tar.gz"
    tar -czf "$ZIP_NAME" VolumeDeck/
    echo "Created: $ZIP_NAME (zip not available, used tar.gz)"
fi
cd ..

echo ""
echo "======================================"
echo "Build Complete!"
echo "======================================"
echo ""
echo "✅ Plugin binaries:"
ls -lh dist/VolumeDeck/*/VolumeDeck.xpl 2>/dev/null || echo "No builds found"

echo ""
echo "📦 Installation package created:"
ls -lh dist/VolumeDeck-v${VERSION}-XPlane.* 2>/dev/null || ls -lh dist/VolumeDeck-v${VERSION}-XPlane.*

echo ""
echo "======================================"
echo "Installation Instructions"
echo "======================================"
echo ""
echo "Quick Install:"
echo "  1. Extract the package file:"
if [ -f "dist/VolumeDeck-v${VERSION}-XPlane.zip" ]; then
    echo "     unzip dist/VolumeDeck-v${VERSION}-XPlane.zip"
else
    echo "     tar -xzf dist/VolumeDeck-v${VERSION}-XPlane.tar.gz"
fi
echo "  2. Copy the extracted 'VolumeDeck' folder to:"
echo "     X-Plane 12/Resources/plugins/"
echo ""
echo "Manual Install:"
echo "  Linux:   cp dist/VolumeDeck/lin_x64/VolumeDeck.xpl 'X-Plane 12/Resources/plugins/VolumeDeck/lin_x64/'"
echo "  Windows: cp dist/VolumeDeck/win_x64/VolumeDeck.xpl 'X-Plane 12/Resources/plugins/VolumeDeck/win_x64/'"
echo "  macOS:   cp dist/VolumeDeck/mac_x64/VolumeDeck.xpl 'X-Plane 12/Resources/plugins/VolumeDeck/mac_x64/'"
echo ""

