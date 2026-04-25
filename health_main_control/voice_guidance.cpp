#include "voice_guidance.h"
#include "config.h"

static uint8_t currentTrack = 1;
static const uint8_t TOTAL_TRACKS = 26;

void voiceInit() {
  pinMode(VOICE_NEXT_PIN, OUTPUT);
  pinMode(VOICE_PAUSE_PIN, OUTPUT);
  pinMode(VOICE_RESET_PIN, OUTPUT);
  
  digitalWrite(VOICE_NEXT_PIN, LOW);
  digitalWrite(VOICE_PAUSE_PIN, LOW);
  digitalWrite(VOICE_RESET_PIN, LOW);  // Assuming active LOW reset
  
  Serial.println(F("Voice Guidance: Initializing..."));
  
  // Reset MP3 player to Track 1
  voiceReset();
  
  Serial.println(F("Voice Guidance: Ready"));
}

void voiceReset() {
  Serial.println(F("Voice Guidance: Resetting MP3 player..."));
  
  // Power cycle or reset the MP3 player
  digitalWrite(VOICE_RESET_PIN, HIGH);   // Turn off power or assert reset
  delay(VOICE_RESET_DURATION_MS);
  digitalWrite(VOICE_RESET_PIN, LOW);  // Turn on power or release reset
  
  delay(10000);  // Wait for MP3 player to boot
  
  currentTrack = 1;
  Serial.println(F("Voice Guidance: Reset complete, at Track 1"));
}

void voicePressPlayPause() {
  // Send pulse to PLAY/PAUSE button
  digitalWrite(VOICE_PAUSE_PIN, HIGH);
  delay(VOICE_PULSE_DURATION_MS);
  digitalWrite(VOICE_PAUSE_PIN, LOW);
}

void voicePlayNext() {
  // Send pulse to trigger next track
  digitalWrite(VOICE_NEXT_PIN, HIGH);
  delay(VOICE_PULSE_DURATION_MS);
  digitalWrite(VOICE_NEXT_PIN, LOW);
  
  // Update track counter
  currentTrack++;
  if (currentTrack > TOTAL_TRACKS) {
    currentTrack = 1;  // Loop back to track 1
  }
  
  Serial.print(F("Voice Guidance: Playing Track "));
  Serial.println(currentTrack);
  
  // Wait for track to almost finish (9.5 seconds)
  delay(VOICE_PAUSE_BEFORE_END_MS);
  
  // Press PLAY/PAUSE to pause before track ends (prevents auto-advance)
  voicePressPlayPause();
  
  Serial.println(F("Voice Guidance: Track paused"));
}

void voicePlayTrack(VoiceTrack track) {
  uint8_t targetTrack = (uint8_t)track;
  
  if (targetTrack < 1 || targetTrack > TOTAL_TRACKS) {
    Serial.println(F("Voice Guidance: Invalid track number"));
    return;
  }
  
  Serial.print(F("Voice Guidance: Jumping to Track "));
  Serial.println(targetTrack);
  
  // Calculate how many NEXT presses needed
  uint8_t nextPresses = 0;
  
  if (targetTrack >= currentTrack) {
    nextPresses = targetTrack - currentTrack;
  } else {
    // Need to loop around
    nextPresses = (TOTAL_TRACKS - currentTrack) + targetTrack;
  }
  
  // Send NEXT pulses quickly to jump to target track
  for (uint8_t i = 0; i < nextPresses; i++) {
    digitalWrite(VOICE_NEXT_PIN, HIGH);
    delay(VOICE_PULSE_DURATION_MS);
    digitalWrite(VOICE_NEXT_PIN, LOW);
    delay(100);  // Short delay between pulses
  }
  
  currentTrack = targetTrack;
  
  Serial.print(F("Voice Guidance: Now at Track "));
  Serial.println(currentTrack);
  
  // Wait for track to almost finish (9.5 seconds)
  delay(VOICE_PAUSE_BEFORE_END_MS);
  
  // Press PLAY/PAUSE to pause before track ends
  voicePressPlayPause();
  
  Serial.println(F("Voice Guidance: Track paused"));
}

void voicePlayTrackNoWait(VoiceTrack track) {
  uint8_t targetTrack = (uint8_t)track;
  
  if (targetTrack < 1 || targetTrack > TOTAL_TRACKS) {
    Serial.println(F("Voice Guidance: Invalid track number"));
    return;
  }
  
  Serial.print(F("Voice Guidance: Starting Track "));
  Serial.println(targetTrack);
  
  // Calculate how many NEXT presses needed
  uint8_t nextPresses = 0;
  
  if (targetTrack >= currentTrack) {
    nextPresses = targetTrack - currentTrack;
  } else {
    // Need to loop around
    nextPresses = (TOTAL_TRACKS - currentTrack) + targetTrack;
  }
  
  // Send NEXT pulses quickly to jump to target track
  for (uint8_t i = 0; i < nextPresses; i++) {
    digitalWrite(VOICE_NEXT_PIN, HIGH);
    delay(VOICE_PULSE_DURATION_MS);
    digitalWrite(VOICE_NEXT_PIN, LOW);
    delay(100);  // Short delay between pulses
  }
  
  currentTrack = targetTrack;
  
  Serial.print(F("Voice Guidance: Track "));
  Serial.print(currentTrack);
  Serial.println(F(" playing (non-blocking)"));
  
  // Don't wait - let it play in background
  // Caller is responsible for pausing later if needed
}

uint8_t voiceGetCurrentTrack() {
  return currentTrack;
}
