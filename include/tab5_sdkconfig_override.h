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

#ifdef CONFIG_ESP_SDIO_GPIO_RESET_SLAVE
#undef CONFIG_ESP_SDIO_GPIO_RESET_SLAVE
#define CONFIG_ESP_SDIO_GPIO_RESET_SLAVE 15
#endif
