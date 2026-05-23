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
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ModuleBase;

class GhostBootModule : public ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        g_api = api;
        ghostboot::TargetConfig::instance().load();
        LOGW("onLoad: module loaded, targets=%zu",
             ghostboot::TargetConfig::instance().list().size());
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        if (!args->nice_name) return;

        if (!ghostboot::TargetConfig::instance().is_target(args->nice_name))
            return;

        should_hook_ = true;
        target_pkg_ = args->nice_name;
        LOGW("preAppSpecialize: targeting %s", args->nice_name);

        g_api->setOption(zygisk::Option::FORCE_DENYLIST_UNMOUNT);
    }

    void postAppSpecialize(const AppSpecializeArgs* /*args*/) override {
        if (!should_hook_) return;

        bool prop_ok = ghostboot::apply_property_hooks();
        bool mount_ok = ghostboot::apply_mount_hiding();
        LOGW("postAppSpecialize: props=%s mount=%s",
             prop_ok ? "ok" : "FAIL", mount_ok ? "ok" : "FAIL");
    }

private:
    static inline Api* g_api = nullptr;
    bool        should_hook_ = false;
    std::string target_pkg_;
};

REGISTER_ZYGISK_MODULE(GhostBootModule)
