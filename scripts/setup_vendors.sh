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
        cmake -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
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

        ./configure --prefix="$VENDOR_DIR/liburing/install"
        make -j$(nproc)
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

# On macOS, use Homebrew's nghttp2 if available
if [ "$OS" = "Darwin" ] && command -v brew &> /dev/null && brew list libnghttp2 &> /dev/null; then
    echo "✓ Using Homebrew's libnghttp2"
    echo "  Location: $(brew --prefix libnghttp2)"
# On Windows with vcpkg, use vcpkg's nghttp2
elif [ "$OS" = "Windows" ] && [ -n "$VCPKG_ROOT" ] && [ -d "$VCPKG_ROOT/installed/x64-windows" ]; then
    echo "✓ Using vcpkg's nghttp2"
    echo "  Location: $VCPKG_ROOT/installed/x64-windows"
# On Windows with MSYS2, use the MSYS2 package
elif [ "$OS" = "Windows" ] && [ -d "/mingw64/include/nghttp2" ]; then
    echo "✓ Using MSYS2's nghttp2"
    echo "  Location: /mingw64"
# On Windows without vcpkg or MSYS2, skip
elif [ "$OS" = "Windows" ]; then
    echo "⊘ nghttp2 on Windows requires vcpkg or MSYS2"
    echo "  vcpkg: vcpkg install nghttp2:x64-windows"
    echo "  MSYS2: pacman -S mingw-w64-x86_64-nghttp2"
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
if [ "$OS" = "Darwin" ] && command -v brew &> /dev/null && brew list libnghttp2 &> /dev/null; then
    echo "  ✓ nghttp2:    $(brew --prefix libnghttp2) (Homebrew)"
elif [ "$OS" = "Windows" ] && [ -n "$VCPKG_ROOT" ] && [ -d "$VCPKG_ROOT/installed/x64-windows" ]; then
    echo "  ✓ nghttp2:    $VCPKG_ROOT/installed/x64-windows (vcpkg)"
elif [ "$OS" = "Windows" ] && [ -d "/mingw64/include/nghttp2" ]; then
    echo "  ✓ nghttp2:    /mingw64 (MSYS2)"
elif [ "$OS" = "Windows" ]; then
    echo "  ⊘ nghttp2:    Install via vcpkg or MSYS2"
else
    echo "  ✓ nghttp2:    $VENDOR_DIR/nghttp2/install"
fi
echo ""
echo "You can now run: make build"
echo ""
