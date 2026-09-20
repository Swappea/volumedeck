#!/bin/bash

# Docker build script for cross-platform compilation

set -e

echo "======================================"
echo "VolumeDeck Plugin - Multi-Platform Build"
echo "======================================"
echo ""
echo "🐋 All builds running inside Docker container"
echo ""

# Ensure we're in the right directory
cd /workspace

# Check required environment variables
if [ -z "$XPLANE_SDK_DIR" ]; then
    echo "ERROR: XPLANE_SDK_DIR environment variable not set"
    echo "Please set it to point to your X-Plane SDK directory"
    exit 1
fi

echo "Using X-Plane SDK: $XPLANE_SDK_DIR"

# Clean previous Docker builds to ensure fresh compilation
echo "Cleaning previous Docker builds..."
rm -rf build/linux build/windows build/macos dist
mkdir -p dist

echo ""
echo "======================================"
echo "Building for Linux (inside Docker)"
echo "======================================"
echo "Compiler: GCC $(gcc --version | head -1 | grep -oP '\d+\.\d+\.\d+' || gcc --version | head -1)"
echo "CMake: $(cmake --version | head -1)"

mkdir -p build/linux
cd build/linux
cmake -DCMAKE_BUILD_TYPE=Release -DSDK_VERSION=301 ../..
cmake --build . --config Release -j$(nproc)
cd ../..

# Copy Linux build
mkdir -p dist/lin_x64
cp build/linux/lin_x64/VolumeDeck.xpl dist/lin_x64/

echo "✓ Linux build complete"

echo ""
echo "======================================"
echo "Building for Windows (MinGW cross-compile)"
echo "======================================"

mkdir -p build/windows
cd build/windows

cmake -DCMAKE_TOOLCHAIN_FILE=../../toolchain-win.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DSDK_VERSION=301 \
      ../..

cmake --build . --config Release -j$(nproc)
cd ../..

# Copy Windows build
mkdir -p dist/win_x64
cp build/windows/win_x64/VolumeDeck.xpl dist/win_x64/

echo "✓ Windows build complete"

echo ""
echo "======================================"
echo "Building for macOS (OSXCross)"
echo "======================================"

# Check for custom OpenGL headers
if [ -n "$MACOS_OPENGL_HEADERS_DIR" ]; then
    echo "✓ Using OpenGL headers from: $MACOS_OPENGL_HEADERS_DIR"
    echo "✓ Using system headers from OSXCross SDK (macOS 12.3)"
else
    echo "⚠ MACOS_OPENGL_HEADERS_DIR not set, using OSXCross defaults"
fi

mkdir -p build/macos
cd build/macos

cmake -DCMAKE_TOOLCHAIN_FILE=../../toolchain-mac.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DSDK_VERSION=301 \
      ../..

if cmake --build . --config Release -j$(nproc); then
    cd ../..
    # Copy macOS build
    mkdir -p dist/mac_x64
    cp build/macos/mac_x64/VolumeDeck.xpl dist/mac_x64/
    echo "✓ macOS build complete"
else
    cd ../..
    echo "⚠ macOS build failed"
    echo "  For macOS builds, compile natively on a Mac using ./build.sh"
    echo "  Continuing with Linux and Windows builds..."
fi

echo ""
echo "======================================"
echo "Build Summary"
echo "======================================"

echo ""
echo "Platform builds:"
ls -lh dist/*/VolumeDeck.xpl 2>/dev/null || true

echo ""
echo "All platform builds complete!"
echo "Output directory: /workspace/dist/"
echo "======================================"
