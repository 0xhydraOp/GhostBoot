# GhostBoot ProGuard Rules
# Keep DataStore preferences keys (serialized by name)
-keepclassmembers class com.ghostboot.settings.SettingsManager$Companion {
    private static final androidx.datastore.preferences.core.Preferences$Key *;
}

# Keep Compose functions and state
-keepclassmembers class * {
    @androidx.compose.runtime.Composable <methods>;
}

# Keep application class
-keep class com.ghostboot.App { *; }

# Keep data classes used in serialization
-keep class com.ghostboot.AppInfo { *; }
-keep class com.ghostboot.settings.GhostBootSettings { *; }
-keep class com.ghostboot.settings.RootHideLevel { *; }
-keep class com.ghostboot.settings.DetectionMethod { *; }
-keep class com.ghostboot.settings.NotificationMode { *; }

# Keep BroadcastReceiver registered in manifest
-keep class com.ghostboot.BootReceiver { *; }
