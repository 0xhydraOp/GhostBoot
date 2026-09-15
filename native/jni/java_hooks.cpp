// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — java_hooks.cpp
// JNI-level patch of android.os.Build static fields inside target apps.
//
// Why: the libc property hook covers native getprop readers, but
// android.os.Build.TAGS / .TYPE are Java statics initialized once at app
// start — often BEFORE our GOT hook lands. Any Kotlin/Java check like
// Build.TAGS.contains("test-keys") would walk straight past the native hook.
// Setting the fields via JNI closes that gap for the target process only.
// ─────────────────────────────────────────────────────────────────────────────
#include "ghostboot.hpp"

#include <jni.h>

namespace ghostboot {

bool apply_java_build_patch(void* java_vm) {
    auto* vm = static_cast<JavaVM*>(java_vm);
    if (!vm) return false;

    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        // postAppSpecialize normally runs on an attached thread; attach only
        // as a fallback (e.g. unusual ZygiskNext flows).
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || env == nullptr)
            return false;
        attached = true;
    }

    // android/os/Build is bootclasspath — FindClass works from any thread.
    // Full field set: TAGS/TYPE plus fingerprint/brand/device markers that
    // GPay/HDFC check via Build.FINGERPRINT.contains("test-keys").
    int patched = 0;
    const struct FieldPatch { const char* field; const char* value; } kFields[] = {
        {"TAGS",        "release-keys"},
        {"TYPE",        "user"},
        {"FINGERPRINT", "google/shamu/shamu:6.0.1/MMB29Q/2480792:user/release-keys"},
        {"BRAND",       "google"},
        {"DEVICE",      "shamu"},
        {"MANUFACTURER","motorola"},
        {"MODEL",       "Nexus 6"},
        {"PRODUCT",     "shamu"},
        {"HOST",        "abfarm"},
        {"USER",        "android-build"},
    };

    jclass build = env->FindClass("android/os/Build");
    if (build != nullptr && !env->ExceptionCheck()) {
        for (const auto& kv : kFields) {
            jfieldID fid = env->GetStaticFieldID(build, kv.field, "Ljava/lang/String;");
            if (fid == nullptr || env->ExceptionCheck()) { env->ExceptionClear(); continue; }
            jstring s = env->NewStringUTF(kv.value);
            if (s == nullptr || env->ExceptionCheck()) { env->ExceptionClear(); continue; }
            env->SetStaticObjectField(build, fid, s);
            if (env->ExceptionCheck()) env->ExceptionClear();
            else patched++;
            env->DeleteLocalRef(s);
        }
        env->DeleteLocalRef(build);
    } else {
        env->ExceptionClear();
    }

    if (attached) vm->DetachCurrentThread();
    return patched > 0;
}

} // namespace ghostboot
