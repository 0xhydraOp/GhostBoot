// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — property_hook.cpp
// PLT / GOT hooks on the Bionic system-property API.
// Bootloader props return "locked / green" inside target apps.
//
// Covered entry points (whichever the app's libraries import):
//   • __system_property_get            (const char*, char*) -> int
//   • __system_property_read           (pi, name, value)   -> int
//   • __system_property_read_callback  (pi, callback, cookie)
// __system_property_find is deliberately NOT hooked: fabricating a prop_info
// is unsafe; get/read/callback coverage is sufficient.
//
// CRASH FIXES applied:
//   • mprotect return value checked — if page can't be made writable, skip
//   • __builtin___clear_cache removed — not needed for GOT data writes;
//     it triggers MTE/tagged-pointer aborts on ARM64 Android 14+
//   • Self-skip via dladdr to avoid patching our own GOT (infinite recursion)
//   • Runtime page size (16K-page Android 15+ devices)
//   • REL (32-bit ARM/x86) and RELA (64-bit) PLT formats both handled
// ─────────────────────────────────────────────────────────────────────────────
#include "ghostboot.hpp"

#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <elf.h>
#include <android/log.h>

#define TAG "GhostBoot"
// Stealth-aware: silent when stealth_mode is on (see settings.conf).
#define LOGW(...) do { if (ghostboot::logging_enabled()) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__); } while (0)

// Architecture-neutral ELF relocation symbol index
#if __SIZEOF_POINTER__ == 8
#define GHOST_ELF_R_SYM(i) ELF64_R_SYM(i)
#else
#define GHOST_ELF_R_SYM(i) ELF32_R_SYM(i)
#endif

#define PROP_VALUE_MAX 92

namespace ghostboot {
namespace {

static int (*orig_prop_get)(const char*, char*) = nullptr;
// prop_info is opaque — void* is ABI-identical and avoids header coupling.
static int (*orig_prop_read)(const void*, char*, char*) = nullptr;
static void (*orig_prop_read_cb)(const void*,
                                 void (*)(void*, const char*, const char*, uint32_t),
                                 void*) = nullptr;

static const char* lookup_spoof(const char* name) {
    if (!name) return nullptr;
    for (int i = 0; kBootloaderSpoofs[i].name; i++)
        if (!strcmp(name, kBootloaderSpoofs[i].name))
            return kBootloaderSpoofs[i].value;
    for (int i = 0; kBuildSpoofs[i].name; i++)
        if (!strcmp(name, kBuildSpoofs[i].name))
            return kBuildSpoofs[i].value;
    return nullptr;
}

static const void* (*orig_prop_find)(const char*) = nullptr;
static int (*orig_prop_get_bool)(const char*, int) = nullptr;
static long long (*orig_prop_get_long)(const char*, long long) = nullptr;

// Poison __system_property_find: we must NOT fabricate a prop_info
// (unsafe), but returning NULL for debug markers forces Java/native
// fallbacks down the spoofed get/read path instead of the real area.
static const void* hooked_prop_find(const char* name) {
    if (name && (!strcmp(name, "ro.debuggable") || !strcmp(name, "ro.secure")))
        return nullptr;
    if (orig_prop_find) return orig_prop_find(name);
    return nullptr;
}

static int hooked_prop_get_bool(const char* name, int def) {
    const char* s = lookup_spoof(name);
    if (s) return (!strcmp(s, "1") || !strcmp(s, "true"));
    if (orig_prop_get_bool) return orig_prop_get_bool(name, def);
    return def;
}

static long long hooked_prop_get_long(const char* name, long long def) {
    const char* s = lookup_spoof(name);
    if (s) {
        long long v = def;
        sscanf(s, "%lld", &v);
        return v;
    }
    if (orig_prop_get_long) return orig_prop_get_long(name, def);
    return def;
}

// Apply substring scrubs to a passthrough value in place.
// Bounded: never grows the buffer, always NUL-terminates.
static void scrub_value_in_place(char* value) {
    if (!value) return;
    char buf[PROP_VALUE_MAX];
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    std::string out(buf);
    bool changed = false;
    for (int i = 0; kValueScrubs[i].from; i++) {
        size_t flen = strlen(kValueScrubs[i].from);
        size_t pos = 0;
        while ((pos = out.find(kValueScrubs[i].from, pos)) != std::string::npos) {
            out.replace(pos, flen, kValueScrubs[i].to);
            pos += strlen(kValueScrubs[i].to);
            changed = true;
        }
    }
    if (changed && out.size() < sizeof(buf)) {
        memcpy(value, out.c_str(), out.size() + 1);
    }
}

static int hooked_prop_get(const char* name, char* value) {
    const char* s = lookup_spoof(name);
    if (s && value) {
        size_t n = strlen(s);
        if (n < PROP_VALUE_MAX - 1) {
            memcpy(value, s, n + 1);
            return static_cast<int>(n);
        }
    }
    // Forward to original — safe because orig_prop_get is set before
    // any GOT entry is patched to point to this function.
    // Passthrough values still get substring scrubs (fingerprints etc.).
    if (orig_prop_get) {
        int r = orig_prop_get(name, value);
        if (r >= 0 && value) scrub_value_in_place(value);
        return r;
    }
    return -1;
}

static int hooked_prop_read(const void* pi, char* name, char* value) {
    int r = -1;
    if (orig_prop_read) r = orig_prop_read(pi, name, value);
    // orig fills name+value; substitute after the fact. name may be null
    // per the API contract — lookup_spoof handles that.
    const char* s = lookup_spoof(name);
    if (s && value) {
        size_t n = strlen(s);
        if (n < PROP_VALUE_MAX - 1) {
            memcpy(value, s, n + 1);
            return static_cast<int>(n);
        }
    }
    if (value) scrub_value_in_place(value);
    return r;
}

// read_callback trampoline state — thread_local because property reads can
// race across threads; globals would mix up cookies.
static thread_local void (*tl_cb)(void*, const char*, const char*, uint32_t) = nullptr;
static thread_local void* tl_cookie = nullptr;

static void read_cb_trampoline(void*, const char* name, const char* value, uint32_t serial) {
    const char* s = lookup_spoof(name);
    if (s) value = s;
    if (tl_cb) tl_cb(tl_cookie, name, value, serial);
}

static void hooked_prop_read_cb(const void* pi,
                                void (*cb)(void*, const char*, const char*, uint32_t),
                                void* cookie) {
    if (!orig_prop_read_cb) return;
    tl_cb = cb;
    tl_cookie = cookie;
    orig_prop_read_cb(pi, read_cb_trampoline, nullptr);
    tl_cb = nullptr;
    tl_cookie = nullptr;
}

// ── GOT patching ────────────────────────────────────────────────────────────
struct GotCtx {
    const char* sym;
    void* target;
    void* hook;
    int patches;
    long page_size;  // runtime-detected, set by caller
};

static void patch_one_got(GotCtx* ctx, const char* dlpi_name, uintptr_t got_addr) {
    auto* got = reinterpret_cast<uintptr_t*>(got_addr);

    // Read current GOT value BEFORE mprotect (verify it's readable)
    uintptr_t current = *got;
    if (current != reinterpret_cast<uintptr_t>(ctx->target)) return;

    // Make the page writable — CHECK RETURN VALUE
    long ps = ctx->page_size > 0 ? ctx->page_size : 0x1000;
    uintptr_t pg = reinterpret_cast<uintptr_t>(got) & ~(uintptr_t)(ps - 1);
    if (mprotect(reinterpret_cast<void*>(pg), (size_t)ps,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        // mprotect failed (SELinux, seccomp, or memory protection).
        // Skip this GOT entry — a crash is worse than a missed hook.
        LOGW("mprotect(RWX) failed for %s in %s (errno=%d)",
             ctx->sym, dlpi_name ? dlpi_name : "?", errno);
        return;
    }

    // Write our hook address
    *got = reinterpret_cast<uintptr_t>(ctx->hook);

    // Restore original permissions
    mprotect(reinterpret_cast<void*>(pg), (size_t)ps, PROT_READ | PROT_EXEC);

    // NOT calling __builtin___clear_cache here.
    // The GOT is data, not code.  On ARM64 Android 14+ with MTE /
    // tagged pointers, __builtin___clear_cache can trigger "Pointer
    // tag was truncated" aborts when given a data address that
    // happens to carry a hardware memory tag.  The data-cache
    // coherency is guaranteed by the mprotect calls above (which
    // include implicit DMB/DSB barriers on ARM64 Linux).

    ctx->patches++;
}

static int got_cb(struct dl_phdr_info* info, size_t, void* data) {
    auto* ctx = static_cast<GotCtx*>(data);
    uintptr_t base = info->dlpi_addr;

    // Skip our own module — use dladdr to find our base address.
    // (dlpi_name is unreliable: Zygisk renames .so to arm64-v8a.so / armeabi-v7a.so / etc.)
    Dl_info self_info;
    if (dladdr(reinterpret_cast<void*>(&got_cb), &self_info) &&
        info->dlpi_addr == reinterpret_cast<uintptr_t>(self_info.dli_fbase)) {
        return 0;
    }

    const ElfW(Dyn)* dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; i++)
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC)
            { dyn = reinterpret_cast<const ElfW(Dyn)*>(base + info->dlpi_phdr[i].p_vaddr); break; }
    if (!dyn) return 0;

    const void*  jmprel = nullptr; size_t pltsz = 0; int pltrel = DT_RELA;
    const ElfW(Sym)* symtab = nullptr; const char* strtab = nullptr;
    for (; dyn->d_tag != DT_NULL; dyn++) {
        switch (dyn->d_tag) {
        case DT_JMPREL:   jmprel = reinterpret_cast<const void*>(base + dyn->d_un.d_ptr); break;
        case DT_PLTRELSZ: pltsz  = dyn->d_un.d_val; break;
        case DT_PLTREL:   pltrel = static_cast<int>(dyn->d_un.d_val); break;
        case DT_SYMTAB:   symtab = reinterpret_cast<const ElfW(Sym)*>(base + dyn->d_un.d_ptr); break;
        case DT_STRTAB:   strtab = reinterpret_cast<const char*>(base + dyn->d_un.d_ptr); break;
        }
    }
    if (!jmprel || !symtab || !strtab || !pltsz) return 0;

    // Per-relocation: resolve symbol name, compare, patch on match.
    auto process_one = [&](uintptr_t r_offset, unsigned sym_idx) {
        if (!sym_idx) return;

        // Bounds check: verify st_name offset is within reasonable range.
        // Symbol table size isn't directly parseable from .dynamic, so we
        // use a heuristic — st_name > 1MB would indicate a corrupt ELF.
        if (symtab[sym_idx].st_name > 0x100000) return;
        const char* sym_name = strtab + symtab[sym_idx].st_name;

        if (strcmp(sym_name, ctx->sym)) return;

        patch_one_got(ctx, info->dlpi_name, base + r_offset);
    };

    if (pltrel == DT_REL) {
        // 32-bit ARM/x86: REL entries (no addend).
        size_t cnt = pltsz / sizeof(ElfW(Rel));
        auto* rel = static_cast<const ElfW(Rel)*>(jmprel);
        for (size_t i = 0; i < cnt; i++)
            process_one(rel[i].r_offset, GHOST_ELF_R_SYM(rel[i].r_info));
    } else {
        // 64-bit: RELA entries.
        size_t cnt = pltsz / sizeof(ElfW(Rela));
        auto* rela = static_cast<const ElfW(Rela)*>(jmprel);
        for (size_t i = 0; i < cnt; i++)
            process_one(rela[i].r_offset, GHOST_ELF_R_SYM(rela[i].r_info));
    }
    return 0;
}

static bool patch_symbol(const char* sym, void* hook, void** orig) {
    void* real = dlsym(RTLD_DEFAULT, sym);
    if (!real) {
        LOGW("dlsym(%s) failed", sym);
        return false;
    }
    *orig = real;
    long ps = sysconf(_SC_PAGESIZE);
    GotCtx ctx{sym, real, hook, 0, ps};
    dl_iterate_phdr(got_cb, &ctx);
    LOGW("patch_symbol(%s): %d GOT entries patched", sym, ctx.patches);
    return ctx.patches > 0;
}

} // anonymous namespace

namespace {

bool g_prop_hook_done = false;
bool g_prop_hook_ok = false;

} // anonymous namespace

bool apply_property_hooks() {
    if (g_prop_hook_done) return g_prop_hook_ok;
    g_prop_hook_done = true;

    // Each entry point is independent: an app image may import any subset.
    // Success = at least one GOT entry actually rewritten. dlsym alone
    // succeeding means nothing — without a patch the spoof is inactive.
    bool any = false;
    any |= patch_symbol("__system_property_get",
                        reinterpret_cast<void*>(hooked_prop_get),
                        reinterpret_cast<void**>(&orig_prop_get));
    any |= patch_symbol("__system_property_read",
                        reinterpret_cast<void*>(hooked_prop_read),
                        reinterpret_cast<void**>(&orig_prop_read));
    any |= patch_symbol("__system_property_read_callback",
                        reinterpret_cast<void*>(hooked_prop_read_cb),
                        reinterpret_cast<void**>(&orig_prop_read_cb));
    // Poison find for debug markers (never fabricate prop_info).
    any |= patch_symbol("__system_property_find",
                        reinterpret_cast<void*>(hooked_prop_find),
                        reinterpret_cast<void**>(&orig_prop_find));
    // libcutils/native bool+long getters used by SystemProperties JNI.
    any |= patch_symbol("__system_property_get_bool",
                        reinterpret_cast<void*>(hooked_prop_get_bool),
                        reinterpret_cast<void**>(&orig_prop_get_bool));
    any |= patch_symbol("__system_property_get_long",
                        reinterpret_cast<void*>(hooked_prop_get_long),
                        reinterpret_cast<void**>(&orig_prop_get_long));
    g_prop_hook_ok = any;
    return g_prop_hook_ok;
}

void reset_property_hook_state() {
    g_prop_hook_done = false;
    g_prop_hook_ok = false;
}

} // namespace ghostboot
