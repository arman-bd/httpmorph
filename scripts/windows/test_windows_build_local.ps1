# PowerShell script to build and test httpmorph wheels on Windows
# Mimics the GitHub Actions release pipeline (.github/workflows/release.yml)

param(
    [switch]$SkipTests = $false
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "httpmorph - Windows Wheel Builder" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$ProjectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

# Step 1: Setup vendors (BoringSSL, nghttp2, zlib)
Write-Host "==> Step 1: Setting up vendor dependencies..." -ForegroundColor Yellow

# Check for required tools
Write-Host "    Checking dependencies..." -ForegroundColor Gray

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Host "[FAIL] cmake not found. Install with: choco install cmake" -ForegroundColor Red
    exit 1
}
Write-Host "    [OK] cmake found" -ForegroundColor Green

$go = Get-Command go -ErrorAction SilentlyContinue
if (-not $go) {
    Write-Host "[FAIL] go not found. Install with: choco install golang" -ForegroundColor Red
    exit 1
}
Write-Host "    [OK] go found" -ForegroundColor Green

# Check vcpkg
if (-not (Test-Path "C:\vcpkg\vcpkg.exe")) {
    Write-Host "[FAIL] vcpkg not found at C:\vcpkg" -ForegroundColor Red
    Write-Host "       Install vcpkg and nghttp2/zlib:" -ForegroundColor Red
    Write-Host "       git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg" -ForegroundColor Yellow
    Write-Host "       C:\vcpkg\bootstrap-vcpkg.bat" -ForegroundColor Yellow
    Write-Host "       C:\vcpkg\vcpkg install nghttp2:x64-windows zlib:x64-windows" -ForegroundColor Yellow
    exit 1
}
Write-Host "    [OK] vcpkg found" -ForegroundColor Green

# Build BoringSSL
Write-Host ""
Write-Host "    Building BoringSSL..." -ForegroundColor Gray
Push-Location $ProjectRoot
bash scripts/setup_vendors.sh
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] Vendor setup failed" -ForegroundColor Red
    Pop-Location
    exit 1
}
Pop-Location
Write-Host "    [OK] Vendors built successfully" -ForegroundColor Green

# Step 2: Build wheel
Write-Host ""
Write-Host "==> Step 2: Building wheel..." -ForegroundColor Yellow

$env:VCPKG_ROOT = "C:\vcpkg"

Push-Location $ProjectRoot

# Build wheel
python setup.py bdist_wheel

if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] Wheel build failed" -ForegroundColor Red
    Pop-Location
    exit 1
}

Pop-Location

# Find the built wheel
$wheel = Get-ChildItem -Path "$ProjectRoot\dist" -Filter "*.whl" | Sort-Object LastWriteTime -Descending | Select-Object -First 1

if (-not $wheel) {
    Write-Host "[FAIL] No wheel found in dist/" -ForegroundColor Red
    exit 1
}

Write-Host "    [OK] Wheel built: $($wheel.Name)" -ForegroundColor Green

# Step 3: Test wheel
if (-not $SkipTests) {
    Write-Host ""
    Write-Host "==> Step 3: Testing wheel..." -ForegroundColor Yellow

    # Install wheel
    Write-Host "    Installing wheel..." -ForegroundColor Gray
    python -m pip install --force-reinstall "$($wheel.FullName)"

    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] Wheel installation failed" -ForegroundColor Red
        exit 1
    }

    # Run tests
    Write-Host ""
    Write-Host "    Running tests..." -ForegroundColor Gray
    python "$ProjectRoot\scripts\test_local_build.py"

    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] Tests failed" -ForegroundColor Red
        exit 1
    }

    Write-Host ""
    Write-Host "    [OK] All tests passed!" -ForegroundColor Green
}

# Summary
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "BUILD COMPLETE" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Wheel: $($wheel.FullName)" -ForegroundColor Green
Write-Host "Size:  $([math]::Round($wheel.Length / 1KB, 2)) KB" -ForegroundColor Green
Write-Host ""
Write-Host "To install:" -ForegroundColor Yellow
Write-Host "  pip install $($wheel.FullName)" -ForegroundColor Gray
Write-Host ""

exit 0
