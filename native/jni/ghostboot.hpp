// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — ghostboot.hpp
// Internal header: spoof tables, TargetConfig, hook API, work paths.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <string>
#include <unordered_set>
#include <mutex>

namespace ghostboot {

// ── Bootloader properties spoofed to "locked stock" ─────────────────────────
struct PropSpoof { const char* name; const char* value; };

inline constexpr PropSpoof kBootloaderSpoofs[] = {
    {"ro.boot.verifiedbootstate",  "green"},
    {"ro.boot.flash.locked",       "1"},
    {"ro.boot.vbmeta.device_state","locked"},
    {"ro.boot.veritymode",         "enforcing"},
    {"ro.boot.warranty_bit",       "0"},
    {"ro.boot.mode",               "normal"},
    {"ro.boot.secureboot",         "1"},
    {"ro.boot.selinux",            "enforcing"},
    {nullptr, nullptr}
};

inline constexpr PropSpoof kBuildSpoofs[] = {
    {"ro.build.tags",              "release-keys"},
    {"ro.build.type",              "user"},
    {"ro.debuggable",              "0"},
    {"ro.secure",                  "1"},
    {nullptr, nullptr}
};

// ── Filesystem paths hidden via bind mount ──────────────────────────────────
// Ordered: directory-level mounts first (shadow everything underneath),
// then individual file paths not covered by directory mounts.
inline constexpr const char* kHidePaths[] = {
    // Directory mounts — empty tmpfs over entire tree
    "/data/adb",                     // covers: magisk, modules, lspd, riru, all subdirs
    "/sbin/.magisk",
    "/debug_ramdisk/.magisk",

    // su binary locations (individual files — not under /data/adb)
    "/system/xbin/su",
    "/system/bin/su",
    "/sbin/su",
    "/vendor/bin/su",
    "/system/sbin/su",
    "/system_ext/bin/su",
    "/product/bin/su",
    "/odm/bin/su",

    // Known root app directories (best effort — may not exist)
    "/system/app/Superuser",
    "/system/app/SuperSU",
    "/system/app/Magisk",
    "/system/priv-app/SuperSU",
    "/system/priv-app/Magisk",

    // Root manager private data dirs (hidden per-app via tmpfs)
    "/data/data/com.topjohnwu.magisk",
    "/data/data/org.lsposed.manager",
    "/data/data/de.robv.android.xposed.installer",
    "/data/data/com.solohsu.android.edxp.manager",
    "/data/data/org.meowcat.edxposed.manager",

    // Riru/Xposed misc traces outside /data/adb
    "/data/misc/riru",
    "/data/misc/riru-modules",
    nullptr
};

// ── Target app configuration ────────────────────────────────────────────────
class TargetConfig {
public:
    static TargetConfig& instance();

    void load();
    void save();

    bool is_target(const char* process_name) const;

    void add(const std::string& package);
    void remove(const std::string& package);
    void clear();
    std::unordered_set<std::string> list() const;

private:
    TargetConfig() = default;
    std::unordered_set<std::string> packages_;
    mutable std::recursive_mutex mutex_;

    void saveLocked() const;
};

// ── Companion-driven settings (settings.conf) ───────────────────────────────
// Written by the companion app alongside targets.conf. Missing file or keys
// fall back to the defaults below (all protections ON, stealth OFF), so an
// old install without the new companion keeps working.
enum class RootHideLevel { Off, Basic, Aggressive };

struct Settings {
    bool bootloader_spoof = true;
    RootHideLevel root_hide = RootHideLevel::Basic;
    bool lsposed_hide = true;
    bool stealth_mode = false;
};

// Reload from disk (cheap, small file). Called at onLoad and per specialize
// so a settings change applies to the next launched target (no reboot).
void reload_settings();
// Cached snapshot of the last reload.
Settings settings();
// False when stealth_mode is on — use for all logcat output.
bool logging_enabled();

// ── Hook API ────────────────────────────────────────────────────────────────
bool apply_property_hooks();
bool apply_mount_hiding();
// Proc-visibility filters (mounts/mountinfo always; maps/smaps/cmdline/
// packages.list on AGGRESSIVE). Best-effort: never fatal on failure.
bool apply_proc_filters();
// JNI-level android.os.Build static-field patch (Java sees release-keys/user
// even though the class was already initialized). No-op without a JVM.
bool apply_java_build_patch(void* java_vm);

// ── Paths ───────────────────────────────────────────────────────────────────
const char* work_dir_path();
const char* config_file_path();
const char* settings_file_path();

} // namespace ghostboot
