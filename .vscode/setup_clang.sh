#!/bin/bash

# Setup script to configure clang paths for the project
# This script helps ensure the correct clang version is used

echo "=== Clang Setup for tbox project ==="

# Check if preferred clang exists
PREFERRED_CLANG="/usr/local/llvm/18/bin/clang"
PREFERRED_CLANG_FORMAT="/usr/local/llvm/18/bin/clang-format"

if [ -x "$PREFERRED_CLANG" ]; then
    echo "✓ Found preferred clang at: $PREFERRED_CLANG"
    CLANG_VERSION=$($PREFERRED_CLANG --version | head -n1)
    echo "  Version: $CLANG_VERSION"
    
    # Check if it's in PATH
    if command -v clang >/dev/null 2>&1; then
        CURRENT_CLANG=$(command -v clang)
        if [ "$CURRENT_CLANG" = "$PREFERRED_CLANG" ]; then
            echo "✓ Preferred clang is already first in PATH"
        else
            echo "⚠ Current clang in PATH: $CURRENT_CLANG"
            echo "  To use preferred clang, add this to your ~/.bashrc or ~/.zshrc:"
            echo "  export PATH=\"/usr/local/llvm/18/bin:\$PATH\""
        fi
    else
        echo "⚠ No clang found in PATH"
        echo "  Add this to your ~/.bashrc or ~/.zshrc:"
        echo "  export PATH=\"/usr/local/llvm/18/bin:\$PATH\""
    fi
else
    echo "⚠ Preferred clang not found at: $PREFERRED_CLANG"
    if command -v clang >/dev/null 2>&1; then
        SYSTEM_CLANG=$(command -v clang)
        echo "✓ Found system clang at: $SYSTEM_CLANG"
        CLANG_VERSION=$($SYSTEM_CLANG --version | head -n1)
        echo "  Version: $CLANG_VERSION"
    else
        echo "✗ No clang found! Please install clang."
        exit 1
    fi
fi

# Check clang-format
echo ""
if [ -x "$PREFERRED_CLANG_FORMAT" ]; then
    echo "✓ Found preferred clang-format at: $PREFERRED_CLANG_FORMAT"
elif command -v clang-format >/dev/null 2>&1; then
    SYSTEM_CLANG_FORMAT=$(command -v clang-format)
    echo "✓ Found system clang-format at: $SYSTEM_CLANG_FORMAT"
else
    echo "⚠ No clang-format found! Code formatting may not work."
fi

echo ""
echo "=== VSCode Configuration ==="
echo "The .vscode/settings.json is configured to use 'clang' and 'clang-format'"
echo "from your PATH. Make sure the desired versions are first in your PATH."

echo ""
echo "=== Current PATH clang tools ==="
echo "clang: $(command -v clang 2>/dev/null || echo 'not found')"
echo "clang-format: $(command -v clang-format 2>/dev/null || echo 'not found')"
