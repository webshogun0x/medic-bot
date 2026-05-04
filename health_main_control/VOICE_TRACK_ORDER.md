# MediBot Voice Guidance Track Order

## MP3 Player Configuration
- **Next Button Pin:** GPIO 10 (connected to MP3 player NEXT button)
- **Play/Pause Button Pin:** GPIO 46 (connected to MP3 player PLAY/PAUSE button)
- **Reset Pin:** GPIO 48 (connected to MP3 player power via relay)
- **Pulse Duration:** 150ms (adjustable in config.h if needed)
- **Track Format:** MP3 files numbered sequentially (001.mp3, 002.mp3, etc.)
- **Track Duration:** Each audio track MUST be exactly 10 seconds (pad with silence if needed)
- **Auto-Pause Logic:** System pauses at 9.5 seconds to prevent auto-advance to next track
- **Reset Method:** Power cycle the MP3 player on system boot using a relay/transistor

---

## Track Sequence & Audio Scripts

### **Track 01** - System Boot Welcome
**Trigger:** `setup()` after initialization
**Audio:** *"Welcome to MediBot, your personal health monitoring assistant. Please wait while the system initializes."*

---

### **Track 02** - WiFi Connecting
**Trigger:** When WiFi connection starts
**Audio:** *"Connecting to WiFi and Firebase database. This may take a few moments."*

---

### **Track 03** - System Ready
**Trigger:** After WiFi and Firebase successfully connect
**Audio:** *"System ready. If you are an existing user, please press the Login button. If you are a new user, please press the Enroll button."*

---

### **Track 04** - Login: Scan RFID
**Trigger:** Login process starts (`loginProcess()`)
**Audio:** *"Login selected. Please scan your RFID card on the reader."*

---

### **Track 05** - Login: RFID Detected
**Trigger:** After RFID is successfully read during login
**Audio:** *"RFID card detected. Please place your finger on the fingerprint scanner."*

---

### **Track 06** - Login: Fingerprint Scanning
**Trigger:** While waiting for fingerprint during login
**Audio:** *"Scanning fingerprint. Please keep your finger steady on the sensor."*

---

### **Track 07** - Login Success
**Trigger:** After successful fingerprint verification
**Audio:** *"Login successful. Welcome back! Loading your dashboard."*

---

### **Track 08** - Login Failed
**Trigger:** If fingerprint doesn't match or not found
**Audio:** *"Login failed. Fingerprint not recognized. Please try again or enroll as a new user."*

---

### **Track 09** - Enroll: Scan RFID
**Trigger:** Enrollment process starts (`enrollmentProcess()`)
**Audio:** *"Enrollment selected. Please scan your RFID card on the reader to register."*

---

### **Track 10** - Enroll: RFID Detected
**Trigger:** After RFID is successfully read during enrollment
**Audio:** *"RFID card detected. Now we will register your fingerprint. Please place your finger on the scanner."*

---

### **Track 11** - Enroll: First Fingerprint Capture
**Trigger:** During first fingerprint capture in enrollment
**Audio:** *"Place your finger on the sensor. Hold steady."*

---

### **Track 12** - Enroll: Remove Finger
**Trigger:** After first capture, before second capture
**Audio:** *"Good. Now remove your finger and place it again on the sensor."*

---

### **Track 13** - Enroll: Second Fingerprint Capture
**Trigger:** During second fingerprint capture
**Audio:** *"Place your finger again. Hold steady."*

---

### **Track 14** - Enrollment Success
**Trigger:** After successful fingerprint enrollment
**Audio:** *"Enrollment successful! Your fingerprint has been registered. Welcome to MediBot!"*

---

### **Track 15** - Enrollment Failed
**Trigger:** If enrollment fails
**Audio:** *"Enrollment failed. Please try again. Make sure your finger is clean and dry."*

---

### **Track 16** - Dashboard Instructions
**Trigger:** When dashboard loads (`STATE_DASHBOARD`)
**Audio:** *"You are now on the dashboard. To take your health measurements, please press the Read button."*

---

### **Track 17** - Measurement Start: Step on Scale
**Trigger:** Before "READ_OXIMETER" command received
**Audio:** *"Starting measurements. Please step on the scale and stand still. Do not move."*

---

### **Track 18** - Measuring Height
**Trigger:** During height measurement (after weight stabilizes)
**Audio:** *"Measuring your height. Please stand straight and remain still."*

---

### **Track 19** - Place Finger on Oximeter
**Trigger:** After height/weight measurement complete
**Audio:** *"Now place your finger gently on the pulse oximeter sensor. Do not press too hard."*

---

### **Track 20** - Reading Oximeter
**Trigger:** While oximeter is reading
**Audio:** *"Reading your heart rate and oxygen levels. Please keep your finger still for thirty seconds."*

---

### **Track 21** - Measurements Complete
**Trigger:** After all sensor readings complete
**Audio:** *"All measurements complete. for your measurement to be displayed on the screen press READ. Press Save to store your data."*

---

### **Track 22** - Save Success
**Trigger:** After successful Firebase save
**Audio:** *"Data saved successfully. You can view your health trends on the web dashboard."*

---

### **Track 23** - Logout
**Trigger:** When user logs out
**Audio:** *"Logging out. Thank you for using MediBot. Stay healthy!"*

---

### **Track 24** - Error: User Not Found
**Trigger:** If RFID not registered in system
**Audio:** *"Error. RFID card not found in the system. Please enroll as a new user first."*

---

### **Track 25** - System Error
**Trigger:** General system errors
**Audio:** *"System error occurred. Please restart the device or contact support."*

---

### **Track 26** - WiFi Connection Failed
**Trigger:** If WiFi fails to connect after retries
**Audio:** *"Unable to connect to WiFi. Please check your network settings and restart the system."*

---

## Reset Options

---

### **MP3 Player Power Cycle**
- Add a relay/transistor on GPIO 48 to control MP3 player power
- On system boot: Turn OFF power for 500ms, then turn ON
- This resets the MP3 player to Track 01

---

### **Option 2: Hardware Reset Pin**
- If MP3 player has a RESET pin, connect to GPIO 48
- Pulse LOW for 200ms on boot to reset

---

## Implementation Notes

1. **Timing:** Add 2-5 second delays after triggering voice to allow audio to play
2. **Non-blocking:** Consider using timers instead of delay() for better responsiveness
3. **Track Verification:** Test each track number matches the expected audio
4. **Volume:** Ensure MP3 player volume is pre-configured (hardware potentiometer or separate volume control pin)
5. **Audio Quality:** Use clear, slow-paced speech for better understanding
6. **Language:** Record in the primary language of your users

---

## Wiring Diagram

```
ESP32-S3 GPIO 10 ──[1kΩ]── MP3 Player NEXT Button Input

ESP32-S3 GPIO 9  ──[1kΩ]── MP3 Player PLAY/PAUSE Button Input

ESP32-S3 GPIO 48 ──[Relay]── MP3 Player VCC (for power reset)
```

## How It Works

1. **Press NEXT button** (GPIO 10) → Track starts playing
2. **Wait 9.5 seconds** → Track plays (with 0.5s silence padding at end)
3. **Press PLAY/PAUSE button** (GPIO 9) → Pauses before track ends
4. **Track is paused** → Prevents auto-advance to next track
5. **When next track needed** → Press NEXT button → Repeat from step 1

**CRITICAL:** Each audio file MUST be exactly 10 seconds long. Pad shorter audio with silence at the end.

---

## Testing Checklist

- [ ] All 26 tracks recorded and numbered correctly (001.mp3 to 026.mp3)
- [ ] Each track is EXACTLY 10 seconds long (pad with silence if needed)
- [ ] Pulse duration tested (adjust VOICE_PULSE_DURATION_MS if needed)
- [ ] Reset mechanism working on boot
- [ ] PLAY/PAUSE button pauses at 9.5 seconds correctly
- [ ] No auto-advance to next track after pause
- [ ] NEXT button advances to next track correctly
- [ ] Audio volume is appropriate
- [ ] Track sequence matches program flow
