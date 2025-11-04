#!/bin/bash
#
# run-benchmark.sh - Run benchmarks in Docker container
#
# Usage:
#   ./docker/run-benchmark.sh                    # Run all libraries with default settings
#   ./docker/run-benchmark.sh httpmorph          # Run only httpmorph
#   ./docker/run-benchmark.sh httpmorph 3 1      # Run httpmorph with 3 requests, 1 warmup
#

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$ROOT_DIR"

# Default values
LIBRARIES="${1:-all}"
SEQUENTIAL="${2:-10}"
WARMUP="${3:-3}"

echo "================================"
echo "httpmorph Benchmark (Docker)"
echo "================================"
echo ""
echo "Libraries: $LIBRARIES"
echo "Sequential: $SEQUENTIAL requests"
echo "Warmup: $WARMUP requests"
echo ""

# Build the Docker image
echo "Building Docker image..."
docker build -f docker/Dockerfile.benchmark -t httpmorph-benchmark .

# Run the benchmark
echo ""
echo "Running benchmark..."
echo ""

if [ "$LIBRARIES" = "all" ]; then
    docker run --rm \
        -v "$ROOT_DIR/benchmarks/results:/workspace/benchmarks/results" \
        -v "$ROOT_DIR/.env:/workspace/.env:ro" \
        httpmorph-benchmark \
        python benchmarks/benchmark.py --sequential "$SEQUENTIAL" --warmup "$WARMUP"
else
    docker run --rm \
        -v "$ROOT_DIR/benchmarks/results:/workspace/benchmarks/results" \
        -v "$ROOT_DIR/.env:/workspace/.env:ro" \
        httpmorph-benchmark \
        python benchmarks/benchmark.py --libraries "$LIBRARIES" --sequential "$SEQUENTIAL" --warmup "$WARMUP"
fi

echo ""
echo "================================"
echo "Benchmark Complete!"
echo "================================"
echo ""
echo "Results saved to: benchmarks/results/linux/"
echo ""
