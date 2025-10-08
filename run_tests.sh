#!/bin/bash
# Test runner for httpmorph
# Uses the correct Python 3.12 pytest with proper PYTHONPATH

PYTHON_BIN="/Library/Frameworks/Python.framework/Versions/3.12/bin"
PROJECT_ROOT="/Users/arman/fast-http-py"
export PYTHONPATH="${PROJECT_ROOT}/src"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}httpmorph Test Runner${NC}"
echo "Python: $PYTHON_BIN/python3"
echo "Pytest: $PYTHON_BIN/pytest"
echo "PYTHONPATH: $PYTHONPATH"
echo ""

case "$1" in
  "all")
    echo -e "${YELLOW}Running ALL tests (may timeout on some)...${NC}"
    $PYTHON_BIN/pytest tests/ -v
    ;;
  "basic")
    echo -e "${GREEN}Running basic tests...${NC}"
    $PYTHON_BIN/pytest tests/test_basic.py -v
    ;;
  "profiles")
    echo -e "${GREEN}Running browser profile tests...${NC}"
    $PYTHON_BIN/pytest tests/test_browser_profiles.py -v
    ;;
  "mock")
    echo -e "${GREEN}Running mock server tests...${NC}"
    $PYTHON_BIN/pytest tests/test_mock_server.py -v
    ;;
  "working")
    echo -e "${GREEN}Running all WORKING tests (no timeouts)...${NC}"
    $PYTHON_BIN/pytest tests/test_basic.py tests/test_browser_profiles.py tests/test_mock_server.py -v
    ;;
  "quick")
    echo -e "${GREEN}Running quick test suite...${NC}"
    $PYTHON_BIN/pytest tests/test_basic.py tests/test_browser_profiles.py tests/test_mock_server.py -q
    ;;
  "fixed")
    echo -e "${GREEN}Running recently fixed tests...${NC}"
    $PYTHON_BIN/pytest \
      tests/test_basic.py::test_post_with_json \
      tests/test_browser_profiles.py::TestBrowserProfiles::test_chrome_user_agent_matches_fingerprint \
      tests/test_browser_profiles.py::TestBrowserProfiles::test_firefox_user_agent_matches_fingerprint \
      -v
    ;;
  *)
    echo "Usage: $0 {all|basic|profiles|mock|working|quick|fixed}"
    echo ""
    echo "  all      - Run all tests (may hang)"
    echo "  basic    - Run basic functionality tests"
    echo "  profiles - Run browser profile tests"
    echo "  mock     - Run mock server tests"
    echo "  working  - Run all working tests (recommended)"
    echo "  quick    - Quick test run with minimal output"
    echo "  fixed    - Run recently fixed tests only"
    echo ""
    echo "Examples:"
    echo "  ./run_tests.sh working   # Run all working tests"
    echo "  ./run_tests.sh quick     # Quick check"
    echo "  ./run_tests.sh fixed     # Verify fixes"
    exit 1
    ;;
esac
