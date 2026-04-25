#ifndef VOICE_GUIDANCE_H
#define VOICE_GUIDANCE_H

#include <Arduino.h>

// Voice track numbers (see VOICE_TRACK_ORDER.md for audio scripts)
enum VoiceTrack {
  VOICE_WELCOME = 1,
  VOICE_WIFI_CONNECTING = 2,
  VOICE_SYSTEM_READY = 3,
  VOICE_LOGIN_SCAN_RFID = 4,
  VOICE_LOGIN_RFID_DETECTED = 5,
  VOICE_LOGIN_FINGERPRINT_SCANNING = 6,
  VOICE_LOGIN_SUCCESS = 7,
  VOICE_LOGIN_FAILED = 8,
  VOICE_ENROLL_SCAN_RFID = 9,
  VOICE_ENROLL_RFID_DETECTED = 10,
  VOICE_ENROLL_FIRST_CAPTURE = 11,
  VOICE_ENROLL_REMOVE_FINGER = 12,
  VOICE_ENROLL_SECOND_CAPTURE = 13,
  VOICE_ENROLL_SUCCESS = 14,
  VOICE_ENROLL_FAILED = 15,
  VOICE_DASHBOARD_INSTRUCTIONS = 16,
  VOICE_MEASUREMENT_STEP_ON_SCALE = 17,
  VOICE_MEASURING_HEIGHT = 18,
  VOICE_PLACE_FINGER_OXIMETER = 19,
  VOICE_READING_OXIMETER = 20,
  VOICE_MEASUREMENTS_COMPLETE = 21,
  VOICE_SAVE_SUCCESS = 22,
  VOICE_LOGOUT = 23,
  VOICE_ERROR_USER_NOT_FOUND = 24,
  VOICE_ERROR_SYSTEM = 25,
  VOICE_ERROR_WIFI_FAILED = 26,
};

// Initialize voice guidance system
void voiceInit();

// Reset MP3 player to track 1
void voiceReset();

// Play next track (single pulse)
void voicePlayNext();

// Play specific track (calculates pulses needed from current position)
void voicePlayTrack(VoiceTrack track);

// Play track without waiting (non-blocking for setup)
void voicePlayTrackNoWait(VoiceTrack track);

// Get current track number
uint8_t voiceGetCurrentTrack();

#endif // VOICE_GUIDANCE_H
