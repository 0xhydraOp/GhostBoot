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
