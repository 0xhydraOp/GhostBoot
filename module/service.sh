#!/system/bin/sh
# GhostBoot — service.sh
# Post-boot: signals the companion app to register target packages.

# Wait for boot
i=0
while [ "$(getprop sys.boot_completed 2>/dev/null || true)" != "1" ] && [ $i -lt 120 ]; do
    sleep 1; i=$((i+1))
done

# Retry launching companion service (PackageManager may not be ready yet)
# NOTE: use -a (intent action), not --es (extra). The service and receiver
# match on intent.action, so --es extras were silently ignored (no-op boot).
for attempt in 1 2 3 4 5; do
    sleep 2
    am startservice -n com.ghostboot/.GhostBootService -a com.ghostboot.START >/dev/null 2>&1 && break
    am broadcast -n com.ghostboot/.BootReceiver -a com.ghostboot.BOOT_COMPLETE >/dev/null 2>&1 && break
done
