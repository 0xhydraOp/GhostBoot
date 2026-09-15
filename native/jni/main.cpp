// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — main.cpp
// Zygisk module entry.  Loads target list; only hooks selected packages.
// ─────────────────────────────────────────────────────────────────────────────
#include "ghostboot.hpp"
#include "zygisk.hpp"
#include <string>
#include <cstring>
#include <dlfcn.h>
#include <sys/prctl.h>
#include <android/log.h>

// TAG is built at runtime (not a plain .rodata string) so a trivial
// `strings libghostboot.so | grep -i ghost` doesn't fingerprint the module.
#define TAG ghostboot::log_tag()
// Stealth-aware: no logcat output at all when the user enables stealth mode.
#define LOGW(...) do { if (ghostboot::logging_enabled()) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__); } while (0)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ModuleBase;

#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#endif
#ifndef PR_SET_VMA_ANON_NAME
#define PR_SET_VMA_ANON_NAME 0
#endif

// Best-effort: rename our own .so VMA to look like a generic libc mapping
// on kernels that support PR_SET_VMA_ANON_NAME (5.17+). Never fatal.
static void disguise_self_vma() {
#ifdef __linux__
    Dl_info info;
    if (!dladdr(reinterpret_cast<void*>(&disguise_self_vma), &info) || !info.dli_fbase)
        return;
    // prctl(PR_SET_VMA, ANON_NAME, addr, len, name). len must be > 0, so
    // cover a large span; the kernel clamps to the actual VMA.
    const char* alias = "[anon:libc.so]";
    prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME,
          reinterpret_cast<unsigned long>(info.dli_fbase),
          static_cast<unsigned long>(8u << 20),
          reinterpret_cast<unsigned long>(alias));
#endif
}

class GhostBootModule : public ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        g_api = api;
        if (env) env->GetJavaVM(&g_vm);
        ghostboot::TargetConfig::instance().load();
        ghostboot::reload_settings();
        LOGW("onLoad: module loaded, targets=%zu",
             ghostboot::TargetConfig::instance().list().size());
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        // Settings are re-read per fork so a toggle applies to the next
        // launched target without reboot. Cheap (single small file).
        ghostboot::reload_settings();
        // Reset per-fork: module instance survives across zygote forks,
        // a previous targeted app must not arm hooking for the next one.
        // Also reset the property-hook done-cache so a failed first target
        // doesn't poison later forks.
        ghostboot::reset_property_hook_state();
        should_hook_ = false;
        target_pkg_.clear();
        if (!args->nice_name) return;

        // Strip :process suffix (e.g. com.phonepe.app:push -> com.phonepe.app)
        // so secondary processes of a target are still hooked.
        const char* nice = args->nice_name;
        std::string pkg(nice);
        auto colon = pkg.find(':');
        if (colon != std::string::npos) pkg.resize(colon);

        if (!ghostboot::TargetConfig::instance().is_target(pkg.c_str()))
            return;

        should_hook_ = true;
        target_pkg_ = pkg;
        LOGW("preAppSpecialize: targeting %s (proc %s)", pkg.c_str(), nice);

        g_api->setOption(zygisk::Option::FORCE_DENYLIST_UNMOUNT);
    }

    void postAppSpecialize(const AppSpecializeArgs* /*args*/) override {
        if (!should_hook_) return;

        const auto s = ghostboot::settings();
        bool prop_ok = true, mount_ok = true, java_ok = true;
        if (s.bootloader_spoof) {
            prop_ok = ghostboot::apply_property_hooks();
            java_ok = ghostboot::apply_java_build_patch(g_vm);
        }
        if (s.root_hide != ghostboot::RootHideLevel::Off)
            mount_ok = ghostboot::apply_mount_hiding();
        // Hide our own mapping after hooks are installed (best-effort).
        disguise_self_vma();
        LOGW("postAppSpecialize: props=%s java=%s mount=%s",
             prop_ok ? "ok" : "FAIL", java_ok ? "ok" : "FAIL",
             mount_ok ? "ok" : "FAIL");
    }

private:
    static inline Api* g_api = nullptr;
    static inline JavaVM* g_vm = nullptr;
    bool        should_hook_ = false;
    std::string target_pkg_;
};

REGISTER_ZYGISK_MODULE(GhostBootModule)
