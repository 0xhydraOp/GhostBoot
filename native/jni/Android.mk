# ─────────────────────────────────────────────────────────────────────────────
# GhostBoot — Android.mk (NDK build)
# Builds the Zygisk .so for each target ABI.
# ─────────────────────────────────────────────────────────────────────────────
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE       := ghostboot
LOCAL_SRC_FILES    := main.cpp property_hook.cpp mount_ns.cpp config.cpp
LOCAL_CPPFLAGS     := -std=c++17 -O2 -fvisibility=hidden -fno-rtti -fno-exceptions \
                      -Wall -Wextra -Wno-unused-parameter -fPIC -DANDROID
LOCAL_LDFLAGS      := -Wl,--exclude-libs,ALL -Wl,-z,relro -Wl,-z,now -Wl,--gc-sections \
                      -Wl,-z,max-page-size=0x1000
include $(BUILD_SHARED_LIBRARY)
