package com.example.cydcompanion.domain

enum class OperationMode {
    COMPANION,
    STANDALONE;

    companion object {
        fun fromStored(value: String?): OperationMode {
            return when (value?.trim()?.uppercase()) {
                STANDALONE.name -> STANDALONE
                else -> COMPANION
            }
        }
    }
}

data class CapabilityMatrix(
    val vpnCapture: Boolean,
    val rootDetected: Boolean,
    val raw80211Capture: Boolean,
    val raw80211Injection: Boolean,
    val companionLinked: Boolean
)
