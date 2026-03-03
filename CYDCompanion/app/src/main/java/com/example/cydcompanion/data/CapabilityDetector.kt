package com.example.cydcompanion.data

import com.example.cydcompanion.domain.CapabilityMatrix
import com.example.cydcompanion.domain.ConnectionState

object CapabilityDetector {
    fun detect(connection: ConnectionState): CapabilityMatrix {
        val root = isRootPresent()
        return CapabilityMatrix(
            vpnCapture = true, // API 26+ app baseline supports VpnService.
            rootDetected = root,
            raw80211Capture = root,
            raw80211Injection = root,
            companionLinked = connection.status == ConnectionState.Status.CONNECTED
        )
    }

    private fun isRootPresent(): Boolean {
        val suCandidates = listOf(
            "/system/bin/su",
            "/system/xbin/su",
            "/sbin/su",
            "/vendor/bin/su",
            "/su/bin/su"
        )
        return suCandidates.any { path ->
            runCatching { java.io.File(path).exists() && java.io.File(path).canExecute() }.getOrDefault(false)
        }
    }
}
