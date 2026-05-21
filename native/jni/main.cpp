// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — main.cpp
// Zygisk module entry.  Loads target list; only hooks selected packages.
// ─────────────────────────────────────────────────────────────────────────────
#include "ghostboot.hpp"
#include "zygisk.hpp"
#include <string>
#include <cstring>

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ModuleBase;

class GhostBootModule : public ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        g_api = api;
        g_env = env;
        ghostboot::TargetConfig::instance().load();
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        if (!args->nice_name) return;

        // ── PER-APP TARGETING ──────────────────────────────────────────
        if (!ghostboot::TargetConfig::instance().is_target(args->nice_name))
            return;

        should_hook_ = true;
        target_pkg_ = args->nice_name;

        g_api->setOption(zygisk::Option::FORCE_DENYLIST_UNMOUNT);
    }

    void postAppSpecialize(const AppSpecializeArgs* /*args*/) override {
        if (!should_hook_) return;

        ghostboot::apply_property_hooks();   // Layer 1: spoof bootloader
        ghostboot::apply_mount_hiding();     // Layer 2: hide root files
    }

private:
    static inline Api*    g_api = nullptr;
    static inline JNIEnv* g_env = nullptr;
    bool        should_hook_ = false;
    std::string target_pkg_;
};

REGISTER_ZYGISK_MODULE(GhostBootModule)
