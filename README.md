# GhostBoot

**Per-app bootloader spoof & root hide for Android 10–15.**

GhostBoot is a Magisk Zygisk module that makes selected apps believe your device
is running **stock, locked-firmware Android** — even with Magisk, LSPosed, and
custom kernels installed.

## How it works

GhostBoot activates **only for the apps you select**. When a target app launches,
the Zygisk module applies up to three layers inside that process only:

1. **Property hooks** — PLT/GOT hooks on `__system_property_get`,
   `__system_property_read`, and `__system_property_read_callback` in libc
   spoof bootloader/build properties (both 64-bit RELA and 32-bit REL PLTs,
   runtime page size for 16K-page devices)
2. **Java Build patch** — JNI patch of `android.os.Build.TAGS` / `.TYPE`, which
   are cached Java statics that a libc hook alone arrives too late to cover
3. **Mount namespace isolation** — `unshare(CLONE_NEWNS)` creates a private
   mount namespace where `/data/adb`, `su` binaries, and root-manager dirs are
   replaced with empty tmpfs; filtered memfd snapshots of
   `/proc/self/mounts|mountinfo` (plus `maps`/`smaps`, cmdline, and
   `packages.list` on Aggressive) hide the hiding itself
4. **Zero trace** — all layers die when the process exits; nothing persists

```
Target App Process
 ┌──────────────────────────────────────────┐
 │  __system_property_get/read()            │
 │  └→ "ro.boot.verifiedbootstate" = green  │
 │  android.os.Build.TAGS = "release-keys"  │
 │  ┌────────────────────────────────────── │
 │  │ /data/adb   → empty tmpfs             │
 │  │ /sbin/su    → /dev/null               │
 │  │ /proc/self/mounts → filtered snapshot │
 │  └────────────────────────────────────── │
 └──────────────────────────────────────────┘
```

## What gets spoofed

### Bootloader / build properties (native + Java)
| Real value | Spoofed to |
|---|---|
| `ro.boot.verifiedbootstate` = orange | **green** |
| `ro.boot.flash.locked` = 0 | **1** |
| `ro.boot.vbmeta.device_state` = unlocked | **locked** |
| `ro.boot.veritymode` = disabled | **enforcing** |
| `ro.boot.warranty_bit` = 1 | **0** |
| `ro.boot.secureboot` / `ro.boot.selinux` | **1** / **enforcing** |
| `ro.build.tags` = test-keys | **release-keys** |
| `ro.build.type` = userdebug | **user** |
| `ro.debuggable` = 1 | **0** |
| `ro.secure` = 0 | **1** |

`__system_property_find` is intentionally not hooked (fabricating a
`prop_info` is unsafe); get/read/callback coverage is sufficient.

### Filesystem (per-app mount namespace)
- `/data/adb` and all subdirectories (Magisk, modules, lspd, riru)
- `/sbin/.magisk`, `/debug_ramdisk/.magisk`
- All `su` binary paths (8 locations)
- Root-manager data dirs (Magisk, LSPosed/Xposed managers) and `priv-app` entries
- Riru misc traces (`/data/misc/riru*`)

## Companion app

Manages targets and settings, syncing them to the native side
(`/data/adb/ghostboot/targets.conf` + `settings.conf`) via root shell —
no reboot needed, the `.so` re-reads on each app fork.

| Feature | Detail |
|---|---|
| App picker | Installed apps, search/filter, system-package blacklist |
| One-tap start | Foreground service with persistent notification |
| Settings | Bootloader spoof, root hide (Off/Basic/Aggressive), LSPosed hide, stealth mode — enforced natively |
| Verify card | Reads back root state + both conf files: what the `.so` actually sees |
| Boot receiver | Auto-starts after unlock (direct-boot safe: ignores `LOCKED_BOOT_COMPLETED`) |
| Permissions | Requests Usage Access (foreground monitoring) + notifications (Android 13+) at runtime |

Settings marked *reserved* in the UI (keybox rotation, detection method,
notification mode, auto-start toggle) are stored but not yet enforced — see
`project_spec.md`.

## Requirements

- **Root**: Magisk 26.0+ with Zygisk enabled, or KernelSU 0.9+ with ZygiskNext, or APatch
- **Android**: 10–15 (API 29–35)
- **Architecture**: arm64-v8a, armeabi-v7a, x86_64, x86 (all prebuilt in `module/zygisk/`)
- **Build**: Android NDK r27+ (tested 28.2), JDK 17, Android SDK (API 35), Gradle 8.5+

## Quick install

1. Download `ghostboot-v1.0.2.zip` and `ghostboot-companion-debug.apk` from the latest release
2. Flash the zip in your Magisk / KernelSU / APatch manager
3. Reboot
4. Install the companion APK
5. Open GhostBoot → grant root → select banking apps → Start Service
6. Grant **Usage Access** (button in-app) and notifications when prompted
7. Use **Verify native status** to confirm `targets.conf` / `settings.conf` on device

## Building from source

```bash
# 1. Native Zygisk .so (all 4 ABIs) — needs ANDROID_NDK_HOME or ndk-build in PATH
cd native
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk -j4
# .so files land in native/libs/<abi>/ — copy each to module/zygisk/<abi>.so

# Windows (PowerShell):
# & "$env:LOCALAPPDATA\Android\Sdk\ndk\<ver>\ndk-build.cmd" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk -j4

# 2. Companion APK — needs local.properties with sdk.dir pointing at your SDK
cd ..
./gradlew :app:assembleDebug        # Linux/macOS
# gradlew.bat :app:assembleDebug    # Windows (or a local Gradle 8.7+ install)

# 3. Package Magisk module zip (Linux/macOS helper; on Windows use Compress-Archive)
mkdir -p release
cp app/build/outputs/apk/debug/app-debug.apk release/ghostboot-companion-debug.apk
cd module && zip -r ../release/ghostboot-v1.0.2.zip . -x "*.DS_Store"
```

## Architecture

```
GhostBoot/
├── module/                 # Magisk module files (flashable)
│   ├── module.prop         # v1.0.2, versionCode 102
│   ├── customize.sh        # Install-time setup
│   ├── post-fs-data.sh     # Early-boot setup
│   ├── service.sh          # Post-boot companion launch
│   ├── uninstall.sh
│   └── zygisk/             # Prebuilt .so per ABI (tracked)
├── native/jni/             # C++ Zygisk source (NDK, -fno-rtti -fno-exceptions)
│   ├── main.cpp            # Zygisk entry, per-app gating, settings gates
│   ├── property_hook.cpp   # PLT/GOT hooks: get + read + read_callback
│   ├── java_hooks.cpp      # JNI android.os.Build static-field patch
│   ├── mount_ns.cpp        # unshare + bind-mount hiding
│   ├── proc_filter.cpp     # memfd proc snapshots (mounts/maps/cmdline/pkgs)
│   ├── config.cpp          # targets.conf + settings.conf reader/writer
│   ├── ghostboot.hpp       # Spoof tables + config/settings API
│   └── zygisk.hpp          # Zygisk API header
├── app/                    # Kotlin companion app (Jetpack Compose)
│   └── src/main/java/com/ghostboot/
│       ├── MainActivity.kt      # Picker + Verify + permission flows
│       ├── GhostBootService.kt  # Foreground service + native sync
│       ├── RootShell.kt         # Root I/O choke point (checked exit codes)
│       ├── BootReceiver.kt      # Direct-boot-aware auto-start
│       └── settings/            # DataStore settings + UI (+ .toConf())
├── release/                # Local build outputs (gitignored)
└── project_spec.md         # Full technical specification
```

## Tested devices

| Device | Android | Magisk | Status |
|---|---|---|---|
| Realme Narzo 70 Turbo | 15 | 30.7 | ✓ Working (v1.0 baseline) |
| (more devices welcome — open an issue) | | | |

## Tested apps

| App | Package | Status |
|---|---|---|
| PhonePe | com.phonepe.app | ✓ Working (v1.0 baseline) |
| Google Pay | com.google.android.apps.nbu.paisa.user | ✓ Working (v1.0 baseline) |
| Paytm | net.one97.paytm | ✓ Working (v1.0 baseline) |
| Amazon Pay | in.amazon.mShop.android.shopping | ✓ Working (v1.0 baseline) |
| BHIM / SBI Yono / HDFC / ICICI | see `MainActivity.kt` list | Testing |

## Security & Privacy

- **No internet permission** — the companion app cannot connect to any server
- **No logging in stealth mode** — all native logcat output is gated by the stealth toggle
- **No cloud** — all files stored locally on device
- **Source-available** — full source for audit
- **Per-process** — hooks apply only to selected apps; system processes untouched

## Limitations

- Does **not** spoof hardware IDs (IMEI, serial, MAC) — use your existing LSPosed module
- Does **not** provide Play Integrity **STRONG** verdict — that requires a valid hardware-backed keybox (separate KeyMint bridge, e.g. TrickyStore-style)
- Does **not** hide `PackageManager` queries at framework level — pair with Shamiko/DenyList for package-visibility checks
- `maps`/`smaps` snapshots on Aggressive trade live accuracy for stealth (debuggers in target may see stale mappings)
- May conflict with other Zygisk modules that hook the same functions

## Disclaimer

This project is for **educational and legitimate privacy purposes**. Using it to bypass banking app root detection may violate their Terms of Service and result in account restrictions. **The user assumes all responsibility.**

## License

MIT — see [LICENSE](LICENSE)
