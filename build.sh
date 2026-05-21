#!/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
# GhostBoot — build.sh
# One-shot build: native Zygisk .so + Android companion APK + Magisk module .zip
#
# Prerequisites:
#   - ANDROID_NDK_HOME or ndk-build in PATH (for native)
#   - ANDROID_HOME or ANDROID_SDK_ROOT (for APK)
#   - JDK 17+
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

TOP="$(cd "$(dirname "$0")" && pwd)"

echo "========================================="
echo " GhostBoot — Full Build"
echo "========================================="

# ── Step 1: Build Zygisk native libraries ───────────────────────────────────
echo ""
echo ">>> Step 1/3: Building Zygisk .so files..."
cd "$TOP/native"
bash build_zygisk.sh

# ── Step 2: Build companion APK ─────────────────────────────────────────────
echo ""
echo ">>> Step 2/3: Building companion APK..."
cd "$TOP"
if command -v gradlew &>/dev/null; then
    ./gradlew assembleRelease
elif command -v gradle &>/dev/null; then
    gradle assembleRelease
else
    echo "WARNING: Gradle not found. Skipping APK build."
    echo "Install Android Studio or add gradle to PATH to build the APK."
fi

# ── Step 3: Package Magisk module ───────────────────────────────────────────
echo ""
echo ">>> Step 3/3: Packaging Magisk module..."

OUT="$TOP/release"
mkdir -p "$OUT"

MODULE_ZIP="$OUT/ghostboot-v1.0.zip"
cd "$TOP/module"
zip -r "$MODULE_ZIP" . \
    -x "*.DS_Store" \
    -x "*__MACOSX*" \
    -x "*.git*"

echo ""
echo "========================================="
echo " Build complete!"
echo " Module zip: $MODULE_ZIP"
if [ -f "$TOP/app/build/outputs/apk/release/app-release.apk" ]; then
    cp "$TOP/app/build/outputs/apk/release/app-release.apk" "$OUT/ghostboot-companion-v1.0.apk"
    echo " Companion APK: $OUT/ghostboot-companion-v1.0.apk"
fi
echo "========================================="
