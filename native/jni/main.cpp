// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — main.cpp
// Zygisk module entry.  Loads target list; only hooks selected packages.
// ─────────────────────────────────────────────────────────────────────────────
#include "ghostboot.hpp"
#include "zygisk.hpp"
#include <string>
#include <cstring>
#include <android/log.h>

#define TAG "GhostBoot"
// Stealth-aware: no logcat output at all when the user enables stealth mode.
#define LOGW(...) do { if (ghostboot::logging_enabled()) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__); } while (0)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ModuleBase;

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
