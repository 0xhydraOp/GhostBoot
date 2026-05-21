#!/system/bin/sh
# GhostBoot — uninstall.sh
echo "[GhostBoot] Uninstalling..."
rm -rf /data/adb/ghostboot 2>/dev/null || true
setprop persist.ghostboot.arch "" 2>/dev/null || true
echo "[GhostBoot] Done."
