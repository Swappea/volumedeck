#!/bin/bash

# Installation script for VolumeDeck X-Plane plugin

set -e

# Default X-Plane path
DEFAULT_XPLANE_PATH="/games/X-Plane 12"

# Check if X-Plane path is provided, otherwise use default
if [ -z "$1" ]; then
    XPLANE_PATH="$DEFAULT_XPLANE_PATH"
    echo "No path provided, using default: $XPLANE_PATH"
else
    XPLANE_PATH="$1"
fi
PLUGIN_NAME="VolumeDeck"

# Verify X-Plane path exists
if [ ! -d "$XPLANE_PATH" ]; then
    echo "Error: X-Plane directory not found: $XPLANE_PATH"
    exit 1
fi

# Determine the platform
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    PLATFORM_DIR="lin_x64"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    PLATFORM_DIR="mac_x64"
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
    PLATFORM_DIR="win_x64"
else
    echo "Unknown platform: $OSTYPE"
    exit 1
fi

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Check if plugin is built
if [ ! -f "build/$PLATFORM_DIR/$PLUGIN_NAME.xpl" ]; then
    echo "Error: Plugin not built. Run ./build.sh first"
    exit 1
fi

# Create plugin directory structure
PLUGIN_DIR="$XPLANE_PATH/Resources/plugins/$PLUGIN_NAME"
mkdir -p "$PLUGIN_DIR/$PLATFORM_DIR"

# Copy plugin file
echo "Installing plugin to $PLUGIN_DIR/$PLATFORM_DIR/"
cp "build/$PLATFORM_DIR/$PLUGIN_NAME.xpl" "$PLUGIN_DIR/$PLATFORM_DIR/"

# Copy README if it exists
if [ -f "README.md" ]; then
    cp "README.md" "$PLUGIN_DIR/"
fi

echo "Installation complete!"
echo "Plugin installed to: $PLUGIN_DIR/$PLATFORM_DIR/$PLUGIN_NAME.xpl"
echo ""
echo "To use the plugin:"
echo "1. Start X-Plane"
echo "2. Move your mouse to the upper right corner of the screen"
echo "3. Click the sound icon that appears"
echo "4. Use mouse wheel on the knobs to adjust volume"

