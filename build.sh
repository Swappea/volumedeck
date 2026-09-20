#!/bin/bash

# Build script for VolumeDeck X-Plane plugin

set -e

# Check for XPLANE_SDK_DIR environment variable
if [ -z "$XPLANE_SDK_DIR" ]; then
    echo "ERROR: XPLANE_SDK_DIR environment variable not set"
    echo "Please set it in your .bashrc or run:"
    echo "  export XPLANE_SDK_DIR=/path/to/xplane/SDK"
    echo "  ./build.sh"
    exit 1
fi

echo "Using X-Plane SDK: $XPLANE_SDK_DIR"

# Determine the platform
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    PLATFORM="linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    PLATFORM="mac"
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
    PLATFORM="windows"
else
    echo "Unknown platform: $OSTYPE"
    exit 1
fi

# Create build directory
BUILD_DIR="build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Run CMake
echo "Configuring build for $PLATFORM..."
cmake -DSDK_VERSION=301 ..

# Build
echo "Building plugin..."
cmake --build . --config Release

echo "Build complete!"
echo "Plugin output location: $BUILD_DIR/"

if [[ "$PLATFORM" == "linux" ]]; then
    echo "Linux plugin: lin_x64/VolumeDeck.xpl"
elif [[ "$PLATFORM" == "mac" ]]; then
    echo "Mac plugin: mac_x64/VolumeDeck.xpl"
elif [[ "$PLATFORM" == "windows" ]]; then
    echo "Windows plugin: win_x64/VolumeDeck.xpl"
fi

