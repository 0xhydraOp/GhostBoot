// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — GhostBootService.kt
// Foreground service.  Syncs the target list to the native Zygisk module
// via root shell, keeps a persistent notification, and monitors app lifecycle.
// ─────────────────────────────────────────────────────────────────────────────
package com.ghostboot

import android.app.Notification
import android.app.PendingIntent
import android.app.Service
import android.app.usage.UsageEvents
import android.app.usage.UsageStatsManager
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import androidx.core.app.NotificationCompat

class GhostBootService : Service() {

    companion object {
        const val ACTION_START = "com.ghostboot.START"
        const val ACTION_STOP  = "com.ghostboot.STOP"
        const val NOTIFICATION_ID = 1001
    }

    private val handler = Handler(Looper.getMainLooper())
    private var targetCheckRunnable: Runnable? = null
    private var lastWrittenList: String? = null  // avoid redundant I/O

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> startGhostBoot()
            ACTION_STOP  -> stopSelf()
            "boot_complete" -> onBootComplete()
        }
        return START_STICKY
    }

    private fun startGhostBoot() {
        startForeground(NOTIFICATION_ID, buildNotification())
        syncTargets()
        startUsageMonitoring()
    }

    private fun onBootComplete() {
        startGhostBoot()
    }

    private fun syncTargets() {
        val prefs = getSharedPreferences("ghostboot_prefs", Context.MODE_PRIVATE)
        val targets = prefs.getStringSet("targets", emptySet()) ?: emptySet()
        if (targets.isEmpty()) return

        val list = targets.joinToString("\n") { it }
        // Skip write if list hasn't changed since last sync
        if (list == lastWrittenList) return

        // Run su in background thread — blocking main thread = ANR
        Thread {
            try {
                val cmd = "mkdir -p /data/adb/ghostboot && " +
                          "cat > /data/adb/ghostboot/targets.conf && " +
                          "chmod 600 /data/adb/ghostboot/targets.conf"
                val process = Runtime.getRuntime().exec(arrayOf("su", "-c", cmd))
                process.outputStream.bufferedWriter().use { writer ->
                    writer.write("# GhostBoot targets\n")
                    writer.write(list)
                    writer.write("\n")
                }
                process.waitFor()
                lastWrittenList = list
            } catch (_: Exception) { }
        }.start()
    }

    private fun startUsageMonitoring() {
        val runnable = object : Runnable {
            override fun run() {
                checkForegroundApp()
                handler.postDelayed(this, 60_000)
            }
        }
        targetCheckRunnable = runnable
        handler.postDelayed(runnable, 5_000)
    }

    private fun checkForegroundApp() {
        try {
            val usm = getSystemService(Context.USAGE_STATS_SERVICE) as? UsageStatsManager ?: return
            val now = System.currentTimeMillis()
            val events = usm.queryEvents(now - 60_000, now) ?: return
            var fgPkg: String? = null
            val ev = UsageEvents.Event()
            while (events.hasNextEvent()) {
                events.getNextEvent(ev)
                @Suppress("DEPRECATION")
                if (ev.eventType == UsageEvents.Event.MOVE_TO_FOREGROUND || 
                    ev.eventType == UsageEvents.Event.ACTIVITY_RESUMED) {
                    fgPkg = ev.packageName
                }
            }
            if (fgPkg != null) {
                val prefs = getSharedPreferences("ghostboot_prefs", Context.MODE_PRIVATE)
                val targets = prefs.getStringSet("targets", emptySet()) ?: emptySet()
                if (targets.contains(fgPkg)) {
                    syncTargets()
                }
            }
        } catch (_: Exception) { }
    }

    private fun buildNotification(): Notification {
        val openIntent = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, App.CHANNEL_SERVICE)
            .setContentTitle("GhostBoot Active")
            .setContentText("Protecting target apps")
            .setSmallIcon(android.R.drawable.ic_lock_lock)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setContentIntent(openIntent)
            .build()
    }

    override fun onDestroy() {
        targetCheckRunnable?.let { handler.removeCallbacks(it) }
        super.onDestroy()
    }
}
