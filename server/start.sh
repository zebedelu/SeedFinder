#!/usr/bin/env bash
# ============================================
# SeedFinder HTTP Bridge - Build and Start (Linux)
# ============================================

set -e

# cd to project root
cd "$(dirname "$0")/.."

SCRIPT_DIR="$(pwd)"
echo "Project root: $SCRIPT_DIR"

# Detect python (python3 preferred; allow "python" alias on Arch)
if command -v python3 >/dev/null 2>&1; then
    PYTHON=python3
elif command -v python >/dev/null 2>&1; then
    PYTHON=python
else
    echo "ERROR: Python 3 not found. Install with: sudo pacman -S python"
    exit 1
fi
echo "Using python: $($PYTHON --version)"

# Check deps
for cmd in cmake make gcc; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "ERROR: '$cmd' not found. Install with:"
        echo "  sudo pacman -S --needed base-devel cmake"
        exit 1
    fi
done
echo "Toolchain: gcc=$($(command -v gcc) --version | head -n1), cmake=$($(command -v cmake) --version | head -n1)"
echo

# [1/2] Build
echo "[1/2] Building seedfinder_lib..."
mkdir -p build_server
cd build_server
cmake -S ../core -B . >/dev/null
make -j"$(nproc)"
cd ..

if [ ! -f build_server/libseedfinder_lib.so ]; then
    echo
    echo "ERROR: Build failed — libseedfinder_lib.so not produced."
    exit 1
fi
echo "Build OK: build_server/libseedfinder_lib.so"
echo

# [2/2] Start
echo "[2/2] Starting SeedFinder server on port 7890..."
echo
echo "Keep this window open while using SeedFinder."
echo "The Minecraft mod will connect to http://localhost:7890"
echo
exec "$PYTHON" server/base/linux/index.py --so-path build_server/libseedfinder_lib.so
