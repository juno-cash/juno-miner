#!/bin/bash

set -e

echo "Building Juno Cash RandomX Miner..."
echo

# Create build directory
mkdir -p build
cd build

# Run CMake
echo "Running CMake..."
cmake ..

# Build
echo "Building..."
make -j$(nproc)

echo
echo "Build complete!"
echo "Binary: ./build/juno-miner"
echo
echo "To install system-wide, run: sudo make install"
