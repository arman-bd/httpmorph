#!/bin/bash
#
# setup_vendors.sh - Download and build vendor dependencies
#
# This script detects the OS and delegates to OS-specific setup scripts:
# - scripts/windows/setup_vendors.sh
# - scripts/linux/setup_vendors.sh
# - scripts/darwin/setup_vendors.sh
#

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "================================"
echo "httpmorph - Vendor Setup"
echo "================================"
echo ""

# Detect OS
OS="$(uname -s)"
case "$OS" in
    MINGW*|MSYS*|CYGWIN*)
        OS_DIR="windows"
        OS_NAME="Windows"
        ;;
    Darwin)
        OS_DIR="darwin"
        OS_NAME="macOS"
        ;;
    Linux)
        OS_DIR="linux"
        OS_NAME="Linux"
        ;;
    *)
        echo "ERROR: Unsupported OS: $OS"
        exit 1
        ;;
esac

echo "Detected OS: $OS_NAME"
echo "Delegating to: scripts/$OS_DIR/setup_vendors.sh"
echo ""

# Execute OS-specific script
OS_SCRIPT="$SCRIPT_DIR/$OS_DIR/setup_vendors.sh"

if [ ! -f "$OS_SCRIPT" ]; then
    echo "ERROR: OS-specific script not found: $OS_SCRIPT"
    exit 1
fi

# Make sure the script is executable
chmod +x "$OS_SCRIPT"

# Execute the OS-specific script
exec "$OS_SCRIPT"
