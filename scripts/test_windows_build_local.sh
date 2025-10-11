#!/bin/bash
# Bash script to build and test httpmorph wheels on Windows
# Mimics the GitHub Actions release pipeline (.github/workflows/release.yml)
# Works with Git Bash, MSYS2, or WSL on Windows

set -e

SKIP_TESTS=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-tests)
            SKIP_TESTS=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--skip-tests]"
            exit 1
            ;;
    esac
done

echo "========================================"
echo "httpmorph - Windows Wheel Builder"
echo "========================================"
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Step 1: Setup vendors
echo "==> Step 1: Setting up vendor dependencies..."

# Check for required tools
echo "    Checking dependencies..."

# Add common tool paths
export PATH="/c/Program Files/CMake/bin:/c/Program Files/Go/bin:$PATH"

if ! command -v cmake &> /dev/null; then
    echo "[FAIL] cmake not found. Install with: choco install cmake"
    exit 1
fi
echo "    [OK] cmake found"

if ! command -v go &> /dev/null; then
    echo "[FAIL] go not found. Install with: choco install golang"
    exit 1
fi
echo "    [OK] go found"

# Check vcpkg
if [ ! -f "/c/vcpkg/vcpkg.exe" ]; then
    echo "[FAIL] vcpkg not found at C:\\vcpkg"
    echo "       Install vcpkg and nghttp2/zlib:"
    echo "       git clone https://github.com/Microsoft/vcpkg.git C:\\vcpkg"
    echo "       C:\\vcpkg\\bootstrap-vcpkg.bat"
    echo "       C:\\vcpkg\\vcpkg install nghttp2:x64-windows zlib:x64-windows"
    exit 1
fi
echo "    [OK] vcpkg found"

# Build BoringSSL
echo ""
echo "    Building BoringSSL..."
cd "$PROJECT_ROOT"
bash scripts/setup_vendors.sh
echo "    [OK] Vendors built successfully"

# Step 2: Build wheel
echo ""
echo "==> Step 2: Building wheel..."

export VCPKG_ROOT="C:/vcpkg"

cd "$PROJECT_ROOT"

# Find Python
if [ -f "/c/Python311/python.exe" ]; then
    PYTHON="/c/Python311/python.exe"
elif command -v python &> /dev/null; then
    PYTHON="python"
else
    echo "[FAIL] Python not found"
    exit 1
fi

# Build wheel
"$PYTHON" setup.py bdist_wheel

# Find the built wheel
WHEEL=$(ls -t "$PROJECT_ROOT/dist"/*.whl 2>/dev/null | head -1)

if [ -z "$WHEEL" ]; then
    echo "[FAIL] No wheel found in dist/"
    exit 1
fi

echo "    [OK] Wheel built: $(basename "$WHEEL")"

# Step 3: Test wheel
if [ "$SKIP_TESTS" = false ]; then
    echo ""
    echo "==> Step 3: Testing wheel..."

    # Install wheel
    echo "    Installing wheel..."
    "$PYTHON" -m pip install --force-reinstall "$WHEEL"

    # Run tests
    echo ""
    echo "    Running tests..."
    "$PYTHON" "$PROJECT_ROOT/scripts/test_local_build.py"

    echo ""
    echo "    [OK] All tests passed!"
fi

# Summary
echo ""
echo "========================================"
echo "BUILD COMPLETE"
echo "========================================"
echo ""
echo "Wheel: $WHEEL"
echo "Size:  $(ls -lh "$WHEEL" | awk '{print $5}')"
echo ""
echo "To install:"
echo "  pip install $WHEEL"
echo ""

exit 0
