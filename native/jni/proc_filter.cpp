// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — proc_filter.cpp
// Hides the hiding itself: serves filtered snapshots of proc files inside the
// target's private mount namespace, so detectors can't see our tmpfs mounts,
// our .so mapping, or the Orange/unlocked cmdline.
//
// Technique: read the live file -> drop/sanitize sensitive lines -> write the
// result to an anonymous memfd -> bind-mount it over the original path. No
// staging files on disk (nothing to clean, nothing to find), and the bind
// holds its own reference so later tmpfs hides can't disturb it.
//
// Ordering matters: snapshots are taken BEFORE our binds, so the served
// copies never list our own bind entries.
//
// Best-effort throughout: any single failure only skips that file, never
// aborts the whole hiding pass. A missed filter is weaker than a crash.
// ─────────────────────────────────────────────────────────────────────────────
#include "ghostboot.hpp"

#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <fcntl.h>
#ifdef __linux__
#include <linux/fcntl.h>
#endif
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#ifndef SYS_memfd_create
#if defined(__aarch64__)
#define SYS_memfd_create 279
#elif defined(__arm__)
#define SYS_memfd_create 385
#elif defined(__x86_64__)
#define SYS_memfd_create 319
#elif defined(__i386__)
#define SYS_memfd_create 356
#endif
#endif

namespace ghostboot {
namespace {

// Anonymous in-memory file, no disk footprint.
// Name is disguised as a generic dmabuf (not "gb") so /proc/pid/maps
// readers see anon_inode:dmabuf instead of an obvious memfd tag.
int memfd_create_compat(const char* name) {
#ifdef SYS_memfd_create
    const char* n = (name && *name) ? name : "dmabuf";
    int fd = (int)syscall((long)SYS_memfd_create, n, (unsigned)MFD_CLOEXEC);
    if (fd >= 0) {
        // Seal growth+shrink: snapshot is immutable, looks like a sealed
        // dma-buf rather than a writable scratch memfd. Best-effort:
        // kernels without sealing support just skip.
        fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE);
    }
    return fd;
#else
    (void)name;
    return -1;
#endif
}

bool read_whole_file(const char* path, std::string& out, size_t cap = 2u << 20) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.clear();
    char buf[8192];
    while (f) {
        f.read(buf, sizeof(buf));
        std::streamsize n = f.gcount();
        if (n <= 0) break;
        if (out.size() + (size_t)n > cap) return false;  // absurdly large — bail
        out.append(buf, (size_t)n);
    }
    return true;
}

bool write_all(int fd, const char* data, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n <= 0) return false;
        data += n;
        len -= (size_t)n;
    }
    return true;
}

// Case-insensitive substring search (mount paths are lowercase, but be safe).
bool ci_contains(const std::string& hay, const char* needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || hay.size() < nlen) return false;
    for (size_t i = 0; i + nlen <= hay.size(); i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)hay[i + j]) != tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool line_matches_any(const std::string& line, const char* const* tokens) {
    for (int i = 0; tokens[i]; i++)
        if (ci_contains(line, tokens[i])) return true;
    return false;
}

// Mount-hiding evidence: our tmpfs binds, Magisk/Zygisk/LSPosed paths, su.
const char* const kMountTokens[] = {
    "data/adb", ".magisk", "magisk", "zygisk", "riru", "lspd",
    "xposed", "substrate", "supersu", "xbin/su", "bin/su", "ghostboot",
    "magiskd", "zygiskd", "lsposedd",
    nullptr,
};

// Unix-domain sockets that leak daemon presence via /proc/net/unix.
const char* const kUnixTokens[] = {
    "magisk", "zygisk", "riru", "lspd", "lsposed", "xposed", "edxp",
    "supersu", "ghostboot",
    nullptr,
};

// Our own mapping + loader traces in maps/smaps.
const char* const kMapsTokens[] = {
    "data/adb", ".magisk", "magisk", "zygisk", "riru", "lspd",
    "xposed", "substrate", "supersu", "ghostboot", "lsposed",
    nullptr,
};

// Bind a filtered snapshot over |target|. Returns true on success or when
// nothing needed filtering (leave the live file alone in that case).
bool bind_filtered_snapshot(const char* target, const char* const* tokens) {
    std::string content;
    if (!read_whole_file(target, content)) return true;  // unreadable — nothing to do

    std::string filtered;
    filtered.reserve(content.size());
    bool dropped = false;
    size_t pos = 0;
    while (pos <= content.size()) {
        size_t nl = content.find('\n', pos);
        std::string line = (nl == std::string::npos) ? content.substr(pos)
                                                     : content.substr(pos, nl - pos);
        if (line_matches_any(line, tokens)) {
            dropped = true;
        } else {
            filtered += line;
            filtered += '\n';
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    if (!dropped) return true;  // already clean — don't touch proc

    int fd = memfd_create_compat(nullptr);
    if (fd < 0) return false;
    bool ok = write_all(fd, filtered.data(), filtered.size());
    if (ok) {
        char src[64];
        snprintf(src, sizeof(src), "/proc/self/fd/%d", fd);
        ok = mount(src, target, nullptr, MS_BIND, nullptr) == 0;
    }
    close(fd);
    return ok;
}

// Replace Orange/unlocked markers in the kernel cmdline with locked ones.
bool sanitize_cmdline() {
    const char* target = "/proc/cmdline";
    std::string content;
    if (!read_whole_file(target, content, 1u << 16)) return true;

    std::string out = content;
    bool changed = false;
    auto replace_all = [&](const char* from, const char* to) {
        size_t flen = strlen(from);
        size_t pos = 0;
        while ((pos = out.find(from, pos)) != std::string::npos) {
            out.replace(pos, flen, to);
            pos += strlen(to);
            changed = true;
        }
    };
    // Kernel uses lowercase; handle the common spellings.
    replace_all("verifiedbootstate=orange", "verifiedbootstate=green");
    replace_all("androidboot.verifiedbootstate=orange", "androidboot.verifiedbootstate=green");
    replace_all("unlocked", "locked");
    if (!changed) return true;

    int fd = memfd_create_compat(nullptr);
    if (fd < 0) return false;
    bool ok = write_all(fd, out.data(), out.size());
    if (ok) {
        char src[64];
        snprintf(src, sizeof(src), "/proc/self/fd/%d", fd);
        ok = mount(src, target, nullptr, MS_BIND, nullptr) == 0;
    }
    close(fd);
    return ok;
}

// Root manager packages visible in the package list.
bool filter_packages_list() {
    const char* target = "/data/system/packages.list";
    static const char* const kRootPkgs[] = {
        "com.topjohnwu.magisk",
        "org.lsposed.manager",
        "de.robv.android.xposed.installer",
        "com.solohsu.android.edxp.manager",
        "org.meowcat.edxposed.manager",
        "com.elderdrivers.riru.edxp",
        "org.lsposed.lspatch",
        nullptr,
    };
    std::string content;
    if (!read_whole_file(target, content)) return true;  // unreadable — skip

    std::string filtered;
    filtered.reserve(content.size());
    bool dropped = false;
    size_t pos = 0;
    while (pos <= content.size()) {
        size_t nl = content.find('\n', pos);
        std::string line = (nl == std::string::npos) ? content.substr(pos)
                                                     : content.substr(pos, nl - pos);
        // Line format: "<package> <uid> ...". Match the first field exactly.
        size_t sp = line.find(' ');
        std::string pkg = (sp == std::string::npos) ? line : line.substr(0, sp);
        bool hit = false;
        for (int i = 0; kRootPkgs[i]; i++)
            if (pkg == kRootPkgs[i]) { hit = true; break; }
        if (hit) dropped = true;
        else { filtered += line; filtered += '\n'; }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    if (!dropped) return true;

    int fd = memfd_create_compat(nullptr);
    if (fd < 0) return false;
    bool ok = write_all(fd, filtered.data(), filtered.size());
    if (ok) {
        char src[64];
        snprintf(src, sizeof(src), "/proc/self/fd/%d", fd);
        ok = mount(src, target, nullptr, MS_BIND, nullptr) == 0;
    }
    close(fd);
    return ok;
}

} // anonymous namespace

bool apply_proc_filters() {
    const Settings s = settings();
    bool ok = true;

    // BASIC and up: hide mount evidence (our own tmpfs binds live here).
    ok &= bind_filtered_snapshot("/proc/self/mounts", kMountTokens);
    ok &= bind_filtered_snapshot("/proc/self/mountinfo", kMountTokens);
    ok &= bind_filtered_snapshot("/proc/mounts", kMountTokens);
    // Daemon sockets leak via net/unix even when mounts are clean (#1
    // Shamiko-bypass vector). Also cover the global cmdline copy.
    ok &= bind_filtered_snapshot("/proc/net/unix", kUnixTokens);
    ok &= bind_filtered_snapshot("/proc/self/net/unix", kUnixTokens);

    if (s.root_hide == RootHideLevel::Aggressive) {
        // Our .so mapping + loader traces. Served snapshot predates our
        // binds, so it never lists the filter binds themselves.
        // Cover per-thread maps too: detectors iterate task/*/maps.
        ok &= bind_filtered_snapshot("/proc/self/maps", kMapsTokens);
        ok &= bind_filtered_snapshot("/proc/self/smaps", kMapsTokens);
        ok &= bind_filtered_snapshot("/proc/self/task/self/maps", kMapsTokens);
        // Bootloader state echoes outside system properties.
        ok &= sanitize_cmdline();
        // Filesystem-level package hiding (PMS queries still need a
        // framework-level hider such as Shamiko — documented limitation).
        if (s.lsposed_hide) ok &= filter_packages_list();
    }
    return ok;
}

} // namespace ghostboot
