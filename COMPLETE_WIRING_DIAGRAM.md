# MediBot ESP32 - Complete Wiring Diagram

## ⚠️ IMPORTANT: Pin Conflicts Resolved

This document reflects the **UPDATED** pin assignments after resolving conflicts.

### **Changes Made**:
- ✅ SD Card moved to HSPI bus (GPIO 12, 13, 14, 15)
- ✅ Fingerprint sensor moved to GPIO 16, 17
- ✅ RFID remains on VSPI bus (GPIO 18, 19, 21, 23)
- ❌ PULSE_LED removed (conflicted with RFID MOSI)
- ❌ READ_LED removed (conflicted with SD MOSI)
- ❌ MODE_SWITCH removed (unused)

---

## 📋 Complete Pin Assignment Table

| GPIO | Function | Module | Direction | Notes |
|------|----------|--------|-----------|-------|
| 1 | MP1 | Motor Control | Output | Motor Pin 1 |
| 2 | MP2 | Motor Control | Output | Motor Pin 2 |
| 6 | UART TX | Display | Output | To display ESP32-S3 |
| 7 | UART RX | Display | Input | From display ESP32-S3 |
| 8 | I2C SDA | Oximeter | Bidirectional | MAX30102 data |
| 9 | I2C SCL | Oximeter | Output | MAX30102 clock |
| 12 | SPI MISO | SD Card | Input | HSPI bus |
| 13 | SPI MOSI | SD Card | Output | HSPI bus |
| 14 | SPI SCK | SD Card | Output | HSPI bus |
| 15 | SPI CS | SD Card | Output | HSPI bus |
| 16 | UART RX | Fingerprint | Input | From sensor |
| 17 | UART TX | Fingerprint | Output | To sensor |
| 18 | SPI SCK | RFID | Output | VSPI bus |
| 19 | SPI MISO | RFID | Input | VSPI bus |
| 21 | SPI CS | RFID | Output | VSPI bus |
| 23 | SPI MOSI | RFID | Output | VSPI bus |
| 30 | GPIO | Motor Relay | Output | Relay control |
| 42 | GPIO | Buzzer | Output | Audio feedback |
| 47 | GPIO | RFID Reset | Output | MFRC522 reset |

**Total Pins Used**: 19 / 48 available

---

## 🔌 Module-by-Module Wiring

### **1. RFID Module (MFRC522)**

```
MFRC522 Module          ESP32 Main Controller
┌──────────────┐        ┌──────────────────┐
│              │        │                  │
│  SDA (CS)    │────────│  GPIO 21         │
│  SCK         │────────│  GPIO 18         │
│  MOSI        │────────│  GPIO 23         │
│  MISO        │────────│  GPIO 19         │
│  IRQ         │        │  (not connected) │
│  GND         │────────│  GND             │
│  RST         │────────│  GPIO 47         │
│  3.3V        │────────│  3.3V            │
│              │        │                  │
└──────────────┘        └──────────────────┘
```

**Notes**:
- Uses VSPI bus (default SPI for ESP32)
- IRQ pin not used in current implementation
- Add 100nF capacitor between VCC and GND near module

---

### **2. SD Card Module**

```
SD Card Module          ESP32 Main Controller
┌──────────────┐        ┌──────────────────┐
│              │        │                  │
│  CS          │────────│  GPIO 15         │
│  SCK         │────────│  GPIO 14         │
│  MOSI        │────────│  GPIO 13         │
│  MISO        │────────│  GPIO 12         │
│  VCC         │────────│  3.3V            │
│  GND         │────────│  GND             │
│              │        │                  │
└──────────────┘        └──────────────────┘
```

**Notes**:
- Uses HSPI bus (secondary SPI for ESP32)
- Keep wires short (< 15cm) for reliable communication
- Add 10kΩ pull-up resistors on MISO, MOSI, SCK if needed
- SD card must be formatted as FAT32
- Use Class 10 or higher SD card

---

### **3. Fingerprint Sensor (Adafruit or compatible)**

```
Fingerprint Sensor      ESP32 Main Controller
┌──────────────┐        ┌──────────────────┐
│              │        │                  │
│  TX (Yellow) │────────│  GPIO 16 (RX)    │
│  RX (White)  │────────│  GPIO 17 (TX)    │
│  VCC (Red)   │────────│  5V or 3.3V      │
│  GND (Black) │────────│  GND             │
│  TOUCH       │        │  (not connected) │
│              │        │                  │
└──────────────┘        └──────────────────┘
```

**Notes**:
- UART communication at 57600 baud
- Some sensors require 5V, others work with 3.3V (check datasheet)
- If using 5V sensor, consider level shifter for RX/TX lines
- TOUCH pin can be used for wake-up (optional)

---

### **4. Oximeter (MAX30102)**

```
MAX30102 Module         ESP32 Main Controller
┌──────────────┐        ┌──────────────────┐
│              │        │                  │
│  SDA         │────────│  GPIO 8          │
│  SCL         │────────│  GPIO 9          │
│  INT         │        │  (not connected) │
│  VCC         │────────│  3.3V            │
│  GND         │────────│  GND             │
│              │        │                  │
└──────────────┘        └──────────────────┘
```

**Notes**:
- I2C communication
- INT pin can be used for interrupt-driven reading (optional)
- Add 4.7kΩ pull-up resistors on SDA and SCL if not on module
- Keep sensor clean for accurate readings

---

### **5. Display Unit (ESP32-S3 with 7" LCD)**

```
Display ESP32-S3        ESP32 Main Controller
┌──────────────┐        ┌──────────────────┐
│              │        │                  │
│  GPIO 18 (RX)│────────│  GPIO 6 (TX)     │
│  GPIO 17 (TX)│────────│  GPIO 7 (RX)     │
│  GND         │────────│  GND             │
│              │        │                  │
└──────────────┘        └──────────────────┘
```

**Notes**:
- UART communication at 115200 baud
- JSON protocol for commands and data
- Common ground is essential
- No level shifter needed (both 3.3V)

---

### **6. Motor Control**

```
Motor Driver            ESP32 Main Controller
┌──────────────┐        ┌──────────────────┐
│              │        │                  │
│  IN1         │────────│  GPIO 1 (MP1)    │
│  IN2         │────────│  GPIO 2 (MP2)    │
│  VCC         │────────│  External 12V    │
│  GND         │────────│  GND (common)    │
│              │        │                  │
└──────────────┘        └──────────────────┘

Relay Module            ESP32 Main Controller
┌──────────────┐        ┌──────────────────┐
│              │        │                  │
│  IN          │────────│  GPIO 30         │
│  VCC         │────────│  5V              │
│  GND         │────────│  GND             │
│              │        │                  │
└──────────────┘        └──────────────────┘
```

**Notes**:
- Motor driver requires external power supply
- Relay for high-power motor control
- Add flyback diodes across motor terminals

---

### **7. Buzzer**

```
Buzzer/Speaker          ESP32 Main Controller
┌──────────────┐        ┌──────────────────┐
│              │        │                  │
│  Positive    │────────│  GPIO 42         │
│  Negative    │────────│  GND             │
│              │        │                  │
└──────────────┘        └──────────────────┘
```

**Notes**:
- Active buzzer (has internal oscillator)
- Add 100Ω resistor in series if too loud
- For passive buzzer, use PWM for different tones

---

## 🔋 Power Supply Recommendations

### **Power Requirements**

| Component | Voltage | Current | Notes |
|-----------|---------|---------|-------|
| ESP32 Main | 3.3V | 500mA | Peak during WiFi |
| RFID Module | 3.3V | 50mA | Active reading |
| SD Card | 3.3V | 200mA | Peak during write |
| Fingerprint | 3.3-5V | 150mA | Check datasheet |
| Oximeter | 3.3V | 50mA | LED on |
| Display ESP32 | 5V | 1A | With LCD backlight |
| Motors | 12V | 2A | External supply |

**Total 3.3V**: ~1A  
**Total 5V**: ~1.5A  
**Total 12V**: ~2A

### **Recommended Power Supply**

```
Power Supply Setup
┌─────────────────────────────────────────┐
│                                         │
│  12V 3A Power Supply                    │
│         │                               │
│         ├──► 12V → Motor Driver         │
│         │                               │
│         └──► 12V → Buck Converter       │
│                    │                    │
│                    ├──► 5V 2A → Display │
│                    │         │          │
│                    │         └──► Relay │
│                    │                    │
│                    └──► 3.3V 1.5A       │
│                         │               │
│                         ├──► ESP32 Main │
│                         ├──► RFID       │
│                         ├──► SD Card    │
│                         ├──► Fingerprint│
│                         └──► Oximeter   │
│                                         │
└─────────────────────────────────────────┘
```

---

## ⚡ Important Electrical Notes

### **1. Common Ground**
- All modules MUST share common ground
- Star ground configuration recommended
- Keep ground traces thick

### **2. Decoupling Capacitors**
Add near each module:
- 100nF ceramic capacitor between VCC and GND
- 10µF electrolytic capacitor on main power rails

### **3. Pull-up Resistors**
- I2C: 4.7kΩ on SDA and SCL (if not on module)
- SPI: 10kΩ on MISO, MOSI, SCK (if long wires)

### **4. Wire Gauge**
- Signal wires: 24-26 AWG
- Power wires (3.3V, 5V): 20-22 AWG
- Motor power (12V): 18-20 AWG

### **5. Wire Length**
- SPI buses: < 15cm (shorter is better)
- I2C: < 30cm
- UART: < 1m
- Power: As short as possible

---

## 🧪 Testing Procedure

### **Step 1: Power Test**
1. Connect only power supply
2. Measure voltages: 3.3V, 5V, 12V
3. Check for shorts with multimeter

### **Step 2: Module Test (one at a time)**
1. Connect RFID → Test with example code
2. Connect SD Card → Test with example code
3. Connect Fingerprint → Test with example code
4. Connect Oximeter → Test with example code
5. Connect Display → Test UART communication

### **Step 3: Integration Test**
1. Connect all modules
2. Upload MediBot firmware
3. Check Serial Monitor for initialization
4. Test each function individually

---

## 🔧 Troubleshooting

### **RFID Not Detected**
- Check SPI wiring (MOSI, MISO, SCK, CS)
- Verify 3.3V power
- Try different RFID card
- Check RST pin connection

### **SD Card Not Detected**
- Verify FAT32 format
- Check SPI wiring on HSPI bus
- Try different SD card
- Add pull-up resistors

### **Fingerprint Sensor Not Responding**
- Check UART wiring (RX ↔ TX crossover)
- Verify baud rate (57600)
- Check power supply (3.3V or 5V)
- Test with example code

### **Oximeter No Reading**
- Check I2C wiring (SDA, SCL)
- Verify pull-up resistors
- Clean sensor surface
- Check finger placement

### **Display Not Communicating**
- Check UART wiring (RX ↔ TX crossover)
- Verify baud rate (115200)
- Check common ground
- Test with simple echo program

---

## 📸 Visual Reference

```
ESP32 Main Controller Pinout (Top View)
┌─────────────────────────────────────────┐
│                                         │
│  [USB]                                  │
│                                         │
│  1  ●  MP1                              │
│  2  ●  MP2                              │
│  3  ●                                   │
│  4  ●  (removed MODE_SWITCH)            │
│  5  ●                                   │
│  6  ●  Display TX                       │
│  7  ●  Display RX                       │
│  8  ●  I2C SDA                          │
│  9  ●  I2C SCL                          │
│ 10  ●                                   │
│ 11  ●  (removed PULSE_LED)              │
│ 12  ●  SD MISO                          │
│ 13  ●  SD MOSI                          │
│ 14  ●  SD SCK                           │
│ 15  ●  SD CS                            │
│ 16  ●  Fingerprint RX                   │
│ 17  ●  Fingerprint TX                   │
│ 18  ●  RFID SCK                         │
│ 19  ●  RFID MISO                        │
│ 20  ●                                   │
│ 21  ●  RFID CS                          │
│ 22  ●                                   │
│ 23  ●  RFID MOSI                        │
│     ...                                 │
│ 30  ●  Motor Relay                      │
│     ...                                 │
│ 42  ●  Buzzer                           │
│     ...                                 │
│ 47  ●  RFID Reset                       │
│                                         │
└─────────────────────────────────────────┘
```

---

## ✅ Pre-Flight Checklist

Before powering on:

- [ ] All connections verified against this diagram
- [ ] No short circuits (multimeter test)
- [ ] Correct voltages on power rails
- [ ] Common ground connected
- [ ] Decoupling capacitors installed
- [ ] SD card formatted as FAT32
- [ ] Firmware uploaded successfully
- [ ] Serial Monitor ready (115200 baud)

---

**Document Version**: 2.0 (Updated after pin conflict resolution)  
**Last Updated**: 2025-01-21  
**Status**: ✅ Conflict-Free and Ready for Implementation
