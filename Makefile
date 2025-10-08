.PHONY: help setup build install test clean benchmark docs lint format sync

help:
	@echo "httpmorph - Development commands"
	@echo ""
	@echo "Setup:"
	@echo "  make sync        - Sync dependencies with uv"
	@echo "  make setup       - Setup vendor dependencies (llhttp, BoringSSL, liburing)"
	@echo "  make build       - Build C extensions"
	@echo "  make install     - Install in development mode"
	@echo ""
	@echo "Development:"
	@echo "  make test        - Run tests"
	@echo "  make benchmark   - Run benchmarks"
	@echo "  make lint        - Run linters (ruff, mypy)"
	@echo "  make format      - Format code (ruff)"
	@echo ""
	@echo "Cleanup:"
	@echo "  make clean       - Remove build artifacts"
	@echo "  make clean-all   - Remove build artifacts and vendor dependencies"

sync:
	@echo "Syncing dependencies with uv..."
	uv sync --extra dev --extra build

setup:
	@echo "Setting up vendor dependencies..."
	./scripts/setup_vendors.sh

build:
	@echo "Building C extensions..."
	uv run python setup.py build_ext --inplace

install: sync setup
	@echo "Installing in development mode..."
	uv pip install -e ".[dev,build]"

test:
	@echo "Running tests..."
	uv run pytest tests/ -v

test-verbose:
	@echo "Running tests with verbose output..."
	uv run pytest tests/ -vv -s

benchmark:
	@echo "Running benchmarks..."
	uv run pytest benchmarks/ -v --benchmark-only

lint:
	@echo "Running linters..."
	uv run ruff check src/ tests/
	uv run mypy src/

format:
	@echo "Formatting code with ruff..."
	uv run ruff format src/ tests/ benchmarks/
	uv run ruff check --fix src/ tests/

clean:
	@echo "Cleaning build artifacts..."
	rm -rf build/
	rm -rf dist/
	rm -rf *.egg-info
	rm -rf src/bindings/*.c
	rm -rf src/**/*.so
	rm -rf src/**/*.html
	find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
	find . -type f -name "*.pyc" -delete

clean-all: clean
	@echo "Cleaning vendor dependencies..."
	rm -rf vendor/

# Quick development workflow
dev: clean build test

# Full rebuild
rebuild: clean setup build

# Check if everything is working
check: lint test
	@echo "All checks passed!"
