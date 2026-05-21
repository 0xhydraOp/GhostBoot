#!/system/bin/sh
# GhostBoot — customize.sh
# Runs during module install.  Verifies Zygisk, detects arch, sets up /data/adb/ghostboot.
set -eu

MODDIR=${0%/*}
WORK_DIR=/data/adb/ghostboot

log() { echo "[GhostBoot] $1"; }

check_zygisk() {
    # Check for running Zygisk daemon (Magisk built-in)
    ps -A 2>/dev/null | grep -q zygisk && { log "Zygisk daemon detected"; return 0; }
    # Check for ZygiskNext
    [ -d /data/adb/modules/zygisksu ] && { log "ZygiskNext detected"; return 0; }
    # Check Magisk Zygisk property
    [ "$(getprop persist.sys.zygisk.enable 2>/dev/null)" = "true" ] && { log "Magisk Zygisk property set"; return 0; }
    # Check if other Zygisk modules are active (implies Zygisk works)
    ls /data/adb/modules/zygisk_shamiko >/dev/null 2>&1 && { log "Zygisk active (Shamiko present)"; return 0; }
    log "WARNING: Zygisk not detected. Module won't function."
}

detect_arch() {
    case "$(getprop ro.product.cpu.abi 2>/dev/null)" in
        arm64-v8a) echo arm64-v8a ;;  armeabi*) echo armeabi-v7a ;;
        x86_64)    echo x86_64    ;;  x86)      echo x86 ;;
        *)         echo arm64-v8a ;;
    esac
}

setup_dirs() {
    mkdir -p "$WORK_DIR"
    chmod 700 "$WORK_DIR"
    chown root:root "$WORK_DIR" 2>/dev/null || true
    [ -f "$WORK_DIR/targets.conf" ] || { touch "$WORK_DIR/targets.conf"; chmod 600 "$WORK_DIR/targets.conf"; }
    log "Working dir: $WORK_DIR"
}

selinux_fix() {
    [ "$(getenforce 2>/dev/null)" = "Enforcing" ] && chcon -R u:object_r:magisk_file:s0 "$WORK_DIR" 2>/dev/null || true
}

log "GhostBoot v1.0 installing..."
check_zygisk
ARCH=$(detect_arch)
log "Architecture: $ARCH"
setup_dirs
selinux_fix
setprop persist.ghostboot.arch "$ARCH" 2>/dev/null || true
log "Done. Reboot to activate."
