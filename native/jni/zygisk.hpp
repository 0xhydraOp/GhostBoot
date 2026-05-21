// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — zygisk.hpp
// Minimal Zygisk API header. Compatible with Magisk 26+ / ZygiskNext.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <jni.h>
#include <cstdint>

#define REGISTER_ZYGISK_MODULE(className)                                      \
    extern "C" __attribute__((visibility("default")))                          \
    ::zygisk::ModuleBase* zygisk_module_entry(::zygisk::Api* api, JNIEnv* env) { \
        auto* mod = new className();                                           \
        mod->onLoad(api, env);                                                 \
        return mod;                                                            \
    }

namespace zygisk {

enum class Option : uint32_t {
    FORCE_DENYLIST_UNMOUNT  = 1u << 0,
    DLCLOSE_POST_SPECIALIZE = 1u << 1,
    MOUNT_EXTERNAL_NONE     = 1u << 2,
};

struct AppSpecializeArgs {
    jint        uid               = -1;
    jint        gid               = -1;
    jint        gids_count        = 0;
    jint*       gids              = nullptr;
    jint*       fds_to_ignore     = nullptr;
    jint*       fds_to_close      = nullptr;
    bool        is_child_zygote   = false;
    bool        is_mount_external = false;
    const char* nice_name         = nullptr;
    const char* app_data_dir      = nullptr;
    const char* instruction_set   = nullptr;
    jint        mount_external    = 0;
    jint        runtime_flags     = 0;
};

struct ServerSpecializeArgs {
    jint uid = -1;
    jint gid = -1;
};

class Api {
public:
    virtual ~Api() = default;
    virtual void setOption(Option opt) = 0;
    virtual int  connectCompanion() = 0;
    virtual void* getModuleDir() = 0;
    virtual int  getModuleId() = 0;
};

class ModuleBase {
public:
    virtual ~ModuleBase() = default;
    virtual void onLoad(Api* api, JNIEnv* env) {}
    virtual void preAppSpecialize(AppSpecializeArgs* args) {}
    virtual void postAppSpecialize(const AppSpecializeArgs* args) {}
    virtual void preServerSpecialize(ServerSpecializeArgs* args) {}
    virtual void postServerSpecialize(const ServerSpecializeArgs* args) {}
};

} // namespace zygisk
