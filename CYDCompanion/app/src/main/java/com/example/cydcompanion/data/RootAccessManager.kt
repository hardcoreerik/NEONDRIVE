package com.example.cydcompanion.data

import java.io.File
import java.util.concurrent.TimeUnit

data class RootCheckResult(
    val hasSuBinary: Boolean,
    val suWorks: Boolean,
    val uidLine: String? = null,
    val message: String
)

object RootAccessManager {
    private val suCandidates = listOf(
        "/system/bin/su",
        "/system/xbin/su",
        "/sbin/su",
        "/vendor/bin/su",
        "/su/bin/su"
    )

    fun quickDetect(): RootCheckResult {
        val hasSu = suCandidates.any { path -> runCatching { File(path).exists() }.getOrDefault(false) }
        if (!hasSu) {
            return RootCheckResult(
                hasSuBinary = false,
                suWorks = false,
                message = "No su binary found."
            )
        }
        return RootCheckResult(
            hasSuBinary = true,
            suWorks = false,
            message = "su binary detected. Tap 1-Click Root Access to request shell permission."
        )
    }

    fun oneClickRootAccess(): RootCheckResult {
        val hasSu = suCandidates.any { path -> runCatching { File(path).exists() }.getOrDefault(false) }
        if (!hasSu) {
            return RootCheckResult(
                hasSuBinary = false,
                suWorks = false,
                message = "Device does not appear rooted (su not present)."
            )
        }

        return runCatching {
            val proc = ProcessBuilder("su", "-c", "id")
                .redirectErrorStream(true)
                .start()

            val finished = proc.waitFor(3, TimeUnit.SECONDS)
            if (!finished) {
                proc.destroyForcibly()
                return@runCatching RootCheckResult(
                    hasSuBinary = true,
                    suWorks = false,
                    message = "su request timed out. Grant root in manager app and retry."
                )
            }

            val out = proc.inputStream.bufferedReader().use { it.readText() }.trim()
            val ok = proc.exitValue() == 0 && out.contains("uid=0")
            RootCheckResult(
                hasSuBinary = true,
                suWorks = ok,
                uidLine = out.lineSequence().firstOrNull(),
                message = if (ok) "Root shell granted." else "Root denied or unavailable."
            )
        }.getOrElse { err ->
            RootCheckResult(
                hasSuBinary = true,
                suWorks = false,
                message = "Root check failed: ${err.message ?: "unknown error"}"
            )
        }
    }
}
