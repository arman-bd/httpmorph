#!/bin/bash
#
# test_macos_build_local.sh - Test macOS wheel build locally
#
# This script mirrors the macOS build pipeline from .github/workflows/release.yml
# but runs locally for testing before pushing to CI.
#

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

echo "================================"
echo "Local macOS Build Test"
echo "================================"
echo ""
echo "This script tests the macOS wheel build pipeline locally."
echo ""

# Parse arguments
VERBOSE=false
CLEAN=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --clean           Clean build artifacts before building"
            echo "  --verbose, -v     Show verbose output"
            echo "  --help, -h        Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                # Build wheel for current Python"
            echo "  $0 --clean        # Clean and build"
            echo "  $0 --verbose      # Show detailed build output"
            echo ""
            echo "Note: Uses the currently active python3 version"
            echo ""
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

PYTHON_VERSION=$(python3 --version)

echo "Configuration:"
echo "  Platform:     macOS $(sw_vers -productVersion)"
echo "  Architecture: $(uname -m)"
echo "  Python:       $PYTHON_VERSION"
echo "  Output Dir:   $ROOT_DIR/wheelhouse"
echo ""

# Create wheelhouse directory
mkdir -p "$ROOT_DIR/wheelhouse"

# Clean if requested
if $CLEAN; then
    echo "🧹 Cleaning build artifacts..."
    rm -rf "$ROOT_DIR/wheelhouse"/*.whl
    rm -rf "$ROOT_DIR/build"
    rm -rf "$ROOT_DIR/dist"
    rm -rf "$ROOT_DIR"/*.egg-info
    rm -rf "$ROOT_DIR/src"/*.egg-info
    echo "✅ Clean complete"
    echo ""
fi

# Check for required tools
echo "🔍 Checking prerequisites..."

# Check for Homebrew
if ! command -v brew &> /dev/null; then
    echo "❌ Error: Homebrew is not installed."
    echo "   Install it from: https://brew.sh"
    exit 1
fi
echo "  ✓ Homebrew found"

# Check for Python 3
if ! command -v python3 &> /dev/null; then
    echo "❌ Error: Python 3 is not installed."
    exit 1
fi
echo "  ✓ Python found: $PYTHON_VERSION"

# Check for build tool
if ! python3 -m pip list 2>/dev/null | grep -q "^build "; then
    echo "⚠️  build tool not found, installing..."
    python3 -m pip install build --user -q
fi
echo "  ✓ build tool found"

echo ""

# Install build dependencies (mirrors the before-all step)
echo "📦 Installing build dependencies..."
BREW_PACKAGES="cmake ninja go"
for pkg in $BREW_PACKAGES; do
    if brew list "$pkg" &>/dev/null; then
        echo "  ✓ $pkg already installed"
    else
        echo "  Installing $pkg..."
        if $VERBOSE; then
            brew install "$pkg"
        else
            brew install "$pkg" >/dev/null 2>&1
        fi
    fi
done
echo "✅ Dependencies installed"
echo ""

# Setup vendor dependencies (mirrors the before-build step)
echo "🔨 Building vendor dependencies..."
echo "  This may take several minutes (BoringSSL, nghttp2)..."
if $VERBOSE; then
    bash "$ROOT_DIR/scripts/setup_vendors.sh"
else
    bash "$ROOT_DIR/scripts/setup_vendors.sh" 2>&1 | grep -E "(===|✓|✅|⊘|ERROR|WARNING)" || true
fi
echo ""

# Build wheel using python -m build
echo "🔨 Building wheel..."
echo ""

cd "$ROOT_DIR"

# Clean previous builds
rm -rf build/ dist/

if $VERBOSE; then
    python3 -m build --wheel
else
    python3 -m build --wheel 2>&1 | grep -E "(Successfully|ERROR|error|failed|Building)" || true
fi

BUILD_EXIT_CODE=$?

# Copy wheel to wheelhouse if build succeeded
if [[ $BUILD_EXIT_CODE -eq 0 ]] && [[ -d dist ]]; then
    mkdir -p wheelhouse
    cp dist/*.whl wheelhouse/ 2>/dev/null || true
fi

echo ""
echo "════════════════════════════════════════════════════════════════"
if [[ $BUILD_EXIT_CODE -eq 0 ]]; then
    echo "✅ Build completed successfully!"
    echo "════════════════════════════════════════════════════════════════"
    echo ""
    echo "Wheels are available in: $ROOT_DIR/wheelhouse"
    echo ""
    ls -lh "$ROOT_DIR/wheelhouse"/*.whl 2>/dev/null || echo "No wheels found in wheelhouse"
    echo ""
    echo "To install locally:"
    echo "  pip install wheelhouse/httpmorph-*.whl"
    echo ""
    echo "To test the wheel:"
    echo "  python3 -c 'import httpmorph; print(httpmorph.version())'"
else
    echo "❌ Build failed with exit code $BUILD_EXIT_CODE"
    echo "════════════════════════════════════════════════════════════════"
    exit $BUILD_EXIT_CODE
fi
