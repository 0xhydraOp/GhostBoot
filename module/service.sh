#!/system/bin/sh
# GhostBoot — service.sh
# Post-boot: signals the companion app to register target packages.

# Wait for boot
i=0
while [ "$(getprop sys.boot_completed 2>/dev/null)" != "1" ] && [ $i -lt 120 ]; do
    sleep 1; i=$((i+1))
done

sleep 2

# Launch companion service
am startservice -n com.ghostboot/.GhostBootService --es action boot_complete >/dev/null 2>&1 || true
am broadcast -n com.ghostboot/.BootReceiver --es action boot_complete >/dev/null 2>&1 || true
