#ifndef VOICE_GUIDANCE_H
#define VOICE_GUIDANCE_H

#include <Arduino.h>

// Voice track numbers
enum VoiceTrack {
  VOICE_INIT = 1,           // medibot_init voice
  VOICE_SYSTEM_INSTRUC = 2, // system_instruc voice
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

// Check if a track is currently playing
bool voiceIsPlaying();

// Stop current track
void voiceStop();

// Get current track number
uint8_t voiceGetCurrentTrack();

#endif // VOICE_GUIDANCE_H
