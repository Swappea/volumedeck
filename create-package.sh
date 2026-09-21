#!/bin/bash

# Create installable X-Plane plugin package from local builds

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

echo "======================================"
echo "Creating X-Plane Plugin Package"
echo "======================================"

# Check if we have any builds
if [ ! -d "build/lin_x64" ] && [ ! -d "build/win_x64" ] && [ ! -d "build/mac_x64" ]; then
    echo "Error: No builds found. Run ./build.sh first"
    exit 1
fi

# Get version
VERSION=$(sed -n 's/^#define SOFTWARE_VERSION "\(.*\)"/\1/p' src/VolumeDeck.h | head -1)

# Create package structure
PACKAGE_DIR="dist/VolumeDeck"
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR"
mkdir -p dist

# Copy platform binaries from build directory
[ -d "build/lin_x64" ] && cp -r build/lin_x64 "$PACKAGE_DIR/" && echo "✓ Added Linux build"
[ -d "build/win_x64" ] && cp -r build/win_x64 "$PACKAGE_DIR/" && echo "✓ Added Windows build"
[ -d "build/mac_x64" ] && cp -r build/mac_x64 "$PACKAGE_DIR/" && echo "✓ Added macOS build"

# Copy documentation
cp README.md "$PACKAGE_DIR/" 2>/dev/null && echo "✓ Added README" || true

# Create package
cd dist

if command -v zip &> /dev/null; then
    PKG_NAME="VolumeDeck-v${VERSION}-XPlane.zip"
    zip -r "$PKG_NAME" VolumeDeck/
    echo "✓ Created ZIP package"
else
    PKG_NAME="VolumeDeck-v${VERSION}-XPlane.tar.gz"
    tar -czf "$PKG_NAME" VolumeDeck/
    echo "✓ Created TAR.GZ package"
fi

cd ..

echo ""
echo "======================================"
echo "Package Created Successfully!"
echo "======================================"
echo ""
ls -lh "dist/$PKG_NAME"

echo ""
echo "Package contents:"
ls -lh dist/VolumeDeck/*/VolumeDeck.xpl 2>/dev/null || echo "  (No .xpl files found)"

echo ""
echo "======================================"
echo "Installation Instructions"
echo "======================================"
echo ""
echo "1. Extract the package:"
if command -v zip &> /dev/null; then
    echo "   unzip dist/VolumeDeck-v${VERSION}-XPlane.zip"
else
    echo "   tar -xzf dist/VolumeDeck-v${VERSION}-XPlane.tar.gz"
fi
echo ""
echo "2. Copy to X-Plane:"
echo "   cp -r VolumeDeck '/games/X-Plane 12/Resources/plugins/'"
echo ""
echo "Or extract directly into plugins directory"
echo ""

