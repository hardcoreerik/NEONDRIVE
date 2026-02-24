// WiGLE wardriving service client
// https://wigle.net/
#pragma once

#include <Arduino.h>
#include <vector>
#include "../core/heap_policy.h"

// Upload status for tracking
enum class WigleUploadStatus {
    NOT_UPLOADED,
    UPLOADED,
    PROCESSING,
    COMPLETE
};

// Sync operation result
struct WigleSyncResult {
    bool success;
    uint8_t uploaded;
    uint8_t failed;
    uint8_t skipped;     // Already uploaded
    bool statsFetched;   // Stats download succeeded
    char error[48];
};

// Sync progress callback for UI updates
typedef void (*WigleProgressCallback)(const char* status, uint8_t progress, uint8_t total);

class WiGLE {
public:
    // Sync status
    static bool isBusy();
    
    /**
     * @brief Container for a subset of WiGLE user statistics.
     *
     * The UI only needs the user's current rank and total counts of
     * WiFi, cellular and Bluetooth observations. Additional fields in
     * the API response are ignored. A `valid` flag indicates whether
     * the structure was successfully loaded from cache.
     */
    struct WigleUserStats {
        bool valid = false;
        int64_t rank = 0;
        uint64_t wifi = 0;
        uint64_t cell = 0;
        uint64_t bt = 0;
    };

    /**
     * @brief Read the cached WiGLE user statistics from disk.
     *
     * If the cache file does not exist or cannot be parsed, the returned
     * structure will have `valid` set to false. This function does not
     * attempt any network connections and may be safely called from the UI.
     *
     * @return WigleUserStats structure containing cached values
     */
    static WigleUserStats getUserStats();

    /**
     * @brief Free the uploaded files list from memory.
     *
     * The uploadedFiles vector can consume heap proportional to the number
     * of entries tracked.  Before performing TLS operations that require
     * large contiguous heap blocks, callers may persist the list and then
     * call this function to clear it.  It resets the lazy load flag so
     * that the list will be reloaded from disk on the next access.
     */
    static void freeUploadedListMemory();
    
    // Upload tracking (offline-only, file-based)
    static bool isUploaded(const char* filename);     // Check if already uploaded
    static void markAsUploaded(const char* filename); // Mark file as uploaded
    static void removeFromUploaded(const char* filename); // Remove from tracking
    static uint16_t getUploadedCount();               // Total uploads tracked
    
    // Batch upload mode (reduces SD writes from N to 1)
    static void beginBatchUpload();                   // Start batch mode
    static void endBatchUpload();                     // End batch mode and save
    
    // Network operations (require WiFi + sufficient heap)
    static bool hasCredentials();                     // Check if WiGLE API credentials configured
    static bool canSync();                            // Check heap requirements (~35KB)
    static WigleSyncResult syncFiles(WigleProgressCallback cb = nullptr);  // Full sync
    
    // Status
    static const char* getLastError();
    
private:
    // Uploaded files tracking — flat struct avoids per-entry String heap allocs
    struct UploadedFile {
        char name[48];
    };
    static std::vector<UploadedFile> uploadedFiles;
    static bool listLoaded;  // Guard for lazy loading
    static volatile bool busy;
    static char lastError[64];
    static bool batchMode;  // Batch upload mode flag
    
    // API endpoints
    static constexpr const char* API_HOST = "api.wigle.net";
    static constexpr uint16_t API_PORT = 443;
    static constexpr const char* UPLOAD_PATH = "/api/v2/file/upload";
    static constexpr const char* STATS_PATH = "/api/v2/stats/user";
    
    // Helpers
    static bool loadUploadedList();
    static bool saveUploadedList();
    static const char* getFilenameFromPath(const char* path);
    
    // Network helpers (internal)
    static bool uploadSingleFile(const char* csvPath);
    static bool fetchStats();
};
