# ─────────────────────────────────────────────────────────────────────────────
# GhostBoot — Application.mk
# ─────────────────────────────────────────────────────────────────────────────
APP_ABI          := arm64-v8a armeabi-v7a x86_64 x86
APP_PLATFORM     := android-29
APP_STL          := c++_static
APP_CPPFLAGS     := -std=c++17 -fvisibility=hidden
APP_LDFLAGS      := -Wl,--exclude-libs,ALL
APP_SHORT_COMMANDS := true
