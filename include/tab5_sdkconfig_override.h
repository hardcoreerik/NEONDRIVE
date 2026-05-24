#pragma once

// Force Tab5 ESP-Hosted SDIO pin mapping at compile-time for Arduino-ESP32 P4
// builds that default to generic esp32-p4-evboard pin values.
//
// Official Tab5 P4 -> C6 SDIO mapping:
// CLK=G12, CMD=G13, D0=G11, D1=G10, D2=G9, D3=G8, RST=G15

#include <sdkconfig.h>

#undef CONFIG_ESP_SDIO_PIN_CLK
#define CONFIG_ESP_SDIO_PIN_CLK 12

#undef CONFIG_ESP_SDIO_PIN_CMD
#define CONFIG_ESP_SDIO_PIN_CMD 13

#undef CONFIG_ESP_SDIO_PIN_D0
#define CONFIG_ESP_SDIO_PIN_D0 11

#undef CONFIG_ESP_SDIO_PIN_D1
#define CONFIG_ESP_SDIO_PIN_D1 10

#undef CONFIG_ESP_SDIO_PIN_D2
#define CONFIG_ESP_SDIO_PIN_D2 9

#undef CONFIG_ESP_SDIO_PIN_D3
#define CONFIG_ESP_SDIO_PIN_D3 8

// RST overrides:
// CONFIG_ESP_SDIO_GPIO_RESET_SLAVE  — used in WiFiGeneric.cpp (commented out there)
// CONFIG_ESP_GPIO_SLAVE_RESET_SLAVE — used by pre-compiled libespressif__esp_hosted.a
//   NOTE: the .a was compiled with GPIO54 (eval board default) and cannot be changed
//   via macro override alone. We issue the actual GPIO15 reset pulse directly from
//   neon_rf_init() using gpio_set_level() before any WiFi call.
#ifdef CONFIG_ESP_SDIO_GPIO_RESET_SLAVE
#undef CONFIG_ESP_SDIO_GPIO_RESET_SLAVE
#define CONFIG_ESP_SDIO_GPIO_RESET_SLAVE 15
#endif
#ifdef CONFIG_ESP_GPIO_SLAVE_RESET_SLAVE
#undef CONFIG_ESP_GPIO_SLAVE_RESET_SLAVE
#define CONFIG_ESP_GPIO_SLAVE_RESET_SLAVE 15
#endif

// Reduce SDIO clock from 40 MHz (eval-board default) to 20 MHz for more robust
// initial enumeration of the C6 slave.  H_SDIO_CLOCK_FREQ_KHZ = this value, and
// it is expanded inline when WiFiGeneric.cpp calls INIT_DEFAULT_HOST_SDIO_CONFIG().
#undef CONFIG_ESP_SDIO_CLOCK_FREQ_KHZ
#define CONFIG_ESP_SDIO_CLOCK_FREQ_KHZ 20000
