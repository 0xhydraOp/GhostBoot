// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — settings.gradle.kts
// ─────────────────────────────────────────────────────────────────────────────
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "GhostBoot"
include(":app")
