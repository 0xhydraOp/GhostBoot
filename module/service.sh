#!/system/bin/sh
# GhostBoot — service.sh
# Post-boot: signals the companion app to register target packages.

# Wait for boot
i=0
while [ "$(getprop sys.boot_completed 2>/dev/null || true)" != "1" ] && [ $i -lt 120 ]; do
    sleep 1; i=$((i+1))
done

# Retry launching companion service (PackageManager may not be ready yet)
for attempt in 1 2 3 4 5; do
    sleep 2
    am startservice -n com.ghostboot/.GhostBootService --es action boot_complete >/dev/null 2>&1 && break
    am broadcast -n com.ghostboot/.BootReceiver --es action boot_complete >/dev/null 2>&1 && break
done
