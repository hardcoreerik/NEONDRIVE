#include "usb_msc_tdisplay.h"

#if (defined(NEONDRIVE_TARGET_TDISPLAY_S3) || defined(NEONDRIVE_TARGET_TDISPLAY_S3_TOUCH)) && defined(CONFIG_TINYUSB_MSC_ENABLED) && (CONFIG_TINYUSB_MSC_ENABLED)

#include <Arduino.h>
#include <USBMSC.h>
#include <sdmmc_cmd.h>

// Access SDMMCFS::_card handle for sector-level callbacks.
#define protected public
#include <SD_MMC.h>
#undef protected

static USBMSC s_msc;
static bool s_mscReady = false;
static volatile bool s_enabled = false;
static volatile bool s_hostMounted = false;
static volatile bool s_appLocked = false;

static bool msc_start_stop(uint8_t power_condition, bool start, bool load_eject) {
  (void)power_condition;
  if (load_eject && !start) {
    s_hostMounted = false;
  } else if (start) {
    s_hostMounted = true;
  }
  s_appLocked = s_hostMounted;
  return true;
}

static int32_t msc_read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (!s_enabled) return 0;
  if (!buffer || bufsize == 0) return 0;

  sdmmc_card_t *card = SD_MMC._card;
  if (!card) return 0;

  // TinyUSB may issue partial-sector reads; our sector-size is 512B.
  uint8_t tmp[512];
  size_t sector = lba;
  uint8_t *dst = static_cast<uint8_t*>(buffer);
  uint32_t remain = bufsize;
  uint32_t ofs = offset;

  while (remain > 0) {
    if (sdmmc_read_sectors(card, tmp, sector, 1) != ESP_OK) {
      return 0;
    }
    uint32_t chunk = min(remain, 512U - ofs);
    memcpy(dst, tmp + ofs, chunk);
    dst += chunk;
    remain -= chunk;
    sector++;
    ofs = 0;
  }
  s_hostMounted = true;
  s_appLocked = true;
  return (int32_t)bufsize;
}

static int32_t msc_write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  if (!s_enabled) return 0;
  if (!buffer || bufsize == 0) return 0;

  sdmmc_card_t *card = SD_MMC._card;
  if (!card) return 0;

  uint8_t tmp[512];
  size_t sector = lba;
  uint8_t *src = buffer;
  uint32_t remain = bufsize;
  uint32_t ofs = offset;

  while (remain > 0) {
    uint32_t chunk = min(remain, 512U - ofs);
    if (ofs != 0 || chunk != 512U) {
      if (sdmmc_read_sectors(card, tmp, sector, 1) != ESP_OK) {
        return 0;
      }
      memcpy(tmp + ofs, src, chunk);
      if (sdmmc_write_sectors(card, tmp, sector, 1) != ESP_OK) {
        return 0;
      }
    } else {
      if (sdmmc_write_sectors(card, src, sector, 1) != ESP_OK) {
        return 0;
      }
    }
    src += chunk;
    remain -= chunk;
    sector++;
    ofs = 0;
  }
  s_hostMounted = true;
  s_appLocked = true;
  return (int32_t)bufsize;
}

void nd_usb_msc_tdisplay_init(void) {
  if (s_mscReady) return;

  sdmmc_card_t *card = SD_MMC._card;
  if (!card || card->csd.sector_size == 0 || card->csd.capacity == 0) {
    Serial.println("[usb_msc] SD card unavailable; MSC not started");
    return;
  }

  const uint32_t blockSize = (uint16_t)card->csd.sector_size;
  const uint32_t blockCount = (uint32_t)card->csd.capacity;

  s_msc.vendorID("NEONDRV");
  s_msc.productID("TDisplayS3 SD");
  s_msc.productRevision("1.0");
  s_msc.onStartStop(msc_start_stop);
  s_msc.onRead(msc_read);
  s_msc.onWrite(msc_write);

  if (!s_msc.begin(blockCount, (uint16_t)blockSize)) {
    Serial.println("[usb_msc] begin failed");
    return;
  }
  // Device-first policy: keep MSC hidden unless explicitly enabled by user/API.
  s_msc.mediaPresent(false);
  s_mscReady = true;
  s_enabled = false;
  s_hostMounted = false;
  s_appLocked = false;

  Serial.printf("[usb_msc] active=1 enabled=0 blocks=%lu block_size=%lu\n",
                (unsigned long)blockCount, (unsigned long)blockSize);
}

void nd_usb_msc_set_enabled(bool enabled) {
  if (!s_mscReady) return;
  s_enabled = enabled;
  s_hostMounted = false;
  s_appLocked = enabled;
  s_msc.mediaPresent(enabled);
  Serial.printf("[usb_msc] enabled=%d\n", enabled ? 1 : 0);
}

bool nd_usb_msc_active(void) { return s_mscReady; }
bool nd_usb_msc_enabled(void) { return s_enabled; }
bool nd_usb_msc_host_mounted(void) { return s_hostMounted; }
bool nd_usb_msc_sd_app_locked(void) { return s_appLocked; }

#else
void nd_usb_msc_tdisplay_init(void) {}
void nd_usb_msc_set_enabled(bool enabled) { (void)enabled; }
bool nd_usb_msc_active(void) { return false; }
bool nd_usb_msc_enabled(void) { return false; }
bool nd_usb_msc_host_mounted(void) { return false; }
bool nd_usb_msc_sd_app_locked(void) { return false; }
#endif
