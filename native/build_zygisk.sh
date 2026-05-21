#!/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
# GhostBoot — build_zygisk.sh
# Builds the Zygisk .so for all architectures using the NDK,
# then copies each .so to module/zygisk/<abi>.so ready for packaging.
#
# Prerequisites:
#   - ANDROID_NDK_HOME environment variable set
#   - or ndk-build in PATH
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MODULE_ZIGISK_DIR="$PROJECT_DIR/module/zygisk"

echo "=== GhostBoot Zygisk Builder ==="

# Locate ndk-build
NDK_BUILD="${ANDROID_NDK_HOME:-}/ndk-build"
if [ ! -x "$NDK_BUILD" ]; then
    NDK_BUILD="ndk-build"
fi
if ! command -v "$NDK_BUILD" &>/dev/null; then
    echo "ERROR: ndk-build not found. Set ANDROID_NDK_HOME or add to PATH."
    exit 1
fi

# Build native library
cd "$SCRIPT_DIR"
echo "Building native libraries..."
"$NDK_BUILD" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk -j"$(nproc)" 2>&1

# Copy .so files to module packaging directory
mkdir -p "$MODULE_ZIGISK_DIR"

for abi in arm64-v8a armeabi-v7a x86_64 x86; do
    SRC="libs/$abi/libghostboot.so"
    if [ -f "$SRC" ]; then
        cp -v "$SRC" "$MODULE_ZIGISK_DIR/$abi.so"
    else
        echo "WARNING: $SRC not built (skipping $abi)"
    fi
done

echo "=== Zygisk .so files placed in module/zygisk/ ==="
ls -la "$MODULE_ZIGISK_DIR"

# Optional: strip symbols for smaller size
if command -v llvm-strip &>/dev/null; then
    for so in "$MODULE_ZIGISK_DIR"/*.so; do
        llvm-strip --strip-unneeded "$so"
    done
    echo "Stripped debug symbols."
elif command -v "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/"*"/bin/llvm-strip" &>/dev/null 2>&1; then
    STRIP="$(echo "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/"*"/bin/llvm-strip")"
    for so in "$MODULE_ZIGISK_DIR"/*.so; do
        "$STRIP" --strip-unneeded "$so"
    done
    echo "Stripped debug symbols."
fi
