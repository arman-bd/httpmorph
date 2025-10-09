.PHONY: help setup build install test clean benchmark docs lint format sync docker-build docker-test docker-shell

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
	@echo "  make test          - Run tests"
	@echo "  make benchmark     - Run benchmarks"
	@echo "  make lint          - Run linters (ruff, mypy)"
	@echo "  make format        - Format code (ruff)"
	@echo "  make check-windows - Quick Windows compatibility check (no Docker)"
	@echo ""
	@echo "Docker (CI Testing):"
	@echo "  make docker-build          - Build Docker test container (mimics CI)"
	@echo "  make docker-test           - Run tests in Docker"
	@echo "  make docker-shell          - Open shell in Docker for debugging"
	@echo "  make docker-windows-quick  - Quick Windows API check (works on ARM64/M1/M2)"
	@echo "  make docker-windows        - Full Windows test (requires x86_64 host)"
	@echo "  make docker-win-shell      - Open Windows test shell for debugging"
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

# Quick Windows compatibility check (no Docker needed)
check-windows:
	@echo "Running Windows compatibility check..."
	@./scripts/check_windows_compat.sh

# Docker targets for CI testing
docker-build:
	@echo "Building Docker test container (mimics GitHub Actions)..."
	docker build -f docker/Dockerfile.test -t httpmorph-test .

docker-test: docker-build
	@echo "Running tests in Docker container..."
	docker run --rm httpmorph-test pytest tests/ -v

docker-shell:
	@echo "Opening shell in Docker container for debugging..."
	docker run --rm -it -v $(PWD):/workspace httpmorph-test bash

# Windows compatibility testing with MinGW cross-compile
docker-windows:
	@echo "Testing Windows compatibility with MinGW-w64..."
	@echo "Note: This tests API compatibility, not full MSVC behavior."
	@echo "Note: Requires x86_64 host (may fail on ARM64/Apple Silicon due to vcpkg limitations)"
	docker-compose -f docker/docker-compose.windows-test.yml up windows-mingw

# Quick Windows syntax check (works on ARM64/Apple Silicon)
docker-windows-quick:
	@echo "Quick Windows API compatibility check..."
	docker build -f docker/Dockerfile.windows-quick -t httpmorph-windows-quick .
	docker run --rm httpmorph-windows-quick

docker-win-shell:
	@echo "Opening Windows test shell (MinGW cross-compile environment)..."
	docker-compose -f docker/docker-compose.windows-test.yml run windows-shell

docker-windows-build:
	@echo "Building Windows test image..."
	docker build -f docker/Dockerfile.windows-mingw -t httpmorph-windows-test .
