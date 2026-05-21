// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — Theme.kt
// Material 3 theme with a minimal, dark-friendly palette.
// ─────────────────────────────────────────────────────────────────────────────
package com.ghostboot.ui.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.*
import androidx.compose.runtime.Composable

private val darkColors = darkColorScheme(
    primary = androidx.compose.ui.graphics.Color(0xFF90CAF9),
    secondary = androidx.compose.ui.graphics.Color(0xFF80CBC4),
    background = androidx.compose.ui.graphics.Color(0xFF121212),
    surface = androidx.compose.ui.graphics.Color(0xFF1E1E1E),
)

private val lightColors = lightColorScheme(
    primary = androidx.compose.ui.graphics.Color(0xFF1565C0),
    secondary = androidx.compose.ui.graphics.Color(0xFF00897B),
)

@Composable
fun GhostBootTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    MaterialTheme(
        colorScheme = if (dark) darkColors else lightColors,
        typography = Typography(),
        content = content
    )
}
