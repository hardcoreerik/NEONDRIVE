package com.example.cydcompanion.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.asRequestBody
import org.json.JSONObject
import java.io.File
import java.io.IOException
import javax.net.ssl.SSLContext
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager

data class WpaSecUploadResult(
    val success: Boolean,
    val uploaded: Int = 0,
    val alreadyExists: Int = 0,
    val error: String? = null
)

data class WpaSecCrack(
    val bssid: String,
    val ssid: String,
    val password: String,
    val uploadDate: String? = null
)

class WpaSecRepository(private val apiKey: String) {
    
    private val client: OkHttpClient by lazy {
        // Trust all certificates for WPA-SEC (some older SSL)
        val trustAllCerts = arrayOf<TrustManager>(object : X509TrustManager {
            override fun checkClientTrusted(chain: Array<java.security.cert.X509Certificate>, authType: String) {}
            override fun checkServerTrusted(chain: Array<java.security.cert.X509Certificate>, authType: String) {}
            override fun getAcceptedIssuers(): Array<java.security.cert.X509Certificate> = arrayOf()
        })
        
        val sslContext = SSLContext.getInstance("TLS")
        sslContext.init(null, trustAllCerts, java.security.SecureRandom())
        
        OkHttpClient.Builder()
            .sslSocketFactory(sslContext.socketFactory, trustAllCerts[0] as X509TrustManager)
            .hostnameVerifier { _, _ -> true }
            .connectTimeout(30, java.util.concurrent.TimeUnit.SECONDS)
            .readTimeout(60, java.util.concurrent.TimeUnit.SECONDS)
            .writeTimeout(60, java.util.concurrent.TimeUnit.SECONDS)
            .build()
    }
    
    companion object {
        private const val BASE_URL = "https://wpa-sec.stanev.org"
        private const val UPLOAD_ENDPOINT = "/"
        private const val POTFILE_ENDPOINT = "/?api&dl=1"
    }
    
    /**
     * Upload a .pcap or .22000 file to WPA-SEC for cracking
     */
    suspend fun uploadCapture(
        file: File,
        onProgress: (String) -> Unit = {}
    ): WpaSecUploadResult = withContext(Dispatchers.IO) {
        if (apiKey.isBlank()) {
            return@withContext WpaSecUploadResult(
                success = false,
                error = "API key not configured"
            )
        }
        
        if (!file.exists()) {
            return@withContext WpaSecUploadResult(
                success = false,
                error = "File not found: ${file.name}"
            )
        }
        
        onProgress("Preparing ${file.name}...")
        
        try {
            val requestBody = MultipartBody.Builder()
                .setType(MultipartBody.FORM)
                .addFormDataPart("key", apiKey)
                .addFormDataPart(
                    "file",
                    file.name,
                    file.asRequestBody("application/octet-stream".toMediaType())
                )
                .build()
            
            val request = Request.Builder()
                .url(BASE_URL + UPLOAD_ENDPOINT)
                .post(requestBody)
                .build()
            
            onProgress("Uploading to WPA-SEC...")
            
            client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) {
                    return@withContext WpaSecUploadResult(
                        success = false,
                        error = "HTTP ${response.code}: ${response.message}"
                    )
                }
                
                val body = response.body?.string() ?: ""
                onProgress("Processing response...")
                
                // WPA-SEC returns simple text response
                val uploaded = when {
                    body.contains("already") || body.contains("duplicate") -> 0
                    body.contains("success", ignoreCase = true) -> 1
                    else -> 1 // Assume success if no error
                }
                
                val alreadyExists = if (uploaded == 0) 1 else 0
                
                return@withContext WpaSecUploadResult(
                    success = true,
                    uploaded = uploaded,
                    alreadyExists = alreadyExists
                )
            }
        } catch (e: IOException) {
            return@withContext WpaSecUploadResult(
                success = false,
                error = "Network error: ${e.message}"
            )
        } catch (e: Exception) {
            return@withContext WpaSecUploadResult(
                success = false,
                error = "Upload failed: ${e.message}"
            )
        }
    }
    
    /**
     * Download the potfile (cracked passwords) from WPA-SEC
     */
    suspend fun downloadPotfile(): List<WpaSecCrack> = withContext(Dispatchers.IO) {
        if (apiKey.isBlank()) {
            return@withContext emptyList()
        }
        
        try {
            val url = BASE_URL + POTFILE_ENDPOINT + "&key=$apiKey"
            val request = Request.Builder().url(url).get().build()
            
            client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) {
                    return@withContext emptyList()
                }
                
                val body = response.body?.string() ?: return@withContext emptyList()
                parsePotfile(body)
            }
        } catch (e: Exception) {
            emptyList()
        }
    }
    
    /**
     * Parse WPA-SEC potfile format:
     * BSSID:SSID:password
     */
    private fun parsePotfile(content: String): List<WpaSecCrack> {
        val results = mutableListOf<WpaSecCrack>()
        content.lines().forEach { line ->
            val parts = line.split(":")
            if (parts.size >= 3) {
                val bssid = parts[0].trim()
                val ssid = parts[1].trim()
                val password = parts.drop(2).joinToString(":").trim()
                
                if (bssid.isNotEmpty() && bssid.replace(":", "").length == 12) {
                    results.add(
                        WpaSecCrack(
                            bssid = bssid.uppercase(),
                            ssid = ssid,
                            password = password
                        )
                    )
                }
            }
        }
        return results
    }
    
    /**
     * Check if a specific BSSID has been cracked
     */
    suspend fun checkBssid(bssid: String): WpaSecCrack? = withContext(Dispatchers.IO) {
        val normalized = bssid.replace(":", "").replace("-", "").uppercase()
        val cracks = downloadPotfile()
        cracks.find { 
            it.bssid.replace(":", "").replace("-", "").uppercase() == normalized 
        }
    }
}
