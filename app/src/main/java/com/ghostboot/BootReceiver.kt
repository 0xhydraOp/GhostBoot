// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — BootReceiver.kt
// Receives BOOT_COMPLETED and launches GhostBootService.
// ─────────────────────────────────────────────────────────────────────────────
package com.ghostboot

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        // Direct Boot: credential-protected prefs/DataStore aren't available
        // yet — do nothing and let BOOT_COMPLETED (after unlock) do the work.
        if (intent.action == Intent.ACTION_LOCKED_BOOT_COMPLETED) return

        if (intent.action == Intent.ACTION_BOOT_COMPLETED ||
            intent.action == "com.ghostboot.BOOT_COMPLETE") {

            val serviceIntent = Intent(context, GhostBootService::class.java).apply {
                action = "boot_complete"
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(serviceIntent)
            } else {
                context.startService(serviceIntent)
            }
        }
    }
}
