package com.example.cydcompanion.ai

import android.content.Context

/**
 * Lazy-loaded OUI vendor lookup backed by assets/oui.csv.
 * Format: XX:XX:XX,Vendor Name  (# comment lines are skipped)
 *
 * Drop the full ~5 MB IEEE MA-L CSV at app/src/main/assets/oui.csv for
 * complete coverage; the seed file ships common WiFi chipset vendors.
 */
object OuiDatabase {

    private var lookup: HashMap<String, String>? = null

    /** Must be called once before any resolve(). Safe to call multiple times. */
    fun init(context: Context) {
        if (lookup != null) return
        val map = HashMap<String, String>(512)
        try {
            context.assets.open("oui.csv").bufferedReader().use { reader ->
                reader.forEachLine { line ->
                    val trimmed = line.trim()
                    if (trimmed.startsWith('#') || trimmed.isBlank()) return@forEachLine
                    val comma = trimmed.indexOf(',')
                    if (comma > 0) {
                        val oui = trimmed.substring(0, comma).uppercase().trim()
                        val vendor = trimmed.substring(comma + 1).trim()
                        if (oui.isNotBlank() && vendor.isNotBlank()) {
                            map[oui] = vendor
                        }
                    }
                }
            }
        } catch (_: Exception) {
            // Asset missing or unreadable — lookup stays empty; resolve() returns fallback.
        }
        lookup = map
    }

    /**
     * Resolve a MAC address or bare OUI string (XX:XX:XX or XX-XX-XX) to a vendor name.
     * Returns "Unknown (XX:XX:XX)" when the OUI is not in the database.
     */
    fun resolve(macOrOui: String): String {
        val map = lookup ?: return "Unknown"
        val norm = macOrOui.uppercase()
            .replace("-", ":")
            .replace(".", ":")
        val parts = norm.split(":")
        val oui = parts.take(3).joinToString(":")
        return map[oui] ?: "Unknown ($oui)"
    }

    /** True when at least one entry has been loaded. */
    val isLoaded: Boolean get() = lookup?.isNotEmpty() == true
}
