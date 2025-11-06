#!/bin/bash

# GCC wrapper script to force BFD linker for musl builds
# This script replaces -fuse-ld=lld with -fuse-ld=bfd

args=()
for arg in "$@"; do
    if [[ "$arg" == "-fuse-ld=lld" ]]; then
        args+=("-fuse-ld=bfd")
    else
        args+=("$arg")
    fi
done

# Detect which tool we're being called as based on environment or fallback to gcc
if [[ -n "$CC_TOOLCHAIN_TYPE" ]]; then
    TOOL_NAME="$CC_TOOLCHAIN_TYPE"
elif [[ "$0" == *"c++"* ]] || [[ "${args[0]}" == *"c++"* ]]; then
    TOOL_NAME="c++"
elif [[ "$0" == *"g++"* ]] || [[ "${args[0]}" == *"g++"* ]]; then
    TOOL_NAME="g++"
else
    TOOL_NAME="gcc"
fi

# Find the real executable - we're being called from within the toolchain bin directory
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
REAL_TOOL="$SCRIPT_DIR/aarch64-unknown-linux-musl-$TOOL_NAME"

# If not found, try common fallback locations
if [[ ! -f "$REAL_TOOL" ]]; then
    # Try with .bin suffix
    REAL_TOOL="$SCRIPT_DIR/aarch64-unknown-linux-musl-$TOOL_NAME.bin"
fi

if [[ ! -f "$REAL_TOOL" ]]; then
    # Fallback to PATH
    REAL_TOOL="aarch64-unknown-linux-musl-$TOOL_NAME"
fi

exec "$REAL_TOOL" "${args[@]}"
