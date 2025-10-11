#!/bin/bash
#
# test_linux_build_local.sh - Test Linux wheel build locally using Docker
#
# This script mirrors the Linux build pipeline from .github/workflows/release.yml
# but runs locally in Docker for testing before pushing to CI.
#

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

echo "================================"
echo "Local Linux Build Test"
echo "================================"
echo ""
echo "This script tests the Linux wheel build pipeline locally"
echo "using the same manylinux Docker images as GitHub Actions."
echo ""

# Configuration
PYTHON_VERSIONS=("cp39" "cp310" "cp311" "cp312")
ARCHITECTURES=("x86_64" "i686")
MANYLINUX_VERSION="2014"
IMAGE_TAG="2024.11.16-1"

# Parse arguments
BUILD_ARCH="x86_64"  # Default to x86_64
SINGLE_PYTHON=""
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --arch)
            BUILD_ARCH="$2"
            shift 2
            ;;
        --python)
            SINGLE_PYTHON="$2"
            shift 2
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --arch ARCH       Build for architecture (x86_64 or i686, default: x86_64)"
            echo "  --python VERSION  Build only for specific Python version (e.g., cp311)"
            echo "  --verbose, -v     Show verbose output"
            echo "  --help, -h        Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                    # Build for x86_64, all Python versions"
            echo "  $0 --arch i686        # Build for 32-bit, all Python versions"
            echo "  $0 --python cp311     # Build only Python 3.11 for x86_64"
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

# Validate architecture
if [[ "$BUILD_ARCH" != "x86_64" && "$BUILD_ARCH" != "i686" ]]; then
    echo "Error: Invalid architecture '$BUILD_ARCH'. Must be x86_64 or i686."
    exit 1
fi

# Docker image
DOCKER_IMAGE="quay.io/pypa/manylinux${MANYLINUX_VERSION}_${BUILD_ARCH}:${IMAGE_TAG}"

echo "Configuration:"
echo "  Architecture: $BUILD_ARCH"
echo "  Docker Image: $DOCKER_IMAGE"
echo "  Output Dir:   $ROOT_DIR/wheelhouse"
if [[ -n "$SINGLE_PYTHON" ]]; then
    echo "  Python:       $SINGLE_PYTHON only"
else
    echo "  Python:       ${PYTHON_VERSIONS[*]}"
fi
echo ""

# Create wheelhouse directory
mkdir -p "$ROOT_DIR/wheelhouse"

# Check if Docker is running
if ! docker info &>/dev/null; then
    echo "Error: Docker is not running. Please start Docker and try again."
    exit 1
fi

# Pull Docker image
echo "📥 Pulling Docker image..."
if $VERBOSE; then
    docker pull "$DOCKER_IMAGE"
else
    docker pull "$DOCKER_IMAGE" 2>&1 | grep -E "(Pulling|Status|Digest)" || true
fi
echo ""

# Build wheels in Docker
echo "🐳 Starting Docker container..."
echo ""

# Set platform flag for Docker
if [[ "$BUILD_ARCH" == "i686" ]]; then
    PLATFORM_FLAG="--platform=linux/386"
else
    PLATFORM_FLAG=""
fi

# Create a temporary container name
CONTAINER_NAME="httpmorph-build-test-$$"

# Run Docker container with the build script
docker run --rm \
    $PLATFORM_FLAG \
    --name "$CONTAINER_NAME" \
    -v "$ROOT_DIR:/project" \
    -w /project \
    -e BUILD_ARCH="$BUILD_ARCH" \
    -e SINGLE_PYTHON="$SINGLE_PYTHON" \
    -e VERBOSE="$VERBOSE" \
    "$DOCKER_IMAGE" \
    /bin/bash -c '
set -e

echo "════════════════════════════════════════════════════════════════"
echo "Running inside Docker container"
echo "════════════════════════════════════════════════════════════════"
echo ""
echo "Environment:"
echo "  Platform:     $(uname -m)"
echo "  Architecture: $BUILD_ARCH"
echo "  Verbose:      $VERBOSE"
echo ""

# Fix CentOS mirrors (mirrors the before-all steps)
echo "🔧 Fixing CentOS mirrors..."
sed -i "s/mirrorlist/#mirrorlist/g" /etc/yum.repos.d/CentOS-* 2>/dev/null || true
sed -i "s|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g" /etc/yum.repos.d/CentOS-* 2>/dev/null || true
echo ""

# Install build dependencies (mirrors the before-all steps)
echo "📦 Installing build dependencies..."
if [[ "$VERBOSE" == "true" ]]; then
    yum install -y cmake openssl-devel zlib-devel pkgconfig autoconf automake libtool golang || \
    yum install -y openssl-devel zlib-devel pkgconfig autoconf automake libtool golang
else
    yum install -y cmake openssl-devel zlib-devel pkgconfig autoconf automake libtool golang >/dev/null 2>&1 || \
    yum install -y openssl-devel zlib-devel pkgconfig autoconf automake libtool golang >/dev/null 2>&1
fi
echo "✅ Dependencies installed"
echo ""

# Setup vendor dependencies (mirrors the before-build step)
echo "🔨 Building vendor dependencies..."
echo "  This may take several minutes (BoringSSL, nghttp2)..."
if [[ "$VERBOSE" == "true" ]]; then
    bash scripts/setup_vendors.sh
else
    bash scripts/setup_vendors.sh 2>&1 | grep -E "(===|✓|✅|⊘|ERROR|WARNING)" || true
fi
echo ""

# Determine which Python versions to build
if [[ -n "$SINGLE_PYTHON" ]]; then
    PYTHON_BINS=("/opt/python/${SINGLE_PYTHON}-${SINGLE_PYTHON}/bin")
else
    PYTHON_BINS=(
        /opt/python/cp39-cp39/bin
        /opt/python/cp310-cp310/bin
        /opt/python/cp311-cp311/bin
        /opt/python/cp312-cp312/bin
    )
fi

# Build wheels for each Python version
for PYBIN in "${PYTHON_BINS[@]}"; do
    if [[ ! -d "$PYBIN" ]]; then
        echo "⚠️  Python interpreter not found: $PYBIN (skipping)"
        continue
    fi

    PYTHON_VERSION=$("$PYBIN/python" --version 2>&1 | cut -d" " -f2)
    echo "════════════════════════════════════════════════════════════════"
    echo "🐍 Building wheel for Python $PYTHON_VERSION"
    echo "════════════════════════════════════════════════════════════════"
    echo ""

    # Install build dependencies
    echo "📦 Installing Python build dependencies..."
    if [[ "$VERBOSE" == "true" ]]; then
        "$PYBIN/pip" install --upgrade pip setuptools wheel Cython
    else
        "$PYBIN/pip" install --upgrade pip setuptools wheel Cython >/dev/null 2>&1
    fi
    echo ""

    # Build wheel
    echo "🔨 Building wheel..."
    if [[ "$VERBOSE" == "true" ]]; then
        "$PYBIN/pip" wheel /project --no-deps -w /project/wheelhouse
    else
        "$PYBIN/pip" wheel /project --no-deps -w /project/wheelhouse 2>&1 | \
            grep -E "(Processing|Building|Successfully built|ERROR)" || true
    fi
    echo ""

    # Repair wheel with auditwheel
    echo "🔧 Repairing wheel with auditwheel..."
    # Extract Python version tag (e.g., cp311-cp311 from /opt/python/cp311-cp311/bin)
    PYTHON_TAG=$(basename $(dirname "$PYBIN"))
    WHEEL_FILE=$(ls -t /project/wheelhouse/httpmorph-*-${PYTHON_TAG}-linux_*.whl 2>/dev/null | head -1)

    if [[ -n "$WHEEL_FILE" ]]; then
        if [[ "$VERBOSE" == "true" ]]; then
            auditwheel repair "$WHEEL_FILE" -w /project/wheelhouse
        else
            auditwheel repair "$WHEEL_FILE" -w /project/wheelhouse 2>&1 | \
                grep -E "(Repairing|Previous|New|Fixed-up)" || true
        fi

        # Remove the unrepaired wheel
        rm -f "$WHEEL_FILE"
        echo "✅ Wheel repaired successfully"
    else
        echo "❌ Error: Wheel file not found"
        exit 1
    fi
    echo ""
done

echo "════════════════════════════════════════════════════════════════"
echo "✅ All wheels built successfully!"
echo "════════════════════════════════════════════════════════════════"
echo ""
echo "Built wheels:"
ls -lh /project/wheelhouse/*.whl 2>/dev/null || echo "No wheels found"
'

BUILD_EXIT_CODE=$?

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
    echo "  python -c 'import httpmorph; print(httpmorph.version())'"
else
    echo "❌ Build failed with exit code $BUILD_EXIT_CODE"
    echo "════════════════════════════════════════════════════════════════"
    exit $BUILD_EXIT_CODE
fi
