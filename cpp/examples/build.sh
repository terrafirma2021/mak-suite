#!/bin/bash

# MAKXD C++ Library Examples Build Script
# This script shows how to build applications using the MAKXD library

set -e

echo "=== MAKXD C++ Library Examples Build ==="
echo

# Build with CMake (recommended approach)
echo "Building examples with CMake..."
mkdir -p build && cd build
cmake ..
make -j$(nproc)

if [ -f "bin/basic_usage" ]; then
    echo "✓ Build successful!"
    echo
    echo "To run the examples:"
    echo "  ./build/bin/basic_usage    # Simple usage example"
    echo
    echo "To use this in your own project:"
    echo "1. Install the MAKXD library: cd .. && mkdir build && cd build && cmake .. && make && sudo make install"
    echo "2. Copy the CMakeLists.txt from this examples directory"
    echo "3. Use: find_package(makxd-cpp REQUIRED) and target_link_libraries(your_app PRIVATE makxd::makxd-cpp)"
else
    echo "✗ Build failed"
    echo "Make sure the MAKXD library is installed or available via CMAKE_PREFIX_PATH"
    exit 1
fi
