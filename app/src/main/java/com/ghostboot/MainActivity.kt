// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — MainActivity.kt
// Main screen with target app picker and settings access.
// ─────────────────────────────────────────────────────────────────────────────
package com.ghostboot

import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Shield
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.ghostboot.settings.SettingsManager
import com.ghostboot.settings.SettingsScreen
import com.ghostboot.settings.toConf
import com.ghostboot.ui.theme.GhostBootTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.lifecycle.lifecycleScope

class MainActivity : ComponentActivity() {

    private val targetPackages = mutableStateListOf<String>()
    private val installedApps = mutableStateListOf<AppInfo>()
    private val appsLoading = mutableStateOf(false)
    private val verifyText = mutableStateOf("Not checked yet")

    private val notificationPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { /* granted or not — service still starts; notification may be hidden */ }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Load saved targets (from DataStore will sync later — stub for v1)
        val prefs = getSharedPreferences("ghostboot_prefs", MODE_PRIVATE)
        val saved = prefs.getStringSet("targets", emptySet()) ?: emptySet()
        targetPackages.addAll(saved)

        requestNotificationPermissionIfNeeded()

        setContent {
            GhostBootTheme {
                MainScreen(
                    targetPackages = targetPackages,
                    installedApps = installedApps,
                    appsLoading = appsLoading.value,
                    verifyText = verifyText.value,
                    onVerify = { runVerify() },
                    onToggleApp = { pkg, enabled ->
                        if (enabled) {
                            if (!targetPackages.contains(pkg)) targetPackages.add(pkg)
                        } else {
                            targetPackages.remove(pkg)
                        }
                        saveTargets()
                        syncTargetsToNative()
                    },
                    onOpenSettings = {
                        startActivity(Intent(this, SettingsScreen::class.java))
                    },
                    onOpenUsageAccess = { openUsageAccessSettings() },
                    onStartService = { startService() }
                )
            }
        }

        // PackageManager queries can take hundreds of ms — keep off the main thread.
        appsLoading.value = true
        lifecycleScope.launch {
            val apps = withContext(Dispatchers.IO) { loadInstalledApps() }
            installedApps.clear()
            installedApps.addAll(apps)
            appsLoading.value = false
        }
    }

    private fun requestNotificationPermissionIfNeeded() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) !=
                PackageManager.PERMISSION_GRANTED
            ) {
                notificationPermissionLauncher.launch(android.Manifest.permission.POST_NOTIFICATIONS)
            }
        }
    }

    private fun openUsageAccessSettings() {
        try {
            startActivity(Intent(android.provider.Settings.ACTION_USAGE_ACCESS_SETTINGS))
        } catch (_: Exception) { }
    }

    private fun loadInstalledApps(): List<AppInfo> {
        val pm = packageManager
        // System packages that must never be targeted (crash risk)
        val systemBlacklist = setOf("android", "com.android.", "com.google.android.gms")
        return pm.getInstalledApplications(PackageManager.GET_META_DATA)
            .filter { app ->
                val pkg = app.packageName
                if (systemBlacklist.any { pkg.startsWith(it) }) return@filter false
                app.flags and ApplicationInfo.FLAG_SYSTEM == 0 || isBankingApp(pkg)
            }
            .sortedBy { pm.getApplicationLabel(it).toString().lowercase() }
            .map { AppInfo(it.packageName, pm.getApplicationLabel(it).toString()) }
    }

    private fun isBankingApp(pkg: String): Boolean {
        // Exact package match — substring matching causes false positives
        val banking = setOf(
            "com.google.android.apps.nbu.paisa.user",   // Google Pay
            "com.phonepe.app",                            // PhonePe
            "net.one97.paytm",                            // Paytm
            "in.amazon.mShop.android.shopping",           // Amazon Pay
            "com.sbi.lotus",                              // SBI Yono
            "com.sbi.SBIFreedomPlus",                     // SBI Yono Lite
            "com.hdfc.retailbanking",                    // HDFC Bank
            "com.hdfcbank",                               // HDFC alternate
            "com.icici.netbanking",                      // ICICI iMobile
            "com.icici.bank",                             // ICICI alternate
            "in.org.npci.upi",                            // BHIM
            "com.idbi",                                   // IDBI
            "com.canarabank.mobility",                   // Canara Bank
            "com.pnb.mobile",                            // PNB
            "com.axis.mobile",                           // Axis Bank
            "com.kotak.kmbl",                            // Kotak
            "com.bankofbaroda.mobile"                    // Bank of Baroda
        )
        return pkg in banking
    }

    private fun saveTargets() {
        getSharedPreferences("ghostboot_prefs", MODE_PRIVATE)
            .edit()
            .putStringSet("targets", targetPackages.toSet())
            .apply()
    }

    private fun syncTargetsToNative() {
        // Write target list + current settings via root shell (background —
        // blocking the main thread here would ANR).
        val list = targetPackages.sorted()
        lifecycleScope.launch(Dispatchers.IO) {
            val body = buildString {
                append("# GhostBoot targets\n")
                if (list.isNotEmpty()) append(list.joinToString("\n")).append('\n')
            }
            RootShell.writeFile(RootShell.TARGETS_PATH, body)
            // Mirror settings so native gates stay fresh without reboot.
            try {
                val s = SettingsManager(applicationContext).settingsFlow.first()
                RootShell.writeFile(RootShell.SETTINGS_PATH, s.toConf())
            } catch (e: Exception) {
                android.util.Log.w("GhostBoot", "settings mirror failed", e)
            }
        }
    }

    // Diagnostics: what does the native side actually see on disk?
    private fun runVerify() {
        verifyText.value = "Checking..."
        lifecycleScope.launch(Dispatchers.IO) {
            val report = buildString {
                append(if (RootShell.hasRoot()) "root: OK (su granted)\n" else "root: MISSING — grant root to the app\n")
                val targets = RootShell.readFile(RootShell.TARGETS_PATH)
                if (targets == null) {
                    append("targets.conf: NOT READABLE\n")
                } else {
                    val entries = targets.lines()
                        .map { it.trim() }
                        .filter { it.isNotEmpty() && !it.startsWith("#") }
                    append("targets.conf: ${entries.size} app(s)\n")
                    entries.take(10).forEach { append("  • ").append(it).append('\n') }
                    if (entries.size > 10) append("  … +${entries.size - 10} more\n")
                }
                val conf = RootShell.readFile(RootShell.SETTINGS_PATH)
                if (conf == null) {
                    append("settings.conf: missing (native uses defaults: all ON)\n")
                } else {
                    append("settings.conf:\n")
                    conf.lines()
                        .map { it.trim() }
                        .filter { it.isNotEmpty() && !it.startsWith("#") }
                        .forEach { append("  ").append(it).append('\n') }
                }
            }
            withContext(Dispatchers.Main) { verifyText.value = report.trimEnd() }
        }
    }

    private fun startService() {
        startForegroundService(Intent(this, GhostBootService::class.java).apply {
            action = GhostBootService.ACTION_START
        })
    }
}

data class AppInfo(
    val packageName: String,
    val label: String
    // icon loaded lazily via remember { packageManager.getApplicationIcon(...) }
)

// ── Main Compose screen ─────────────────────────────────────────────────────
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(
    targetPackages: List<String>,
    installedApps: List<AppInfo>,
    appsLoading: Boolean = false,
    verifyText: String = "",
    onVerify: () -> Unit = {},
    onToggleApp: (String, Boolean) -> Unit,
    onOpenSettings: () -> Unit,
    onOpenUsageAccess: () -> Unit = {},
    onStartService: () -> Unit
) {
    var showAppPicker by remember { mutableStateOf(false) }
    var searchQuery by remember { mutableStateOf("") }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("GhostBoot") },
                actions = {
                    IconButton(onClick = onOpenSettings) {
                        Icon(Icons.Default.Settings, contentDescription = "Settings")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp)
        ) {
            // Status card
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = if (targetPackages.isNotEmpty())
                        MaterialTheme.colorScheme.primaryContainer
                    else
                        MaterialTheme.colorScheme.surfaceVariant
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.Shield, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text(
                            if (targetPackages.isNotEmpty()) "${targetPackages.size} apps protected"
                            else "No target apps selected",
                            style = MaterialTheme.typography.titleMedium
                        )
                    }
                }
            }

            Spacer(Modifier.height(16.dp))

            // App picker toggle
            Button(
                onClick = { showAppPicker = !showAppPicker },
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(if (showAppPicker) "Hide App List" else "Select Target Apps")
            }

            if (showAppPicker) {
                Spacer(Modifier.height(8.dp))
                if (appsLoading) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.Center,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        CircularProgressIndicator(modifier = Modifier.size(20.dp))
                        Spacer(Modifier.width(8.dp))
                        Text("Loading apps...")
                    }
                    Spacer(Modifier.height(8.dp))
                } else {
                OutlinedTextField(
                    value = searchQuery,
                    onValueChange = { searchQuery = it },
                    modifier = Modifier.fillMaxWidth(),
                    placeholder = { Text("Search apps...") },
                    singleLine = true
                )

                Spacer(Modifier.height(8.dp))

                val filtered = installedApps.filter {
                    searchQuery.isEmpty() ||
                    it.label.contains(searchQuery, ignoreCase = true) ||
                    it.packageName.contains(searchQuery, ignoreCase = true)
                }

                LazyColumn(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    items(filtered) { app ->
                        val isTarget = targetPackages.contains(app.packageName)
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { onToggleApp(app.packageName, !isTarget) },
                            color = if (isTarget) MaterialTheme.colorScheme.primaryContainer
                                    else MaterialTheme.colorScheme.surface
                        ) {
                            Row(
                                modifier = Modifier.padding(12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Checkbox(
                                    checked = isTarget,
                                    onCheckedChange = { onToggleApp(app.packageName, it) }
                                )
                                Spacer(Modifier.width(8.dp))
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(app.label, fontWeight = FontWeight.Medium)
                                    Text(
                                        app.packageName,
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                        }
                    }
                }
                }
            }

            Spacer(Modifier.height(16.dp))

            // Start service button
            Button(
                onClick = onStartService,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text("Start GhostBoot Service")
            }

            Spacer(Modifier.height(8.dp))

            // Usage Access grant — required for foreground-app monitoring
            OutlinedButton(
                onClick = onOpenUsageAccess,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text("Grant Usage Access")
            }

            Spacer(Modifier.height(8.dp))

            // Native status diagnostics — what the .so actually sees on disk
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Native status", style = MaterialTheme.typography.titleSmall)
                    Spacer(Modifier.height(4.dp))
                    Text(verifyText, style = MaterialTheme.typography.bodySmall)
                    Spacer(Modifier.height(8.dp))
                    OutlinedButton(
                        onClick = onVerify,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text("Verify native status")
                    }
                }
            }
        }
    }
}
