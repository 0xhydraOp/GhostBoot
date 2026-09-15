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
import com.ghostboot.settings.SettingsManager
import com.ghostboot.settings.toConf
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking

class GhostBootService : Service() {

    companion object {
        const val ACTION_START = "com.ghostboot.START"
        const val ACTION_STOP  = "com.ghostboot.STOP"
        const val NOTIFICATION_ID = 1001
    }

    private val handler = Handler(Looper.getMainLooper())
    private var targetCheckRunnable: Runnable? = null
    private var lastWrittenList: String? = null  // avoid redundant I/O
    private var lastWrittenSettings: String? = null

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

        // Write even when empty: clearing all apps must clear the native
        // list, otherwise stale targets stay hooked after deselect.
        val list = targets.sorted().joinToString("\n")
        // Skip write if neither list nor settings changed since last sync
        if (list == lastWrittenList && lastWrittenSettings != null) return

        // Run su in background thread — blocking main thread = ANR
        Thread {
            val body = buildString {
                append("# GhostBoot targets\n")
                if (list.isNotEmpty()) append(list).append('\n')
            }
            if (RootShell.writeFile(RootShell.TARGETS_PATH, body)) lastWrittenList = list
            syncSettingsLocked()
        }.start()
    }

    // Must run on a background thread (blocks on DataStore + su).
    private fun syncSettingsLocked() {
        try {
            val s = runBlocking { SettingsManager(this@GhostBootService).settingsFlow.first() }
            val conf = s.toConf()
            if (conf == lastWrittenSettings) return
            if (RootShell.writeFile(RootShell.SETTINGS_PATH, conf)) lastWrittenSettings = conf
        } catch (e: Exception) {
            android.util.Log.w("GhostBoot", "settings sync failed", e)
        }
    }

    private fun startUsageMonitoring() {
        val runnable = object : Runnable {
            override fun run() {
                checkForegroundApp()
                // Watchdog only — syncs are event-driven (toggle/start), so a
                // 5-minute cadence is plenty and kinder to battery.
                handler.postDelayed(this, 300_000)
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
        // Notification mode comes from DataStore; OFF/STEALTH minimise the
        // foreground notification where the platform allows. Android 8+ still
        // requires a foreground notification for a foreground service, so
        // STEALTH uses LOW importance + minimal text rather than removal.
        val mode = try {
            runBlocking { SettingsManager(this@GhostBootService).settingsFlow.first() }.notification
        } catch (_: Exception) {
            com.ghostboot.settings.NotificationMode.ON
        }
        val openIntent = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val stealth = mode != com.ghostboot.settings.NotificationMode.ON
        return NotificationCompat.Builder(this, App.CHANNEL_SERVICE)
            .setContentTitle(if (stealth) "System Service" else "GhostBoot Active")
            .setContentText(if (stealth) "Running" else "Protecting target apps")
            .setSmallIcon(android.R.drawable.ic_lock_lock)
            .setOngoing(true)
            .setPriority(if (stealth) NotificationCompat.PRIORITY_MIN else NotificationCompat.PRIORITY_LOW)
            .setContentIntent(openIntent)
            .build()
    }

    override fun onDestroy() {
        targetCheckRunnable?.let { handler.removeCallbacks(it) }
        super.onDestroy()
    }
}
