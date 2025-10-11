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
case "$OS" in
    MINGW*|MSYS*|CYGWIN*)
        OS="Windows"
        ;;
    Darwin)
        OS="Darwin"
        ;;
    Linux)
        OS="Linux"
        ;;
esac
echo "Detected OS: $OS"
echo ""

#
# 1. BoringSSL
#
echo "==> Setting up BoringSSL..."

# Build BoringSSL on all platforms (including Windows)
if [ ! -d "boringssl" ]; then
    echo "Cloning BoringSSL..."
    git clone --depth 1 https://boringssl.googlesource.com/boringssl
fi

cd boringssl

# Check if already built (Windows uses .lib files instead of .a)
if [ "$OS" = "Windows" ]; then
    SSL_LIB_FILE="build/ssl/ssl.lib"
else
    SSL_LIB_FILE="build/ssl/libssl.a"
fi

if [ ! -f "$SSL_LIB_FILE" ]; then
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

    # Platform-specific CMake configuration
    if [ "$OS" = "Windows" ]; then
        # Use Visual Studio generator on Windows for proper .lib output
        cmake -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
              -DBUILD_SHARED_LIBS=OFF \
              ..
        cmake --build . --config Release
    else
        # Detect compiler (GCC vs Clang)
        # GCC needs -Wno-maybe-uninitialized, Clang doesn't recognize it
        if [ "$OS" = "Darwin" ]; then
            # macOS uses Clang - no special flags needed
            CMAKE_C_FLAGS=""
            CMAKE_CXX_FLAGS=""
        else
            # Linux typically uses GCC - disable false positive warning
            CMAKE_C_FLAGS="-Wno-maybe-uninitialized"
            CMAKE_CXX_FLAGS="-Wno-maybe-uninitialized"
        fi

        cmake -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
              -DCMAKE_C_FLAGS="$CMAKE_C_FLAGS" \
              -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" \
              ..
        make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    fi

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
# 2. liburing (Linux only)
#
if [ "$OS" = "Linux" ]; then
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
else
    echo ""
    echo "⊘ Skipping liburing (Linux only)"
fi

#
# 3. nghttp2
#
echo ""
echo "==> Setting up nghttp2..."

if [ "$OS" = "Windows" ]; then
    # Always build nghttp2 from source on Windows for compatibility
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

    # Check if already built (Windows uses .lib files)
    if [ ! -f "build/lib/Release/nghttp2.lib" ]; then
        echo "Building nghttp2 with CMake for Windows..."

        mkdir -p build
        cd build

        # Use CMake for Windows build
        cmake .. \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            -DBUILD_SHARED_LIBS=OFF \
            -DENABLE_LIB_ONLY=ON \
            -DENABLE_STATIC_LIB=ON

        cmake --build . --config Release

        echo "✓ nghttp2 built successfully"
    else
        echo "✓ nghttp2 already built"
    fi

    cd "$VENDOR_DIR"
else
    # Build from source on Linux or if Homebrew version not available
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

        make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
        make install

        echo "✓ nghttp2 built successfully"
    else
        echo "✓ nghttp2 already built"
    fi

    cd "$VENDOR_DIR"
fi

#
# 4. zlib (Windows only - Linux/macOS have system zlib)
#
if [ "$OS" = "Windows" ]; then
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
fi

# Clean up downloaded archives to save cache space
echo ""
echo "==> Cleaning up downloaded archives..."
rm -f "$VENDOR_DIR/nghttp2.tar.gz"
rm -f "$VENDOR_DIR/zlib.zip"
echo "✓ Cleanup complete"

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
if [ "$OS" = "Windows" ]; then
    echo "  ✓ nghttp2:    $VENDOR_DIR/nghttp2/build"
    echo "  ✓ zlib:       $VENDOR_DIR/zlib/build"
else
    echo "  ✓ nghttp2:    $VENDOR_DIR/nghttp2/install"
fi
echo ""
echo "You can now run: make build"
echo ""
