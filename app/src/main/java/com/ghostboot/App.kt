// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — App.kt (Application class)
// Initialises DataStore and registers the foreground service notification
// channel on first launch.
// ─────────────────────────────────────────────────────────────────────────────
package com.ghostboot

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import android.os.Build

class App : Application() {

    companion object {
        const val CHANNEL_SERVICE  = "ghostboot_service"
        const val CHANNEL_STATUS   = "ghostboot_status"
        lateinit var instance: App
            private set
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
        createNotificationChannels()
    }

    private fun createNotificationChannels() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val nm = getSystemService(NotificationManager::class.java)

        nm.createNotificationChannel(
            NotificationChannel(
                CHANNEL_SERVICE,
                "GhostBoot Service",
                NotificationManager.IMPORTANCE_LOW
            ).apply { description = "Persistent notification while GhostBoot is active" }
        )
        nm.createNotificationChannel(
            NotificationChannel(
                CHANNEL_STATUS,
                "GhostBoot Status",
                NotificationManager.IMPORTANCE_DEFAULT
            ).apply { description = "Hook status and target app alerts" }
        )
    }
}
