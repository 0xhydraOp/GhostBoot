// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — RootShell.kt
// Single choke point for all root I/O. Exit codes are checked ( callers used
// to assume success), failures are logged instead of swallowed, and every
// write is one su invocation with its content piped on stdin.
// ─────────────────────────────────────────────────────────────────────────────
package com.ghostboot

import android.util.Log

object RootShell {

    private const val TAG = "GhostBoot"

    const val TARGETS_PATH = "/data/adb/ghostboot/targets.conf"
    const val SETTINGS_PATH = "/data/adb/ghostboot/settings.conf"

    fun hasRoot(): Boolean {
        return try {
            val p = Runtime.getRuntime().exec(arrayOf("su", "-c", "true"))
            p.waitFor() == 0
        } catch (e: Exception) {
            Log.w(TAG, "root check failed", e)
            false
        }
    }

    /** Write [content] to a root-owned [path]. Returns true on exit code 0. */
    fun writeFile(path: String, content: String): Boolean {
        return try {
            val dir = path.substringBeforeLast('/')
            val setup = "mkdir -p $dir && cat > $path && chmod 600 $path"
            val p = Runtime.getRuntime().exec(arrayOf("su", "-c", setup))
            p.outputStream.bufferedWriter().use { it.write(content) }
            val code = p.waitFor()
            if (code != 0) Log.w(TAG, "root write failed ($code): $path")
            code == 0
        } catch (e: Exception) {
            Log.w(TAG, "root write failed: $path", e)
            false
        }
    }

    /** Read a root-owned [path] via su. Null on any failure. */
    fun readFile(path: String): String? {
        return try {
            val p = Runtime.getRuntime().exec(arrayOf("su", "-c", "cat $path"))
            val out = p.inputStream.bufferedReader().readText()
            val code = p.waitFor()
            if (code == 0) out else null
        } catch (e: Exception) {
            Log.w(TAG, "root read failed: $path", e)
            null
        }
    }
}
