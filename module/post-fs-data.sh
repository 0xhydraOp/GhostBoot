#!/system/bin/sh
# GhostBoot — post-fs-data.sh
# Runs early boot.  Ensures working dir survives.  Zygisk .so does the real work.

mkdir -p /data/adb/ghostboot
chmod 700 /data/adb/ghostboot
touch /data/adb/ghostboot/targets.conf
chmod 600 /data/adb/ghostboot/targets.conf

[ "$(getenforce 2>/dev/null)" = "Enforcing" ] && chcon -R u:object_r:magisk_file:s0 /data/adb/ghostboot 2>/dev/null || true
