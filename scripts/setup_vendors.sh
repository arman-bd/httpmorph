#!/bin/bash
#
# setup_vendors.sh - Download and build vendor dependencies
#
# This script sets up:
# - BoringSSL (Google's TLS library with custom fingerprinting)
# - liburing (Linux io_uring library for high-performance I/O)
# - nghttp2 (HTTP/2 C library)
#

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
VENDOR_DIR="$ROOT_DIR/vendor"

echo "================================"
echo "httpmorph - Vendor Setup"
echo "================================"
echo ""

# Create vendor directory
mkdir -p "$VENDOR_DIR"
cd "$VENDOR_DIR"

# Detect OS
OS="$(uname -s)"
echo "Detected OS: $OS"
echo ""

#
# 1. BoringSSL
#
echo "==> Setting up BoringSSL..."

if [ ! -d "boringssl" ]; then
    echo "Cloning BoringSSL..."
    git clone https://boringssl.googlesource.com/boringssl
fi

cd boringssl

# Check if already built
if [ ! -f "build/ssl/libssl.a" ]; then
    echo "Building BoringSSL..."

    # BoringSSL requires CMake and Go
    if ! command -v cmake &> /dev/null; then
        echo "ERROR: cmake not found. Please install cmake."
        exit 1
    fi

    if ! command -v go &> /dev/null; then
        echo "WARNING: go not found. BoringSSL will build without some tests."
    fi

    mkdir -p build
    cd build

    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
          ..

    make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

    echo "✓ BoringSSL built successfully"
else
    echo "✓ BoringSSL already built"
fi

cd "$VENDOR_DIR"

#
# 2. liburing (Linux only)
#
if [ "$OS" = "Linux" ]; then
    echo ""
    echo "==> Setting up liburing..."

    if [ ! -d "liburing" ]; then
        echo "Cloning liburing..."
        git clone https://github.com/axboe/liburing.git
    fi

    cd liburing

    if [ ! -f "src/liburing.a" ]; then
        echo "Building liburing..."

        ./configure --prefix="$VENDOR_DIR/liburing/install"
        make -j$(nproc)
        make install

        echo "✓ liburing built successfully"
    else
        echo "✓ liburing already built"
    fi

    cd "$VENDOR_DIR"
else
    echo ""
    echo "⊘ Skipping liburing (Linux only)"
fi

#
# 3. nghttp2
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

if [ ! -f "lib/.libs/libnghttp2.a" ]; then
    echo "Building nghttp2..."

    ./configure --prefix="$VENDOR_DIR/nghttp2/install" \
                --enable-lib-only \
                --enable-static \
                --disable-shared \
                --disable-examples \
                --disable-python-bindings

    make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    make install

    echo "✓ nghttp2 built successfully"
else
    echo "✓ nghttp2 already built"
fi

cd "$VENDOR_DIR"

#
# Summary
#
echo ""
echo "================================"
echo "Vendor Setup Complete!"
echo "================================"
echo ""
echo "Built libraries:"
echo "  ✓ BoringSSL:  $VENDOR_DIR/boringssl/build"
if [ "$OS" = "Linux" ]; then
    echo "  ✓ liburing:   $VENDOR_DIR/liburing/install"
fi
echo "  ✓ nghttp2:    $VENDOR_DIR/nghttp2/install"
echo ""
echo "You can now run: make build"
echo ""
