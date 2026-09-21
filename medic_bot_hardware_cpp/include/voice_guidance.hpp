#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
namespace medicbot {

enum class VoiceTrack : uint8_t {
    INIT = 1,
    INSTRUCTIONS = 2
};

class VoiceGuidance {
public:
    VoiceGuidance();
    ~VoiceGuidance();

    esp_err_t begin();
    void play(VoiceTrack track);
    void reset();
    void stop();
    bool isPlaying() const { return m_playing; }
    void powerDown();

private:
    bool m_initialized;
    volatile bool m_playing;
};

VoiceGuidance &getVoice();

} // namespace medicbot

extern "C" {
#endif

typedef enum {
    VOICE_TRACK_INIT = 1,
    VOICE_TRACK_INSTRUCTIONS = 2,
} voice_track_t;

esp_err_t voice_guidance_init(void);
void voice_guidance_reset(void);
void voice_guidance_play(voice_track_t track);
void voice_guidance_stop(void);
bool voice_guidance_is_playing(void);
void voice_guidance_power_down(void);

#ifdef __cplusplus
}
#endif
