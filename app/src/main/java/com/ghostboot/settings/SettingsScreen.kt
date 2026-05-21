// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — SettingsScreen.kt
// Jetpack Compose settings UI matching the spec table.
// ─────────────────────────────────────────────────────────────────────────────
package com.ghostboot.settings

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.ghostboot.ui.theme.GhostBootTheme
import kotlinx.coroutines.launch

class SettingsScreen : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            GhostBootTheme {
                SettingsUI()
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsUI() {
    val context = androidx.compose.ui.platform.LocalContext.current
    val manager = remember { SettingsManager(context.applicationContext) }
    val settings by manager.settingsFlow.collectAsStateWithLifecycle(initialValue = GhostBootSettings())
    val scope = rememberCoroutineScope()

    Scaffold(
        topBar = { TopAppBar(title = { Text("GhostBoot Settings") }) }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // Bootloader spoof
            SwitchRow("Bootloader spoof", settings.bootloaderSpoof) { checked ->
                scope.launch { manager.updateBootloaderSpoof(checked) }
            }

            // Root hide level
            Text("Root hide level", style = MaterialTheme.typography.titleSmall)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                RootHideLevel.entries.forEach { level ->
                    FilterChip(
                        selected = settings.rootHide == level,
                        onClick = { scope.launch { manager.updateRootHide(level) } },
                        label = { Text(level.name) }
                    )
                }
            }

            // LSPosed hide
            SwitchRow("LSPosed hide", settings.lsposedHide) { checked ->
                scope.launch { manager.updateLsposedHide(checked) }
            }

            // Keybox rotation
            SwitchRow("Keybox rotation", settings.keyboxRotation) { checked ->
                scope.launch { manager.updateKeyboxRotation(checked) }
            }

            // Stealth mode
            SwitchRow("Stealth mode", settings.stealthMode) { checked ->
                scope.launch { manager.updateStealthMode(checked) }
            }

            // Auto-start
            SwitchRow("Auto-start on boot", settings.autoStartOnBoot) { checked ->
                scope.launch { manager.updateAutoStart(checked) }
            }

            // Notification mode
            Text("Notification mode", style = MaterialTheme.typography.titleSmall)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NotificationMode.entries.forEach { mode ->
                    FilterChip(
                        selected = settings.notification == mode,
                        onClick = { scope.launch { manager.updateNotification(mode) } },
                        label = { Text(mode.name.removeSuffix("_")) }
                    )
                }
            }
        }
    }
}

@Composable
private fun SwitchRow(label: String, checked: Boolean, onToggle: (Boolean) -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, style = MaterialTheme.typography.bodyLarge)
        Switch(checked = checked, onCheckedChange = onToggle)
    }
}
