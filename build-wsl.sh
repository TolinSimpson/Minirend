#!/bin/bash
#
# build-wsl.sh - Build Minrend using WSL with proper Linux headers
#
# Usage from PowerShell:
#   wsl bash ./build-wsl.sh
#
set -e

echo "========================================"
echo " Minrend WSL Build"
echo "========================================"
echo ""

# Ensure we're in the project directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "==> Project directory: $SCRIPT_DIR"

# Check for required packages
PACKAGES_NEEDED=""
for pkg in build-essential libx11-dev libxcursor-dev libxrandr-dev libxi-dev libgl-dev; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        PACKAGES_NEEDED="$PACKAGES_NEEDED $pkg"
    fi
done

if [ -n "$PACKAGES_NEEDED" ]; then
    echo "==> Installing required packages:$PACKAGES_NEEDED"
    sudo apt update
    sudo apt install -y $PACKAGES_NEEDED
fi

# Create symlinks for Linux headers if needed
SHIMS_DIR="$SCRIPT_DIR/src/platform/shims"
if [ ! -L "$SHIMS_DIR/X11" ] || [ ! -e "$SHIMS_DIR/X11" ]; then
    echo "==> Creating X11 header symlink..."
    rm -rf "$SHIMS_DIR/X11"  # Remove any existing directory
    ln -sf /usr/include/X11 "$SHIMS_DIR/X11"
fi

if [ ! -L "$SHIMS_DIR/GL" ] || [ ! -e "$SHIMS_DIR/GL" ]; then
    echo "==> Creating GL header symlink..."
    rm -rf "$SHIMS_DIR/GL"  # Remove any existing directory
    ln -sf /usr/include/GL "$SHIMS_DIR/GL"
fi

# KHR headers (OpenGL Khronos headers)
if [ -d /usr/include/KHR ] && ([ ! -L "$SHIMS_DIR/KHR" ] || [ ! -e "$SHIMS_DIR/KHR" ]); then
    echo "==> Creating KHR header symlink..."
    rm -rf "$SHIMS_DIR/KHR"
    ln -sf /usr/include/KHR "$SHIMS_DIR/KHR"
fi

echo "==> Header symlinks ready"
ls -la "$SHIMS_DIR/" | grep "^l"

# Run the main build script
echo ""
echo "==> Running build..."
./build_scripts/build

echo ""
echo "========================================"
echo " Build Complete!"
echo "========================================"

# Check if executable was created
if [ -f "minrend" ]; then
    echo ""
    echo "Output: $(pwd)/minrend"
    file minrend
    ls -lh minrend
fi

if [ -f "dist/minrend" ]; then
    echo ""
    echo "Distribution: $(pwd)/dist/minrend"
    ls -lh dist/minrend
fi

