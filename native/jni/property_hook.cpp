// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — property_hook.cpp
// PLT / GOT hook on __system_property_get.
// Bootloader props return "locked / green" inside target apps.
//
// CRASH FIXES applied:
//   • mprotect return value checked — if page can't be made writable, skip
//   • __builtin___clear_cache removed — not needed for GOT data writes;
//     it triggers MTE/tagged-pointer aborts on ARM64 Android 14+
//   • Self-skip via dladdr to avoid patching our own GOT (infinite recursion)
// ─────────────────────────────────────────────────────────────────────────────
#include "ghostboot.hpp"

#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <elf.h>

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
    // any GOT entry is patched to point to this function
    if (orig_prop_get) return orig_prop_get(name, value);
    return -1;
}

// ── GOT patching ────────────────────────────────────────────────────────────
struct GotCtx { const char* sym; void* target; void* hook; int patches; };

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

    const ElfW(Rela)* jmprel = nullptr; size_t pltsz = 0;
    const ElfW(Sym)*  symtab = nullptr; const char* strtab = nullptr;
    for (; dyn->d_tag != DT_NULL; dyn++) {
        switch (dyn->d_tag) {
        case DT_JMPREL:   jmprel = reinterpret_cast<const ElfW(Rela)*>(base + dyn->d_un.d_ptr); break;
        case DT_PLTRELSZ: pltsz  = dyn->d_un.d_val; break;
        case DT_SYMTAB:   symtab = reinterpret_cast<const ElfW(Sym)*>(base + dyn->d_un.d_ptr); break;
        case DT_STRTAB:   strtab = reinterpret_cast<const char*>(base + dyn->d_un.d_ptr); break;
        }
    }
    if (!jmprel || !symtab || !strtab || !pltsz) return 0;

    size_t cnt = pltsz / sizeof(ElfW(Rela));
    for (size_t i = 0; i < cnt; i++) {
        auto* rel = &jmprel[i];
        auto sym_idx = GHOST_ELF_R_SYM(rel->r_info);
        if (!sym_idx) continue;

        // Bounds check: sym_idx must be within the symbol table
        // (The symbol table size isn't directly available, but 0 is invalid)
        const char* sym_name = strtab + symtab[sym_idx].st_name;

        if (strcmp(sym_name, ctx->sym)) continue;

        auto* got = reinterpret_cast<uintptr_t*>(base + rel->r_offset);

        // Read current GOT value BEFORE mprotect (verify it's readable)
        uintptr_t current = *got;
        if (current != reinterpret_cast<uintptr_t>(ctx->target)) continue;

        // Make the page writable — CHECK RETURN VALUE
        uintptr_t pg = reinterpret_cast<uintptr_t>(got) & ~uintptr_t(0xFFF);
        if (mprotect(reinterpret_cast<void*>(pg), 0x1000,
                     PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
            // mprotect failed (SELinux, seccomp, or memory protection).
            // Skip this GOT entry — a crash is worse than a missed hook.
            continue;
        }

        // Write our hook address
        *got = reinterpret_cast<uintptr_t>(ctx->hook);

        // Restore original permissions
        mprotect(reinterpret_cast<void*>(pg), 0x1000, PROT_READ | PROT_EXEC);

        // NOT calling __builtin___clear_cache here.
        // The GOT is data, not code.  On ARM64 Android 14+ with MTE /
        // tagged pointers, __builtin___clear_cache can trigger "Pointer
        // tag was truncated" aborts when given a data address that
        // happens to carry a hardware memory tag.  The data-cache
        // coherency is guaranteed by the mprotect calls above (which
        // include implicit DMB/DSB barriers on ARM64 Linux).

        ctx->patches++;
    }
    return 0;
}

static bool patch_symbol(const char* sym, void* hook, void** orig) {
    void* real = dlsym(RTLD_DEFAULT, sym);
    if (!real) return false;
    *orig = real;
    GotCtx ctx{sym, real, hook, 0};
    dl_iterate_phdr(got_cb, &ctx);
    return ctx.patches > 0;
}

} // anonymous namespace

bool apply_property_hooks() {
    static bool done = false;
    if (done) return (orig_prop_get != nullptr);
    done = true;

    patch_symbol("__system_property_get",
                 reinterpret_cast<void*>(hooked_prop_get),
                 reinterpret_cast<void**>(&orig_prop_get));
    return orig_prop_get != nullptr;
}

} // namespace ghostboot
