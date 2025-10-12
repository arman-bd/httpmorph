#!/bin/bash
#
# setup_vendors.sh - Build vendor dependencies for macOS/Darwin
#
# This script builds:
# - BoringSSL (Google's TLS library)
# - nghttp2 (HTTP/2 C library)
#

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
VENDOR_DIR="$ROOT_DIR/vendor"

echo "================================"
echo "httpmorph - macOS Vendor Setup"
echo "================================"
echo ""

# Create vendor directory
mkdir -p "$VENDOR_DIR"
cd "$VENDOR_DIR"

#
# 1. BoringSSL
#
echo "==> Setting up BoringSSL..."

# Always remove and re-clone BoringSSL to ensure clean state
# This prevents platform cross-contamination (e.g., Linux files on macOS runners)
if [ -d "boringssl" ]; then
    echo "Removing existing BoringSSL directory to ensure clean build..."
    rm -rf boringssl
fi

echo "Cloning BoringSSL..."
git clone --depth 1 https://boringssl.googlesource.com/boringssl

cd boringssl

# Check if already built
if [ ! -f "build/ssl/libssl.a" ] && [ ! -f "build/libssl.a" ]; then
    echo "Building BoringSSL..."

    # Check for required tools
    if ! command -v cmake &> /dev/null; then
        echo "ERROR: cmake not found. Please install cmake."
        exit 1
    fi

    if ! command -v go &> /dev/null; then
        echo "WARNING: go not found. BoringSSL will build without some tests."
    fi

    # Clean build directory if it exists
    if [ -d "build" ]; then
        echo "Cleaning previous build..."
        rm -rf build
    fi

    mkdir -p build
    cd build

    # Detect architecture
    ARCH=$(uname -m)

    # macOS uses Clang
    # On ARM64, we need to disable assembly optimizations since BoringSSL's pre-generated
    # x86_64 assembly files cause issues
    if [ "$ARCH" = "arm64" ]; then
        echo "Detected ARM64 architecture, disabling assembly optimizations..."
        cmake -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
              -DCMAKE_OSX_ARCHITECTURES=arm64 \
              -DOPENSSL_NO_ASM=1 \
              ..
    else
        echo "Detected x86_64 architecture, using assembly optimizations..."
        cmake -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
              ..
    fi

    make -j$(sysctl -n hw.ncpu)

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

# Build from source for wheel compatibility
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

if [ ! -f "lib/.libs/libnghttp2.a" ]; then
    echo "Building nghttp2..."

    # Clean up install directory if it exists as a file
    if [ -e "$VENDOR_DIR/nghttp2/install" ] && [ ! -d "$VENDOR_DIR/nghttp2/install" ]; then
        rm -f "$VENDOR_DIR/nghttp2/install"
    fi
    mkdir -p "$VENDOR_DIR/nghttp2/install"

    # Build with -fPIC for use in shared libraries
    CFLAGS="-fPIC" ./configure --prefix="$VENDOR_DIR/nghttp2/install" \
                --enable-lib-only \
                --enable-static \
                --disable-shared \
                --disable-examples \
                --disable-python-bindings

    make -j$(sysctl -n hw.ncpu)
    make install

    echo "✓ nghttp2 built successfully"
else
    echo "✓ nghttp2 already built"
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
echo "macOS Vendor Setup Complete!"
echo "================================"
echo ""
echo "Built libraries:"
echo "  ✓ BoringSSL:  $VENDOR_DIR/boringssl/build"
echo "  ✓ nghttp2:    $VENDOR_DIR/nghttp2/install"
echo ""
echo "Library verification:"

# Verify BoringSSL
if [ -f "$VENDOR_DIR/boringssl/build/ssl/libssl.a" ]; then
    echo "  ✓ BoringSSL libssl.a found (ssl/ subdirectory)"
elif [ -f "$VENDOR_DIR/boringssl/build/libssl.a" ]; then
    echo "  ✓ BoringSSL libssl.a found (build root)"
else
    echo "  ✗ BoringSSL libssl.a NOT FOUND"
fi

if [ -f "$VENDOR_DIR/boringssl/build/crypto/libcrypto.a" ]; then
    echo "  ✓ BoringSSL libcrypto.a found (crypto/ subdirectory)"
elif [ -f "$VENDOR_DIR/boringssl/build/libcrypto.a" ]; then
    echo "  ✓ BoringSSL libcrypto.a found (build root)"
else
    echo "  ✗ BoringSSL libcrypto.a NOT FOUND"
fi

# Verify nghttp2
if [ -f "$VENDOR_DIR/nghttp2/install/lib/libnghttp2.a" ]; then
    echo "  ✓ nghttp2 libnghttp2.a found"
else
    echo "  ✗ nghttp2 libnghttp2.a NOT FOUND"
fi

echo ""
echo "You can now run: pip install -e ."
echo ""
