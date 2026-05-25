# LilyGO T-Display-S3 HAL Pinout (Porting Notes)

Last verified: 2026-05-24

## Board Probe (This Device)

Probed on `COM6` with PlatformIO esptool:

- Chip: `ESP32-S3 (QFN56) rev v0.2`
- Features: `WiFi, BLE, Embedded PSRAM 8MB (AP_3v3)`
- Crystal: `40MHz`
- USB mode: `USB-Serial/JTAG`
- MAC: `B4:3A:45:A1:A6:0C`

Command used:

```powershell
python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port COM6 chip_id
```

## Official Pin Map For HAL

The following mapping is taken from LilyGO's own T-Display-S3 example pin config and TFT_eSPI setup, and matches our local `include/User_Setup_tdisplay_s3.h`.

### Display (ST7789, 8-bit parallel)

- `PIN_LCD_BL` = `GPIO38`
- `PIN_POWER_ON` = `GPIO15` (display power gate, especially important on battery boot)
- `PIN_LCD_RES` = `GPIO5`
- `PIN_LCD_CS` = `GPIO6`
- `PIN_LCD_DC` = `GPIO7`
- `PIN_LCD_WR` = `GPIO8`
- `PIN_LCD_RD` = `GPIO9`
- `PIN_LCD_D0` = `GPIO39`
- `PIN_LCD_D1` = `GPIO40`
- `PIN_LCD_D2` = `GPIO41`
- `PIN_LCD_D3` = `GPIO42`
- `PIN_LCD_D4` = `GPIO45`
- `PIN_LCD_D5` = `GPIO46`
- `PIN_LCD_D6` = `GPIO47`
- `PIN_LCD_D7` = `GPIO48`

### Buttons / Battery ADC

- `PIN_BUTTON_1` = `GPIO0`
- `PIN_BUTTON_2` = `GPIO14`
- `PIN_BAT_VOLT` = `GPIO4`

### Touch + I2C (Touch variant)

- `PIN_IIC_SCL` = `GPIO17`
- `PIN_IIC_SDA` = `GPIO18`
- `PIN_TOUCH_INT` = `GPIO16`
- `PIN_TOUCH_RES` = `GPIO21`

Notes:
- Touch controller on T-Display-S3 touch variants is CST816/CST820/CST328 family depending on module.
- Non-touch modules can keep touch disabled in the HAL while still using the same display/button pins.

## HAL Implementation Guidance

For the NEONDRIVE T-Display-S3 HAL profile:

- Treat TFT as 8-bit parallel ST7789, not SPI.
- Initialize `PIN_POWER_ON (GPIO15)` early and drive it `HIGH` before display init.
- Keep backlight control on `GPIO38` (PWM optional, digital on/off acceptable initially).
- Keep nav buttons on `GPIO0` (next) and `GPIO14` (select).
- If touch is enabled, use I2C on `SCL=17`, `SDA=18`, with `INT=16`, `RST=21`.

## Source References

- LilyGO T-Display-S3 repository: [https://github.com/Xinyuan-LilyGO/T-Display-S3](https://github.com/Xinyuan-LilyGO/T-Display-S3)
- Example pin config used above: [https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/examples/touch_test/pin_config.h](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/examples/touch_test/pin_config.h)
- TFT_eSPI setup used above: [https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/lib/TFT_eSPI/User_Setups/Setup206_LilyGo_T_Display_S3.h](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/lib/TFT_eSPI/User_Setups/Setup206_LilyGo_T_Display_S3.h)
- Repository README notes (battery/backlight power-on requirement): [https://github.com/Xinyuan-LilyGO/T-Display-S3#9️⃣-faq](https://github.com/Xinyuan-LilyGO/T-Display-S3#9%EF%B8%8F-faq)
