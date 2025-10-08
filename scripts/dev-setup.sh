#!/bin/bash
#
# dev-setup.sh - Complete development environment setup using uv
#

set -e

echo "================================"
echo "httpmorph - Development Setup"
echo "================================"
echo ""

# Check if uv is installed
if ! command -v uv &> /dev/null; then
    echo "❌ uv is not installed"
    echo ""
    echo "Install uv with:"
    echo "  curl -LsSf https://astral.sh/uv/install.sh | sh"
    echo ""
    echo "Or on macOS:"
    echo "  brew install uv"
    echo ""
    exit 1
fi

echo "✓ uv found: $(uv --version)"
echo ""

# Sync Python dependencies
echo "==> Syncing Python dependencies with uv..."
uv sync --extra dev --extra build

echo ""
echo "==> Setting up vendor dependencies..."
./scripts/setup_vendors.sh

echo ""
echo "==> Building C extensions..."
uv run python setup.py build_ext --inplace

echo ""
echo "================================"
echo "Development Environment Ready!"
echo "================================"
echo ""
echo "Quick commands:"
echo "  make test        - Run tests"
echo "  make lint        - Check code quality"
echo "  make format      - Format code"
echo "  make build       - Rebuild C extensions"
echo ""
echo "Or use uv directly:"
echo "  uv run pytest tests/ -v"
echo "  uv run ruff check src/"
echo "  uv run ruff format src/"
echo ""
