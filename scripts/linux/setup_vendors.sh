#!/bin/bash
#
# setup_vendors_linux.sh - Build vendor dependencies for Linux
#
# This script builds:
# - BoringSSL (Google's TLS library)
# - liburing (io_uring library for high-performance I/O)
# - nghttp2 (HTTP/2 C library)
#

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
VENDOR_DIR="$ROOT_DIR/vendor"

echo "================================"
echo "httpmorph - Linux Vendor Setup"
echo "================================"
echo ""

# Create vendor directory
mkdir -p "$VENDOR_DIR"
cd "$VENDOR_DIR"

#
# 1. BoringSSL
#
echo "==> Setting up BoringSSL..."

# Check if BoringSSL is already built with valid libraries
if [ -d "boringssl/build" ] && [ -f "boringssl/build/libssl.a" ] && [ -f "boringssl/build/libcrypto.a" ]; then
    echo "✓ BoringSSL already built (using cached build)"
else
    # Remove any incomplete or contaminated BoringSSL directory
    if [ -d "boringssl" ]; then
        echo "Removing existing BoringSSL directory to ensure clean build..."
        rm -rf boringssl
    fi

    echo "Cloning BoringSSL..."
    git clone --depth 1 https://boringssl.googlesource.com/boringssl

    cd boringssl
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

    # Linux typically uses GCC - disable false positive warning
    CMAKE_C_FLAGS="-Wno-maybe-uninitialized"
    CMAKE_CXX_FLAGS="-Wno-maybe-uninitialized"

    # Force CMake to regenerate all platform detection
    # Unset any environment variables that might confuse platform detection
    unset CMAKE_OSX_ARCHITECTURES
    unset CMAKE_OSX_DEPLOYMENT_TARGET
    unset CMAKE_OSX_SYSROOT
    unset APPLE

    # Workaround: Disable assembly optimizations to avoid platform detection issues
    # BoringSSL's CMake is incorrectly trying to use Apple assembly files on Linux
    # Using -DOPENSSL_NO_ASM=1 forces pure C implementations instead
    echo "Building BoringSSL with assembly disabled to avoid platform detection issues..."
    echo "(This may take 5-10 minutes on first build, but will be cached for subsequent builds)"

    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
          -DCMAKE_C_FLAGS="$CMAKE_C_FLAGS" \
          -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" \
          -DOPENSSL_NO_ASM=1 \
          ..
    make -j$(nproc)

    echo "✓ BoringSSL built successfully"

    # Clean up .git directory to save cache space
    cd "$VENDOR_DIR/boringssl"
    if [ -d ".git" ]; then
        echo "Cleaning up .git directory to save cache space..."
        rm -rf .git
    fi
fi

cd "$VENDOR_DIR"

#
# 2. liburing
#
echo ""
echo "==> Setting up liburing..."

if [ ! -d "liburing" ]; then
    echo "Cloning liburing..."
    git clone --depth 1 https://github.com/axboe/liburing.git
fi

cd liburing

if [ ! -f "src/liburing.a" ]; then
    echo "Building liburing..."

    # Build only the library, skip tests to avoid header conflicts
    ./configure --prefix="$VENDOR_DIR/liburing/install"
    make -C src -j$(nproc)
    make install

    echo "✓ liburing built successfully"

    # Clean up .git directory to save cache space
    cd "$VENDOR_DIR/liburing"
    if [ -d ".git" ]; then
        echo "Cleaning up .git directory to save cache space..."
        rm -rf .git
    fi
else
    echo "✓ liburing already built"
fi

cd "$VENDOR_DIR"

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

# Check if nghttp2 is properly installed (not just built)
if [ ! -f "install/lib/libnghttp2.a" ]; then
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

    make -j$(nproc)
    make install

    echo "✓ nghttp2 built and installed successfully"
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
echo "Linux Vendor Setup Complete!"
echo "================================"
echo ""
echo "Built libraries:"
echo "  ✓ BoringSSL:  $VENDOR_DIR/boringssl/build"
echo "  ✓ liburing:   $VENDOR_DIR/liburing/install"
echo "  ✓ nghttp2:    $VENDOR_DIR/nghttp2/install"
echo ""
echo "Library verification:"

# Verify BoringSSL
if [ -f "$VENDOR_DIR/boringssl/build/libssl.a" ]; then
    echo "  ✓ BoringSSL libssl.a found"
else
    echo "  ✗ BoringSSL libssl.a NOT FOUND"
fi

if [ -f "$VENDOR_DIR/boringssl/build/libcrypto.a" ]; then
    echo "  ✓ BoringSSL libcrypto.a found"
else
    echo "  ✗ BoringSSL libcrypto.a NOT FOUND"
fi

# Verify liburing
if [ -f "$VENDOR_DIR/liburing/src/liburing.a" ]; then
    echo "  ✓ liburing.a found"
else
    echo "  ✗ liburing.a NOT FOUND"
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
