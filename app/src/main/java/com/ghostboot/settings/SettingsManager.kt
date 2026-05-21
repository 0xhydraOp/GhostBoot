// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — SettingsManager.kt
// Persists user preferences via Jetpack DataStore.
// Mirrors the settings table from project_spec.md.
// ─────────────────────────────────────────────────────────────────────────────
package com.ghostboot.settings

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.*
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "ghostboot_settings")

data class GhostBootSettings(
    val bootloaderSpoof: Boolean          = true,
    val rootHide: RootHideLevel           = RootHideLevel.BASIC,
    val keyboxRotation: Boolean           = true,
    val stealthMode: Boolean              = false,
    val detectionMethod: DetectionMethod  = DetectionMethod.USAGE_STATS,
    val autoStartOnBoot: Boolean          = true,
    val notification: NotificationMode    = NotificationMode.ON,
    val lsposedHide: Boolean              = true     // because you're keeping LSPosed
)

enum class RootHideLevel { OFF, BASIC, AGGRESSIVE }
enum class DetectionMethod { USAGE_STATS, ACCESSIBILITY }
enum class NotificationMode { ON, OFF_, STEALTH }   // OFF_ avoids Kotlin keyword

class SettingsManager(private val context: Context) {

    companion object {
        private val KEY_BOOTLOADER  = booleanPreferencesKey("bootloader_spoof")
        private val KEY_ROOT_HIDE   = stringPreferencesKey("root_hide")
        private val KEY_KEYBOX      = booleanPreferencesKey("keybox_rotation")
        private val KEY_STEALTH     = booleanPreferencesKey("stealth_mode")
        private val KEY_DETECTION   = stringPreferencesKey("detection_method")
        private val KEY_AUTO_START  = booleanPreferencesKey("auto_start")
        private val KEY_NOTIFY      = stringPreferencesKey("notification")
        private val KEY_LSPOSED     = booleanPreferencesKey("lsposed_hide")
    }

    val settingsFlow: Flow<GhostBootSettings> = context.dataStore.data.map { prefs ->
        GhostBootSettings(
            bootloaderSpoof  = prefs[KEY_BOOTLOADER]  ?: true,
            rootHide         = parseEnum(prefs[KEY_ROOT_HIDE], RootHideLevel.BASIC),
            keyboxRotation   = prefs[KEY_KEYBOX]      ?: true,
            stealthMode      = prefs[KEY_STEALTH]     ?: false,
            detectionMethod  = parseEnum(prefs[KEY_DETECTION], DetectionMethod.USAGE_STATS),
            autoStartOnBoot  = prefs[KEY_AUTO_START]  ?: true,
            notification     = parseEnum(prefs[KEY_NOTIFY], NotificationMode.ON),
            lsposedHide      = prefs[KEY_LSPOSED]     ?: true
        )
    }

    private inline fun <reified T : Enum<T>> parseEnum(value: String?, default: T): T {
        if (value == null) return default
        return try { enumValueOf<T>(value) } catch (_: IllegalArgumentException) { default }
    }

    suspend fun updateBootloaderSpoof(on: Boolean) {
        context.dataStore.edit { it[KEY_BOOTLOADER] = on }
    }
    suspend fun updateRootHide(level: RootHideLevel) {
        context.dataStore.edit { it[KEY_ROOT_HIDE] = level.name }
    }
    suspend fun updateKeyboxRotation(on: Boolean) {
        context.dataStore.edit { it[KEY_KEYBOX] = on }
    }
    suspend fun updateStealthMode(on: Boolean) {
        context.dataStore.edit { it[KEY_STEALTH] = on }
    }
    suspend fun updateDetectionMethod(method: DetectionMethod) {
        context.dataStore.edit { it[KEY_DETECTION] = method.name }
    }
    suspend fun updateAutoStart(on: Boolean) {
        context.dataStore.edit { it[KEY_AUTO_START] = on }
    }
    suspend fun updateNotification(mode: NotificationMode) {
        context.dataStore.edit { it[KEY_NOTIFY] = mode.name }
    }
    suspend fun updateLsposedHide(on: Boolean) {
        context.dataStore.edit { it[KEY_LSPOSED] = on }
    }
}
