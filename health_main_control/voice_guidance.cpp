#include "voice_guidance.h"
#include "config.h"

static uint8_t currentTrack = 1;
static const uint8_t TOTAL_TRACKS = 2;
static bool isAtTrackStart = true;
static volatile unsigned long trackStartTime = 0;
static volatile bool isTrackPlaying = false;
static TaskHandle_t voiceTaskHandle = NULL;

// Internal helper for pulses
void voicePressPlayPause() {
  digitalWrite(VOICE_PAUSE_PIN, HIGH);
  delay(VOICE_PULSE_DURATION_MS);
  digitalWrite(VOICE_PAUSE_PIN, LOW);
}

// Background task to handle auto-pausing at 9.5 seconds
void voiceBackgroundTask(void *pvParameters) {
  for (;;) {
    if (isTrackPlaying) {
      if (millis() - trackStartTime >= VOICE_PAUSE_BEFORE_END_MS) {
        voicePressPlayPause();
        isTrackPlaying = false;
        isAtTrackStart = false; // Now near the end
        Serial.println(F("Voice Guidance: Track auto-paused at 9.5s"));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void voiceInit() {
  pinMode(VOICE_NEXT_PIN, OUTPUT);
  pinMode(VOICE_PAUSE_PIN, OUTPUT);
  pinMode(VOICE_RESET_PIN, OUTPUT);
  
  digitalWrite(VOICE_NEXT_PIN, LOW);
  digitalWrite(VOICE_PAUSE_PIN, LOW);
  digitalWrite(VOICE_RESET_PIN, LOW);
  
  Serial.println(F("Voice Guidance: Initializing..."));
  
  voiceReset();
  
  // Create background task for auto-pausing
  if (voiceTaskHandle == NULL) {
    xTaskCreate(voiceBackgroundTask, "voice_task", 2048, NULL, 1, &voiceTaskHandle);
  }
  
  Serial.println(F("Voice Guidance: Ready (Background Task Active)"));
}

void voiceReset() {
  Serial.println(F("Voice Guidance: Resetting MP3 player..."));
  
  digitalWrite(VOICE_RESET_PIN, HIGH);
  delay(VOICE_RESET_DURATION_MS);
  digitalWrite(VOICE_RESET_PIN, LOW);
  
  delay(10000);  // Wait for MP3 player to boot
  
  currentTrack = 1;
  isAtTrackStart = true;
  isTrackPlaying = false;
  Serial.println(F("Voice Guidance: Reset complete, at Track 1"));
}

bool voiceIsPlaying() {
  return isTrackPlaying;
}

void voiceStop() {
  if (isTrackPlaying) {
    voicePressPlayPause();
    isTrackPlaying = false;
    isAtTrackStart = false;
    Serial.println(F("Voice Guidance: Track stopped manually"));
  }
}

void voiceStartTrack(VoiceTrack track) {
  uint8_t targetTrack = (uint8_t)track;
  
  if (targetTrack < 1 || targetTrack > TOTAL_TRACKS) {
    Serial.println(F("Voice Guidance: Invalid track number"));
    return;
  }

  // If already playing, stop it first
  if (isTrackPlaying) {
    voiceStop();
    delay(200); // Small gap between stop and next command
  }
  
  uint8_t nextPresses = 0;
  bool usePlayPause = false;

  // Determine how to reach the target track
  if (targetTrack == currentTrack && isAtTrackStart) {
    // We are already at the beginning of the track (e.g. after reset)
    usePlayPause = true;
  } else if (targetTrack >= currentTrack) {
    nextPresses = targetTrack - currentTrack;
    // If target == current but not at start, we must loop all the way around
    if (nextPresses == 0) nextPresses = TOTAL_TRACKS; 
  } else {
    // Need to loop around
    nextPresses = (TOTAL_TRACKS - currentTrack) + targetTrack;
  }
  
  if (usePlayPause) {
    Serial.print(F("Voice Guidance: Starting current Track "));
    Serial.println(targetTrack);
    voicePressPlayPause();
  } else {
    Serial.print(F("Voice Guidance: Jumping to Track "));
    Serial.println(targetTrack);
    for (uint8_t i = 0; i < nextPresses; i++) {
      digitalWrite(VOICE_NEXT_PIN, HIGH);
      delay(VOICE_PULSE_DURATION_MS);
      digitalWrite(VOICE_NEXT_PIN, LOW);
      delay(150); // Increased delay between pulses for reliability
    }
  }
  
  currentTrack = targetTrack;
  trackStartTime = millis();
  isTrackPlaying = true;
  isAtTrackStart = false;
}

void voicePlayNext() {
  uint8_t next = currentTrack + 1;
  if (next > TOTAL_TRACKS) next = 1;
  voicePlayTrack((VoiceTrack)next);
}

void voicePlayTrack(VoiceTrack track) {
  voiceStartTrack(track);
  
  // Blocking wait for the track to finish playing (until auto-paused by task)
  Serial.println(F("Voice Guidance: Waiting for track to finish (blocking)..."));
  while (isTrackPlaying) {
    delay(100);
  }
  Serial.println(F("Voice Guidance: Track finished"));
}

void voicePlayTrackNoWait(VoiceTrack track) {
  voiceStartTrack(track);
  Serial.println(F("Voice Guidance: Track started (non-blocking)"));
}

uint8_t voiceGetCurrentTrack() {
  return currentTrack;
}
