#!/usr/bin/env bash
# ==============================================================================
# MediBot Kiosk - Firmware Flash Backup & Restore Utility
# Safely dumps or restores bit-for-bit physical microcontroller flash memory.
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BACKUP_DIR="${REPO_ROOT}/firmware_backups"

mkdir -p "${BACKUP_DIR}"

# Find esptool
ESPTOOL=""
if [ -x "${HOME}/.platformio/penv/bin/esptool.py" ]; then
    ESPTOOL="${HOME}/.platformio/penv/bin/esptool.py"
elif command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL="$(command -v esptool.py)"
elif [ -x "${HOME}/.platformio/penv/bin/pio" ]; then
    ESPTOOL="${HOME}/.platformio/penv/bin/pio pkg exec -p tool-esptoolpy -- esptool.py"
else
    echo "[-] Error: esptool.py could not be found."
    echo "    Please verify PlatformIO is installed at ~/.platformio"
    exit 1
fi

echo "======================================================================"
echo "         MediBot Microcontroller Firmware Backup Utility"
echo "======================================================================"
echo "[+] Using esptool: ${ESPTOOL}"
echo "[+] Destination:   ${BACKUP_DIR}"
echo "----------------------------------------------------------------------"

# Check for restore flag
if [ "$1" == "--restore" ] || [ "$1" == "-r" ]; then
    RESTORE_FILE="$2"
    RESTORE_PORT="$3"

    if [ -z "${RESTORE_FILE}" ] || [ ! -f "${RESTORE_FILE}" ]; then
        echo "[-] Error: Please provide a valid backup .bin file to restore."
        echo "    Usage: $0 --restore <path_to_backup.bin> [port]"
        exit 1
    fi

    if [ -z "${RESTORE_PORT}" ]; then
        PORTS=($(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true))
        if [ ${#PORTS[@]} -eq 0 ]; then
            echo "[-] Error: No serial devices (/dev/ttyUSB* or /dev/ttyACM*) detected."
            exit 1
        elif [ ${#PORTS[@]} -eq 1 ]; then
            RESTORE_PORT="${PORTS[0]}"
            echo "[+] Auto-detected port: ${RESTORE_PORT}"
        else
            echo "[?] Multiple serial ports detected. Select target port:"
            select p in "${PORTS[@]}"; do
                RESTORE_PORT="$p"
                break
            done
        fi
    fi

    echo "[!] WARNING: You are about to restore firmware from:"
    echo "    File: ${RESTORE_FILE}"
    echo "    Port: ${RESTORE_PORT}"
    read -p "Are you sure you want to proceed? (y/N): " CONFIRM
    if [[ ! "${CONFIRM}" =~ ^[Yy]$ ]]; then
        echo "[-] Restore aborted by user."
        exit 0
    fi

    echo "[+] Flashing backup image..."
    ${ESPTOOL} --port "${RESTORE_PORT}" --baud 921600 write_flash 0x0 "${RESTORE_FILE}"
    echo "[✓] Firmware successfully restored to ${RESTORE_PORT}!"
    exit 0
fi

MODULE_ARG="$1"
PORT_ARG="$2"

# Detect Serial Port
PORT="${PORT_ARG}"
if [ -z "${PORT}" ]; then
    PORTS=($(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true))
    if [ ${#PORTS[@]} -eq 0 ]; then
        echo "[-] No serial devices found (/dev/ttyUSB* or /dev/ttyACM*)."
        echo "    Please plug in the microcontroller via USB and try again."
        exit 1
    elif [ ${#PORTS[@]} -eq 1 ]; then
        PORT="${PORTS[0]}"
        echo "[+] Auto-detected port: ${PORT}"
    else
        echo "[?] Multiple serial ports found. Select port:"
        select p in "${PORTS[@]}"; do
            PORT="$p"
            break
        done
    fi
fi

# Detect Chip & Flash Information
echo ""
echo "[+] Reading chip ID and SPI flash information on ${PORT}..."
CHIP_INFO=$(${ESPTOOL} --port "${PORT}" flash_id 2>&1 || true)
echo "${CHIP_INFO}" | grep -E "Detecting chip type|Chip is|MAC:|Flash size" || true

# Module Selection
MODULE_NAME="${MODULE_ARG}"
if [ -z "${MODULE_NAME}" ]; then
    echo ""
    echo "[?] Select which module is currently connected to ${PORT}:"
    MODULE_OPTIONS=(
        "main_controller        (ESP32-S3 - 8MB Flash)"
        "display_unit           (Sunton 7\" ESP32-S3 - 16MB Flash)"
        "height_module          (ESP32 Dev - 4MB Flash)"
        "weight_module          (ESP8266 ESP-12F - 4MB Flash)"
        "stepper_module         (ESP32 Dev - 4MB Flash)"
        "temp_carriage_module   (ESP8266 ESP-12F - 4MB Flash)"
        "custom_label"
    )
    select opt in "${MODULE_OPTIONS[@]}"; do
        case "$REPLY" in
            1) MODULE_NAME="main_controller"; break ;;
            2) MODULE_NAME="display_unit"; break ;;
            3) MODULE_NAME="height_module"; break ;;
            4) MODULE_NAME="weight_module"; break ;;
            5) MODULE_NAME="stepper_module"; break ;;
            6) MODULE_NAME="temp_carriage_module"; break ;;
            7) 
                read -p "Enter custom module label: " MODULE_NAME
                MODULE_NAME=$(echo "${MODULE_NAME}" | tr ' ' '_')
                break 
                ;;
            *) echo "Invalid option";;
        esac
    done
fi

TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
OUT_BIN="${BACKUP_DIR}/${MODULE_NAME}_dump_${TIMESTAMP}.bin"

echo ""
echo "[+] Initiating full flash dump..."
echo "    Port:   ${PORT}"
echo "    Target: ${MODULE_NAME}"
echo "    Output: ${OUT_BIN}"
echo "----------------------------------------------------------------------"

${ESPTOOL} --port "${PORT}" --baud 921600 read_flash 0x0 ALL "${OUT_BIN}"

FILE_SIZE=$(stat -c%s "${OUT_BIN}" 2>/dev/null || stat -f%z "${OUT_BIN}" 2>/dev/null || echo "0")
FILE_SIZE_MB=$(awk "BEGIN {printf \"%.2f\", ${FILE_SIZE}/1048576}")

echo "----------------------------------------------------------------------"
echo "[✓] Flash dump completed successfully!"
echo "[✓] Saved to: ${OUT_BIN}"
echo "[✓] Image size: ${FILE_SIZE} bytes (${FILE_SIZE_MB} MB)"
echo ""
echo "[ℹ] To restore this backup in the future, run:"
echo "    ${ESPTOOL} --port ${PORT} --baud 921600 write_flash 0x0 ${OUT_BIN}"
echo "    OR:"
echo "    $0 --restore ${OUT_BIN} ${PORT}"
echo "======================================================================"
