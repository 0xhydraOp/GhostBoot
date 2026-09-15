// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — mount_ns.cpp
// Per-process mount namespace isolation.
// unshare(CLONE_NEWNS) + bind-mount empty fs over every root-indicating path,
// then serve filtered proc snapshots so detectors can't see the hiding itself.
// ─────────────────────────────────────────────────────────────────────────────
#include "ghostboot.hpp"

#include <sys/mount.h>
#include <sys/stat.h>
#include <sched.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <android/log.h>

#define TAG "GhostBoot"
// Stealth-aware: silent when stealth_mode is on (see settings.conf).
#define LOGW(...) do { if (ghostboot::logging_enabled()) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__); } while (0)

namespace ghostboot {
namespace {

bool exists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

bool hide_file(const char* path) {
    if (!exists(path)) return true;
    return mount("/dev/null", path, nullptr, MS_BIND, nullptr) == 0;
}

bool hide_dir(const char* path) {
    if (!exists(path)) return true;
    // Mount empty tmpfs — mode 0755 looks like a normal empty directory.
    // size=4096 handles kernels that reject size=0.
    return mount("tmpfs", path, "tmpfs", 0, "size=4096,nr_inodes=2,mode=0755") == 0;
}

// LSPosed-related paths are skipped when the user disabled LSPosed hiding.
bool lsposed_related(const char* p) {
    return strstr(p, "lspd") || strstr(p, "lsposed") || strstr(p, "xposed") ||
           strstr(p, "riru") || strstr(p, "edxposed");
}

} // anonymous namespace

bool apply_mount_hiding() {
    const Settings s = settings();
    if (s.root_hide == RootHideLevel::Off) return true;  // gated off — nothing to do

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
        if (!s.lsposed_hide && lsposed_related(p)) continue;
        if (!exists(p)) continue;

        struct stat st;
        if (stat(p, &st) != 0) continue;  // race-safe: re-check under stat

        bool ok = S_ISDIR(st.st_mode) ? hide_dir(p) : hide_file(p);
        if (ok) hidden++;
    }

    // Step 4: proc-visibility filters (best-effort — a missed filter only
    // weakens hiding for that file, never fails the whole pass).
    if (!apply_proc_filters())
        LOGW("apply_mount_hiding: some proc filters failed");

    return hidden > 0;
}

} // namespace ghostboot
