#!/bin/bash
#
# setup_vendors_windows.sh - Build vendor dependencies for Windows
#
# This script builds:
# - BoringSSL (Google's TLS library)
# - nghttp2 (HTTP/2 C library)
# - zlib (Compression library)
#

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
VENDOR_DIR="$ROOT_DIR/vendor"

echo "================================"
echo "httpmorph - Windows Vendor Setup"
echo "================================"
echo ""

# Create vendor directory
mkdir -p "$VENDOR_DIR"
cd "$VENDOR_DIR"

#
# 1. BoringSSL
#
echo "==> Setting up BoringSSL..."

if [ ! -d "boringssl" ]; then
    echo "Cloning BoringSSL..."
    git clone --depth 1 https://boringssl.googlesource.com/boringssl
fi

cd boringssl

# Check if already built
if [ ! -f "build/ssl/Release/ssl.lib" ] && [ ! -f "build/Release/ssl.lib" ]; then
    echo "Building BoringSSL..."

    # Check for required tools
    if ! command -v cmake &> /dev/null; then
        echo "ERROR: cmake not found. Please install cmake."
        exit 1
    fi

    if ! command -v go &> /dev/null; then
        echo "WARNING: go not found. BoringSSL will build without some tests."
    fi

    mkdir -p build
    cd build

    # Use Visual Studio generator on Windows for proper .lib output
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
          -DBUILD_SHARED_LIBS=OFF \
          ..
    cmake --build . --config Release

    echo "✓ BoringSSL built successfully"

    # Clean up .git directory to save cache space
    cd "$VENDOR_DIR/boringssl"
    if [ -d ".git" ]; then
        echo "Cleaning up .git directory to save cache space..."
        rm -rf .git
    fi
else
    echo "✓ BoringSSL already built"
fi

cd "$VENDOR_DIR"

#
# 2. nghttp2
#
echo ""
echo "==> Setting up nghttp2..."

if [ ! -d "nghttp2" ]; then
    echo "Downloading nghttp2..."

    NGHTTP2_VERSION="1.59.0"
    curl -L "https://github.com/nghttp2/nghttp2/releases/download/v${NGHTTP2_VERSION}/nghttp2-${NGHTTP2_VERSION}.tar.gz" \
         -o nghttp2.tar.gz

    tar xzf nghttp2.tar.gz
    mv "nghttp2-${NGHTTP2_VERSION}" nghttp2
    rm nghttp2.tar.gz
fi

cd nghttp2

# Check if already built
if [ ! -f "build/lib/Release/nghttp2.lib" ]; then
    echo "Building nghttp2 with CMake for Windows (static library only)..."

    mkdir -p build
    cd build

    # Build static library only (disable shared library)
    # NGHTTP2_STATICLIB will be defined when linking in setup.py
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DENABLE_LIB_ONLY=ON \
        -DENABLE_SHARED_LIB=OFF \
        -DENABLE_STATIC_LIB=ON

    cmake --build . --config Release

    echo "✓ nghttp2 built successfully (static library)"

    # Verify static library was built
    if [ -f "lib/Release/nghttp2.lib" ] && [ ! -f "lib/Release/nghttp2.dll" ]; then
        echo "  ✓ Static library confirmed: lib/Release/nghttp2.lib"
    elif [ -f "lib/Release/nghttp2.dll" ]; then
        echo "  ⚠ WARNING: DLL was built (should be static only)"
        ls -la lib/Release/
    fi
else
    echo "✓ nghttp2 already built"
fi

cd "$VENDOR_DIR"

#
# 3. zlib
#
echo ""
echo "==> Setting up zlib..."

if [ ! -d "zlib" ]; then
    echo "Downloading zlib..."
    ZLIB_VERSION="1.3.1"
    curl -L "https://github.com/madler/zlib/archive/refs/tags/v${ZLIB_VERSION}.tar.gz" \
         -o zlib.tar.gz

    tar xzf zlib.tar.gz
    mv "zlib-${ZLIB_VERSION}" zlib
    rm zlib.tar.gz
fi

cd zlib

# Check if already built
if [ ! -f "build/Release/zlibstatic.lib" ]; then
    echo "Building zlib with CMake for Windows..."

    mkdir -p build
    cd build

    # Use CMake for Windows build
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON

    cmake --build . --config Release

    echo "✓ zlib built successfully"
else
    echo "✓ zlib already built"
fi

cd "$VENDOR_DIR"

# Clean up downloaded archives to save cache space
echo ""
echo "==> Cleaning up downloaded archives..."
rm -f "$VENDOR_DIR"/*.tar.gz
echo "✓ Cleanup complete"

#
# Summary
#
echo ""
echo "================================"
echo "Windows Vendor Setup Complete!"
echo "================================"
echo ""
echo "Built libraries:"
echo "  ✓ BoringSSL:  $VENDOR_DIR/boringssl/build"
echo "  ✓ nghttp2:    $VENDOR_DIR/nghttp2/build/lib/Release"
echo "  ✓ zlib:       $VENDOR_DIR/zlib/build/Release"
echo ""
echo "Library verification:"

# Verify BoringSSL
if [ -f "$VENDOR_DIR/boringssl/build/ssl/Release/ssl.lib" ]; then
    echo "  ✓ BoringSSL ssl.lib found"
elif [ -f "$VENDOR_DIR/boringssl/build/Release/ssl.lib" ]; then
    echo "  ✓ BoringSSL ssl.lib found (alternate location)"
else
    echo "  ✗ BoringSSL ssl.lib NOT FOUND"
fi

# Verify nghttp2
if [ -f "$VENDOR_DIR/nghttp2/build/lib/Release/nghttp2.lib" ]; then
    echo "  ✓ nghttp2.lib found"
    # Check if it's static (no DLL should exist)
    if [ ! -f "$VENDOR_DIR/nghttp2/build/lib/Release/nghttp2.dll" ]; then
        echo "    ✓ Static library (no DLL)"
    else
        echo "    ⚠ WARNING: DLL also exists (should be static only)"
    fi
else
    echo "  ✗ nghttp2.lib NOT FOUND"
fi

# Verify zlib
if [ -f "$VENDOR_DIR/zlib/build/Release/zlibstatic.lib" ]; then
    echo "  ✓ zlibstatic.lib found"
else
    echo "  ✗ zlibstatic.lib NOT FOUND"
fi

echo ""
echo "You can now run: pip install -e ."
echo ""
