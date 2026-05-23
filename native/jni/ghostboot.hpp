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
    mutable std::mutex mutex_;
};

// ── Hook API ────────────────────────────────────────────────────────────────
bool apply_property_hooks();
bool apply_mount_hiding();

// ── Paths ───────────────────────────────────────────────────────────────────
const char* work_dir_path();
const char* config_file_path();

} // namespace ghostboot
