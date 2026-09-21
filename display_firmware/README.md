# MediBot Health Kiosk - 7" Display Firmware

Pure native ESP-IDF firmware for the **Sunton ESP32-8048S070C** 7.0" (800x480) capacitive touch display unit running LVGL 9.

---

## 1. Hardware Overview & Drivers

- **Target Board**: Sunton ESP32-8048S070C (ESP32-S3-WROOM-1-N16R8, 16MB Flash, 8MB Octal PSRAM)
- **Display Controller**: ST7262 RGB Parallel LCD (800 × 480 @ 16MHz Dot Clock)
- **Touch Controller**: Goodix GT911 Capacitive Multi-touch (I2C)
- **Framework**: ESP-IDF v5.3+ (`framework = espidf`)
- **Official Managed Libraries** (via Espressif Component Registry `idf_component.yml`):
  - `lvgl/lvgl`: `==9.2.2`
  - `espressif/esp_lvgl_port`: `==2.3.2`
  - `esp_lcd_touch_gt911`: `==1.1.0`

---

## 2. Pinout & Bus Mapping

### 16-Bit RGB Data Bus (Sunton Scrambled Pinout)
| RGB Line | ESP32-S3 GPIO | RGB Line | ESP32-S3 GPIO |
|:---:|:---:|:---:|:---:|
| **D0** | GPIO 15 | **D8**  | GPIO 8  |
| **D1** | GPIO 7  | **D9**  | GPIO 16 |
| **D2** | GPIO 6  | **D10** | GPIO 1  |
| **D3** | GPIO 5  | **D11** | GPIO 14 |
| **D4** | GPIO 4  | **D12** | GPIO 21 |
| **D5** | GPIO 9  | **D13** | GPIO 47 |
| **D6** | GPIO 46 | **D14** | GPIO 48 |
| **D7** | GPIO 3  | **D15** | GPIO 45 |

### LCD Timing & Backlight
- **HSYNC**: GPIO 39
- **VSYNC**: GPIO 40
- **DE (Data Enable)**: GPIO 41
- **PCLK (Pixel Clock)**: GPIO 42
- **Backlight (BL)**: GPIO 2 (Active High)

### GT911 Capacitive Touch (I2C)
- **SDA**: GPIO 19 (Internal pull-up enabled, 400kHz)
- **SCL**: GPIO 20
- **RST**: GPIO 38
- **INT**: GPIO NC (Polled via `esp_lvgl_port`)

### Inter-Controller Communication Bus (UART1)
- **TX**: GPIO 17 (Connects to Main Controller RX)
- **RX**: GPIO 18 (Connects to Main Controller TX)
- **Baudrate**: 115200 8N1

---

## 3. Multicore Execution Model

```
 ┌───────────────────────────────────────────────┐
 │               ESP32-S3 (240 MHz)              │
 ├───────────────────────┬───────────────────────┤
 │        CORE 0         │        CORE 1         │
 │ (Communication & I/O) │  (Rendering & Touch)  │
 ├───────────────────────┼───────────────────────┤
 │ • UART RX Task        │ • esp_lvgl_port timer │
 │ • UART TX Task        │ • 800x480 RGB DMA     │
 │ • cJSON Packet Parser │ • GT911 Touch Polling │
 │ • Boot Watchdog       │ • Screen Transitions  │
 └───────────────────────┴───────────────────────┘
```

---

## 4. UI Screens & Navigation

1. **Boot Screen**: 0–100% animated progress bar, live diagnostic status pills (ESP32-S3 Core, WiFi, Cloud Sync, Subsystem Sensors).
2. **Idle / Welcome Screen**: Standby kiosk mode with large interactive cards: *Existing Patient Login* and *New Patient Registration*.
3. **Login Flows**:
   - RFID Badge Tap verification prompt.
   - Fingerprint Scanner biometric verification prompt.
4. **New Patient Signup Flow**:
   - Badge allocation.
   - Profile demographic review.
   - Dual-fingerprint biometric enrollment steps.
5. **Interactive Vitals Dashboard**:
   - Real-time Height, Weight, and dynamic BMI Speedometer Gauge.
   - Oximeter card (SpO2 & Pulse).
   - Infrared temperature card.
   - **Manual Blood Pressure Section**: Interactive Systolic & Diastolic stepper adjustments with dynamic AHA cardiovascular classification badge (*Optimal*, *Elevated*, *Stage 1 HTN*, *Stage 2 HTN*, *Crisis*).
   - "Take Measurement" trigger and guided instructions modal.
6. **Patient History / Profile**: Longitudinal BMI & biometric trend graph.

---

## 5. Building & Flashing

### Build Firmware
```bash
pio run -d display_firmware
```

### Upload to Sunton Display
```bash
pio run -d display_firmware -t upload --upload-port /dev/ttyUSB0
```

### Monitor Serial Logs
```bash
pio device monitor -d display_firmware -b 115200
```
