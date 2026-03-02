# Device-Bins

These are fullflash binaries (flash at offset `0x0`).

Filename format:
- `NEONDRIVE_<DEVICE_NAME>_<VERSION>.bin`

Example flash command (ESP32 boards):
- `python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32 --port COM15 --baud 460800 write_flash 0x0 NEONDRIVE_CYD-3.5_<VERSION>.bin`
Example flash command (ESP32-S3 boards):
- `python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port COM3 --baud 460800 write_flash 0x0 NEONDRIVE_T-DisplayS3_<VERSION>.bin`

CYD-3.5 note:
- First boot runs touch calibration and saves `/touch_cal_cyd35.json` to SD card.
- To recalibrate, delete `/touch_cal_cyd35.json` from SD and reboot.
