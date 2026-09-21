# MediBot Hardware Firmware Backups

This directory stores raw binary dumps (`.bin`) read directly from the physical microcontrollers in the MediBot kiosk ecosystem before any new firmware is flashed.

## Modules in Ecosystem

| Module | Target Chip | Default Flash Size | Backup File Naming Convention |
| :--- | :--- | :--- | :--- |
| **Main Controller** | ESP32-S3 | 8 MB (`0x800000`) | `firmware_backups/main_controller_backup_YYYYMMDD.bin` |
| **Sunton 7" Display** | ESP32-S3 | 16 MB (`0x1000000`) | `firmware_backups/display_unit_backup_YYYYMMDD.bin` |
| **Height Module** | ESP32-WROOM-32 | 4 MB (`0x400000`) | `firmware_backups/height_module_backup_YYYYMMDD.bin` |
| **Weight Module** | ESP-12F (ESP8266) | 4 MB (`0x400000`) | `firmware_backups/weight_module_backup_YYYYMMDD.bin` |
| **Stepper Module** | ESP32-WROOM-32 | 4 MB (`0x400000`) | `firmware_backups/stepper_module_backup_YYYYMMDD.bin` |
| **Temp Carriage Module** | ESP-12F (ESP8266) | 4 MB (`0x400000`) | `firmware_backups/temp_carriage_backup_YYYYMMDD.bin` |

## One-Click Backup Script

Run the automated interactive helper script from the repository root:
```bash
./tools/backup_firmware.sh
```

Or pass target name and port directly:
```bash
./tools/backup_firmware.sh main /dev/ttyUSB0
./tools/backup_firmware.sh height /dev/ttyUSB1
```

## How to Restore / Rollback a Backup

To flash an original backup back onto a microcontroller:
```bash
~/.platformio/penv/bin/esptool.py --port /dev/ttyUSB0 --baud 921600 write_flash 0x0 firmware_backups/<YOUR_BACKUP_FILE>.bin
```
