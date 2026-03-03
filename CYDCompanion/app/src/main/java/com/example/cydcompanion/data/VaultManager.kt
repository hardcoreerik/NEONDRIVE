package com.example.cydcompanion.data

import android.content.ContentResolver
import android.content.Context
import android.database.Cursor
import android.net.Uri
import android.provider.OpenableColumns
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

data class VaultCaptureInfo(
    val projectId: String,
    val captureId: String,
    val label: String,
    val source: String? = null,
    val createdAt: String
)

class VaultManager(private val context: Context) {
    private val allowedCaptureExt = setOf("pcap", "pcapng", "log", "txt")

    private fun vaultRoot(): File = File(context.filesDir, "vault").apply { mkdirs() }

    private fun projectDir(projectId: String): File =
        File(vaultRoot(), sanitizeSegment(projectId.ifBlank { "default" })).apply { mkdirs() }

    fun captureDir(projectId: String, captureId: String): File =
        File(projectDir(projectId), "capture_${sanitizeSegment(captureId)}").apply { mkdirs() }

    private fun captureMetaFile(projectId: String, captureId: String): File =
        File(captureDir(projectId, captureId), "capture.json")

    suspend fun createCapture(
        projectId: String,
        label: String,
        source: String? = null
    ): VaultCaptureInfo = withContext(Dispatchers.IO) {
        val id = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val createdAt = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())
        val info = VaultCaptureInfo(
            projectId = projectId.ifBlank { "default" },
            captureId = id,
            label = label.ifBlank { id },
            source = source,
            createdAt = createdAt
        )

        val meta = JSONObject().apply {
            put("projectId", info.projectId)
            put("captureId", info.captureId)
            put("label", info.label)
            put("source", info.source ?: JSONObject.NULL)
            put("createdAt", info.createdAt)
        }
        captureMetaFile(info.projectId, info.captureId).writeText(meta.toString(2))
        info
    }

    fun getCapturePath(projectId: String, captureId: String): File = captureDir(projectId, captureId)

    suspend fun countItems(projectId: String, captureId: String): Int = withContext(Dispatchers.IO) {
        val dir = captureDir(projectId, captureId)
        dir.walkTopDown().count { it.isFile && it.name != "capture.json" }
    }

    suspend fun saveBytes(
        projectId: String,
        captureId: String,
        fileName: String,
        bytes: ByteArray,
        subPath: String = ""
    ): File = withContext(Dispatchers.IO) {
        val baseDir = captureDir(projectId, captureId)
        val targetDir = if (subPath.isBlank()) {
            baseDir
        } else {
            File(baseDir, subPath.replace('\\', '/').trim('/')).apply { mkdirs() }
        }
        val safe = sanitizeFileName(fileName.ifBlank { "file.bin" })
        val out = uniqueFile(targetDir, safe)
        out.writeBytes(bytes)
        out
    }

    suspend fun importUris(
        projectId: String,
        captureId: String,
        uris: List<Uri>
    ): List<File> = withContext(Dispatchers.IO) {
        val resolver = context.contentResolver
        val dir = captureDir(projectId, captureId)
        uris.mapNotNull { uri ->
            val name = sanitizeFileName(guessDisplayName(resolver, uri) ?: "import.bin")
            if (!isAllowedCaptureName(name)) return@mapNotNull null
            val out = uniqueFile(dir, name)
            resolver.openInputStream(uri)?.use { input ->
                FileOutputStream(out).use { output ->
                    input.copyTo(output)
                }
                out
            }
        }
    }

    suspend fun importDocumentTree(
        projectId: String,
        captureId: String,
        treeUri: Uri
    ): Int = withContext(Dispatchers.IO) {
        val resolver = context.contentResolver
        val root = DocumentFile.fromTreeUri(context, treeUri) ?: return@withContext 0
        copyDocumentTreeInto(resolver, root, captureDir(projectId, captureId))
    }

    suspend fun addBookmark(
        projectId: String,
        captureId: String,
        url: String,
        title: String? = null
    ): File = withContext(Dispatchers.IO) {
        val dir = captureDir(projectId, captureId)
        val safeTitle = sanitizeFileName((title?.ifBlank { null } ?: "bookmark").trim())
        val out = uniqueFile(dir, "$safeTitle.url")
        out.writeText("[InternetShortcut]\nURL=${url.trim()}\n")
        out
    }

    suspend fun buildVaultContextSummary(
        projectId: String,
        captureId: String,
        maxFiles: Int = 200,
        maxTextBytes: Int = 24_000
    ): String = withContext(Dispatchers.IO) {
        val dir = captureDir(projectId, captureId)
        if (!dir.exists()) return@withContext "(Vault capture missing)"

        val files = dir.walkTopDown()
            .filter { it.isFile && it.name != "capture.json" }
            .take(maxFiles)
            .toList()

        val sb = StringBuilder()
        sb.appendLine("=== DATA VAULT (PROJECT=$projectId CAPTURE=$captureId) ===")
        sb.appendLine("Root: ${dir.absolutePath}")
        sb.appendLine("Files: ${files.size}${if (files.size >= maxFiles) "+" else ""}")
        sb.appendLine()

        var remainingText = maxTextBytes
        for (f in files) {
            val rel = f.relativeTo(dir).path.replace('\\', '/')
            sb.appendLine("- $rel (${f.length()} bytes)")

            val ext = f.extension.lowercase(Locale.US)
            val isText = ext in setOf("txt", "json", "csv", "log", "md", "url")
            if (isText && remainingText > 0) {
                val chunk = readFirstBytes(f, remainingText)
                remainingText -= chunk.size
                val text = runCatching { chunk.toString(Charsets.UTF_8) }.getOrElse { "" }
                if (text.isNotBlank()) {
                    sb.appendLine("  --- BEGIN ${rel} ---")
                    sb.appendLine(text.trim())
                    sb.appendLine("  --- END ${rel} ---")
                }
            }
        }
        sb.toString()
    }

    // ---------------------------------------------------------------------
    // Internals
    // ---------------------------------------------------------------------

    private fun sanitizeSegment(s: String): String =
        s.trim().replace(Regex("[^a-zA-Z0-9._-]+"), "_").take(80).ifBlank { "default" }

    private fun sanitizeFileName(name: String): String {
        val trimmed = name.trim().ifBlank { "file" }
        val base = trimmed.substringAfterLast('/').substringAfterLast('\\')
        return base.replace(Regex("[\\u0000-\\u001F\\u007F/:*?\"<>|]+"), "_")
            .take(120)
            .ifBlank { "file" }
    }

    private fun uniqueFile(dir: File, name: String): File {
        var out = File(dir, name)
        if (!out.exists()) return out
        val dot = name.lastIndexOf('.')
        val stem = if (dot >= 0) name.substring(0, dot) else name
        val ext = if (dot >= 0) name.substring(dot) else ""
        var i = 2
        while (out.exists()) {
            out = File(dir, "${stem}_$i$ext")
            i++
        }
        return out
    }

    private fun readFirstBytes(file: File, maxBytes: Int): ByteArray {
        if (maxBytes <= 0) return ByteArray(0)
        val cap = minOf(maxBytes, 256 * 1024)
        val buf = ByteArray(cap)
        val read = file.inputStream().use { it.read(buf) }
        if (read <= 0) return ByteArray(0)
        return buf.copyOf(read)
    }

    private fun guessDisplayName(resolver: ContentResolver, uri: Uri): String? {
        if (uri.scheme != "content") return uri.lastPathSegment
        var cursor: Cursor? = null
        return try {
            cursor = resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            if (cursor != null && cursor.moveToFirst()) cursor.getString(0) else null
        } catch (_: Exception) {
            null
        } finally {
            cursor?.close()
        }
    }

    private fun copyDocumentTreeInto(resolver: ContentResolver, doc: DocumentFile, destDir: File): Int {
        var count = 0
        if (doc.isDirectory) {
            val dirName = sanitizeSegment(doc.name ?: "folder")
            val childDir = File(destDir, dirName).apply { mkdirs() }
            doc.listFiles().forEach { child ->
                count += copyDocumentTreeInto(resolver, child, childDir)
            }
        } else if (doc.isFile) {
            val name = sanitizeFileName(doc.name ?: "file.bin")
            if (!isAllowedCaptureName(name)) return count
            val out = uniqueFile(destDir, name)
            resolver.openInputStream(doc.uri)?.use { input ->
                FileOutputStream(out).use { output ->
                    input.copyTo(output)
                }
                count++
            }
        }
        return count
    }

    private fun isAllowedCaptureName(pathOrName: String): Boolean {
        val name = pathOrName.substringAfterLast('/').substringAfterLast('\\')
        val ext = name.substringAfterLast('.', "").lowercase(Locale.US)
        return ext in allowedCaptureExt
    }
}
