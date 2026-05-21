# GhostBoot

**Per-app bootloader spoof & root hide for Android 10–15.**

GhostBoot is a Magisk Zygisk module that makes banking apps believe your device is running **stock, locked-firmware Android** — even with Magisk, LSPosed, and custom kernels installed.

## How it works

GhostBoot activates **only for the apps you select**. When a target app launches:

1. **Property hook** — PLT/GOT hook on `__system_property_get` in libc spoofs bootloader properties inside the target process
2. **Mount namespace isolation** — `unshare(CLONE_NEWNS)` creates a private mount namespace where `/data/adb`, `su` binaries, and root indicators are replaced with empty tmpfs
3. **Zero trace** — both layers die when the process exits; nothing persists to disk

```
Target App Process
 ┌──────────────────────────────────────────┐
 │  __system_property_get()                 │
 │  └→ "ro.boot.verifiedbootstate" = green  │
 │  └→ "ro.boot.flash.locked"    = 1        │
 │  ┌─────────────────────────────────────── │
 │  │ /data/adb   → empty tmpfs             │
 │  │ /sbin/su    → /dev/null               │
 │  └─────────────────────────────────────── │
 └──────────────────────────────────────────┘
```

## What gets spoofed

### Bootloader properties
| Real value | Spoofed to |
|---|---|
| `ro.boot.verifiedbootstate` = ORANGE | **green** |
| `ro.boot.flash.locked` = 0 | **1** |
| `ro.boot.vbmeta.device_state` = unlocked | **locked** |
| `ro.boot.veritymode` = disabled | **enforcing** |
| `ro.build.tags` = test-keys | **release-keys** |
| `ro.build.type` = userdebug | **user** |
| `ro.debuggable` = 1 | **0** |

### Filesystem (per-app mount namespace)
- `/data/adb` and all subdirectories (Magisk, modules, lspd, riru)
- `/sbin/.magisk`
- `/debug_ramdisk/.magisk`
- All `su` binary paths (8 locations)
- Known root app directories

## Requirements

- **Root**: Magisk 26.0+ with Zygisk enabled, or KernelSU 0.9+ with ZygiskNext, or APatch
- **Android**: 10–15 (API 29–35)
- **Architecture**: arm64-v8a, armeabi-v7a, x86_64, x86

## Quick install

1. Download `ghostboot-v1.0.zip` from the latest release
2. Flash in your Magisk / KernelSU / APatch manager
3. Reboot
4. Install `ghostboot-companion-debug.apk` from the release
5. Open GhostBoot → select your banking apps → Start Service
6. Grant **Usage Access** permission when prompted

## Companion app

The companion app manages your target list and writes it to `/data/adb/ghostboot/targets.conf` via root shell.

| Feature | Detail |
|---|---|
| App picker | Shows installed apps; search/filter supported |
| One-tap start | Foreground service with persistent notification |
| Settings | Bootloader spoof, root hide level, LSPosed hide, auto-start |
| Boot receiver | Auto-starts service after reboot |

## Building from source

```bash
# Prerequisites: Android NDK r27+, JDK 17, Android SDK (API 35)

# Build native Zygisk .so (all 4 ABIs)
cd native
set HOST_OS=windows  # or: export HOST_OS=linux
ndk-build APP_BUILD_SCRIPT=jni/Android.mk -j4

# Build companion APK
cd ..
gradlew assembleDebug

# Package Magisk module zip
# .so files must be under module/zygisk/ before packaging
cd module && zip -r ../release/ghostboot-v1.0.zip . -x "*.DS_Store"
```

## Architecture

```
GhostBoot/
├── module/                 # Magisk module files
│   ├── module.prop
│   ├── customize.sh        # Install-time setup
│   ├── post-fs-data.sh     # Early-boot setup
│   ├── service.sh          # Post-boot service launch
│   ├── uninstall.sh
│   └── zygisk/             # Output: per-ABI .so files
├── native/jni/             # C++ Zygisk source
│   ├── main.cpp            # Zygisk entry point, per-app gating
│   ├── property_hook.cpp   # PLT/GOT hook on __system_property_get
│   ├── mount_ns.cpp        # unshare + bind-mount hiding
│   ├── config.cpp          # Target list read/write
│   ├── ghostboot.hpp       # Spoof tables + config API
│   └── zygisk.hpp          # Zygisk API v4 header
├── app/                    # Kotlin companion app (Jetpack Compose)
│   └── src/main/java/com/ghostboot/
│       ├── MainActivity.kt
│       ├── GhostBootService.kt
│       ├── BootReceiver.kt
│       └── settings/
├── build.gradle.kts
├── gradlew.bat
└── release/                # Output APKs + module zip
```

## Tested devices

| Device | Android | Magisk | Status |
|---|---|---|---|
| Realme Narzo 70 Turbo | 15 | 30.7 | ✓ Working |
| (more devices welcome — open an issue) | | | |

## Tested apps

| App | Package | Status |
|---|---|---|
| PhonePe | com.phonepe.app | ✓ Working |
| Google Pay | com.google.android.apps.nbu.paisa.user | ✓ Working |
| Paytm | net.one97.paytm | ✓ Working |
| Amazon Pay | in.amazon.mShop.android.shopping | ✓ Working |
| BHIM | in.org.npci.upi | Testing |
| SBI Yono | com.sbi.lotus | Testing |
| HDFC Bank | com.hdfc.retailbanking | Testing |
| ICICI iMobile | com.icici.netbanking | Testing |

## Security & Privacy

- **No internet permission** — the companion app cannot connect to any server
- **No logging** — bank credentials are never recorded
- **No cloud** — all files stored locally on device
- **Source-available** — full source for audit
- **Per-process** — hooks apply only to selected apps; system processes untouched

## Limitations

- Does **not** spoof hardware IDs (IMEI, serial, MAC) — use your existing LSPosed module
- Does **not** provide Play Integrity **STRONG** verdict — that requires a valid hardware-backed keybox
- Does **not** hide Zygisk itself — use Shamiko or Zygisk Assistant alongside GhostBoot
- May conflict with other Zygisk modules that hook the same functions

## Disclaimer

This project is for **educational and legitimate privacy purposes**. Using it to bypass banking app root detection may violate their Terms of Service and result in account restrictions. **The user assumes all responsibility.**

## License

MIT — see [LICENSE](LICENSE)
