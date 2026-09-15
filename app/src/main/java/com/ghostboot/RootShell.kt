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
    fun writeFile(path: String, content: String, timeoutMs: Long = 8000): Boolean {
        if (!isAllowedPath(path)) { Log.w(TAG, "root write blocked: $path"); return false }
        return try {
            val dir = path.substringBeforeLast('/')
            // Single-quoted paths: content flows on stdin, never in argv.
            val setup = "mkdir -p '$dir' && cat > '$path' && chmod 600 '$path'"
            val p = Runtime.getRuntime().exec(arrayOf("su", "-c", setup))
            p.outputStream.bufferedWriter().use { it.write(content) }
            val finished = p.waitFor(timeoutMs, java.util.concurrent.TimeUnit.MILLISECONDS)
            if (!finished) { p.destroyForcibly(); Log.w(TAG, "root write timeout: $path"); return false }
            val code = p.exitValue()
            if (code != 0) Log.w(TAG, "root write failed ($code): $path")
            code == 0
        } catch (e: Exception) {
            Log.w(TAG, "root write failed: $path", e)
            false
        }
    }

    /** Read a root-owned [path] via su. Null on any failure. */
    fun readFile(path: String, timeoutMs: Long = 8000): String? {
        if (!isAllowedPath(path)) { Log.w(TAG, "root read blocked: $path"); return null }
        return try {
            val p = Runtime.getRuntime().exec(arrayOf("su", "-c", "cat -- '$path'"))
            val out = p.inputStream.bufferedReader().readText()
            val finished = p.waitFor(timeoutMs, java.util.concurrent.TimeUnit.MILLISECONDS)
            if (!finished) { p.destroyForcibly(); Log.w(TAG, "root read timeout: $path"); return null }
            val code = p.exitValue()
            if (code == 0) out else null
        } catch (e: Exception) {
            Log.w(TAG, "root read failed: $path", e)
            null
        }
    }

    private val PID_RE = Regex("[0-9]+")

    /** Package/pid-safe probe: pid + nsenter reads for a target package.
     * Sanitizes pkg to alphanumerics+dot+underscore so no shell
     * interpolation is possible. Returns Triple(pid, verifiedbootstate, adb). */
    fun probeTarget(pkg: String, timeoutMs: Long = 8000): Triple<String, String, String> {
        val safe = pkg.filter { it.isLetterOrDigit() || it == '.' || it == '_' }
        if (safe.isEmpty() || safe.length > 256) return Triple("", "", "")
        val pid = suOut("pidof $safe 2>/dev/null", timeoutMs).split(' ').firstOrNull().orEmpty()
        if (!PID_RE.matches(pid)) return Triple("", "", "")
        val prop = suOut("nsenter --mount -t $pid getprop ro.boot.verifiedbootstate 2>/dev/null", timeoutMs)
        val adb = suOut("nsenter --mount -t $pid ls /data/adb 2>&1 | head -3", timeoutMs)
        return Triple(pid, prop, adb)
    }

    private fun suOut(cmd: String, timeoutMs: Long): String {
        return try {
            if (cmd.length > 512) return ""
            val p = Runtime.getRuntime().exec(arrayOf("su", "-c", cmd))
            val out = p.inputStream.bufferedReader().readText()
            val ok = p.waitFor(timeoutMs, java.util.concurrent.TimeUnit.MILLISECONDS)
            if (!ok) { p.destroyForcibly(); return "" }
            out.trim()
        } catch (_: Exception) { "" }
    }

    private fun isAllowedPath(path: String): Boolean {
        if (!path.startsWith("/data/adb/ghostboot/")) return false
        if (path.contains("..") || path.contains("\n") || path.contains("'") ||
            path.contains("\"") || path.contains(";") || path.contains("&") ||
            path.contains("|") || path.contains("`") || path.contains("$")) return false
        return path == TARGETS_PATH || path == SETTINGS_PATH
    }
}
