// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — mount_ns.cpp
// Per-process mount namespace isolation.
// unshare(CLONE_NEWNS) + bind-mount empty fs over every root-indicating path.
// ─────────────────────────────────────────────────────────────────────────────
#include "ghostboot.hpp"

#include <sys/mount.h>
#include <sys/stat.h>
#include <sched.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

namespace ghostboot {
namespace {

static bool exists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static bool hide_file(const char* path) {
    if (!exists(path)) return true;
    return mount("/dev/null", path, nullptr, MS_BIND, nullptr) == 0;
}

static bool hide_dir(const char* path) {
    if (!exists(path)) return true;
    // Mount empty tmpfs — mode 0755 looks like a normal empty directory
    if (mount("tmpfs", path, "tmpfs", 0, "size=0,nr_inodes=1,mode=0755") == 0) return true;
    return mount("tmpfs", path, "tmpfs", 0, "size=4096,nr_inodes=2,mode=0755") == 0;
}

} // anonymous namespace

bool apply_mount_hiding() {
    // Step 1: create private mount namespace (requires CAP_SYS_ADMIN)
    if (unshare(CLONE_NEWNS) != 0) return false;

    // Step 2: make root mount private — CRITICAL: if this fails, we MUST abort
    // because subsequent bind mounts would propagate to parent namespace and
    // hide /data/adb SYSTEM-WIDE, breaking Magisk for every app.
    if (mount(nullptr, "/", nullptr, MS_PRIVATE | MS_REC, nullptr) != 0) {
        // Mount propagation failed — abort to avoid system-wide corruption
        return false;
    }

    // Step 3: hiding loop
    int hidden = 0;
    for (int i = 0; kHidePaths[i]; i++) {
        const char* p = kHidePaths[i];
        if (!exists(p)) continue;

        struct stat st;
        if (stat(p, &st) != 0) continue;  // race-safe: re-check under stat

        bool ok = S_ISDIR(st.st_mode) ? hide_dir(p) : hide_file(p);
        if (ok) hidden++;
    }
    return hidden > 0;
}

} // namespace ghostboot
