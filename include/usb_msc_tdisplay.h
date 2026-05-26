#pragma once

#include <stdbool.h>

// T-Display-only USB MSC helper.
// Exposes TF-shield SD card as USB Mass Storage while keeping CDC serial active.
// usb_msc_tdisplay.cpp provides real implementations for T-Display targets and
// no-op stubs for all others — no guarding needed in this header.
void nd_usb_msc_tdisplay_init(void);
void nd_usb_msc_set_enabled(bool enabled);

bool nd_usb_msc_active(void);
bool nd_usb_msc_enabled(void);
bool nd_usb_msc_host_mounted(void);
bool nd_usb_msc_sd_app_locked(void);
