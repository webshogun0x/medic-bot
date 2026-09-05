#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_TRACK_INIT = 1,           // Track 1: medibot_init
    VOICE_TRACK_INSTRUCTIONS = 2,   // Track 2: system_instruc
} voice_track_t;

/**
 * @brief Initialize Voice Guidance GPIO pins and start background timer/task.
 * @return ESP_OK on success.
 */
esp_err_t voice_guidance_init(void);

/**
 * @brief Reset the MP3 player to Track 1 (non-blocking).
 */
void voice_guidance_reset(void);

/**
 * @brief Start playing a specific track asynchronously.
 * @param track Track number to play.
 */
void voice_guidance_play(voice_track_t track);

/**
 * @brief Stop audio playback immediately.
 */
void voice_guidance_stop(void);

/**
 * @brief Check if audio is currently playing.
 * @return true if playing, false otherwise.
 */
bool voice_guidance_is_playing(void);

/**
 * @brief Power down MP3 module.
 */
void voice_guidance_power_down(void);

#ifdef __cplusplus
}
#endif
