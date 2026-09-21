#include "voice_guidance.hpp"
#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "VOICE_GUIDANCE_CPP";

namespace medicbot {

static VoiceGuidance s_voice_instance;

VoiceGuidance &getVoice() {
    return s_voice_instance;
}

static uint8_t s_current_track = 1;
static const uint8_t TOTAL_TRACKS = 2;
static bool s_is_at_track_start = true;
static volatile int64_t s_track_start_time_us = 0;
static TaskHandle_t s_voice_task_handle = nullptr;
static SemaphoreHandle_t s_voice_mutex = nullptr;

static void pulse_pin(gpio_num_t pin, uint32_t duration_ms) {
    gpio_set_level(pin, 1);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_set_level(pin, 0);
}

static void voice_press_play_pause(void) {
    pulse_pin(PIN_VOICE_PLAY_PAUSE, VOICE_PULSE_MS);
}

static void voice_background_task(void *pvParameters) {
    ESP_LOGI(TAG, "Voice background task started");

    while (1) {
        if (s_voice_instance.isPlaying()) {
            int64_t elapsed_ms = (esp_timer_get_time() - s_track_start_time_us) / 1000;
            if (elapsed_ms >= VOICE_TRACK_MAX_MS) {
                xSemaphoreTake(s_voice_mutex, portMAX_DELAY);
                voice_press_play_pause();
                s_voice_instance.stop();
                s_is_at_track_start = false;

                if (s_current_track == static_cast<uint8_t>(VoiceTrack::INSTRUCTIONS)) {
                    vTaskDelay(pdMS_TO_TICKS(200));
                    s_voice_instance.powerDown();
                    ESP_LOGI(TAG, "Final track finished; module powered down.");
                } else {
                    ESP_LOGI(TAG, "Track %d auto-paused at duration limit", s_current_track);
                }
                xSemaphoreGive(s_voice_mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

VoiceGuidance::VoiceGuidance()
    : m_initialized(false), m_playing(false) {}

VoiceGuidance::~VoiceGuidance() {}

esp_err_t VoiceGuidance::begin() {
    if (m_initialized) return ESP_OK;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_VOICE_NEXT) |
                        (1ULL << PIN_VOICE_PLAY_PAUSE) |
                        (1ULL << PIN_VOICE_RESET),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure voice GPIOs: %s", esp_err_to_name(err));
        return err;
    }

    gpio_set_level(PIN_VOICE_NEXT, 0);
    gpio_set_level(PIN_VOICE_PLAY_PAUSE, 0);
    gpio_set_level(PIN_VOICE_RESET, 0);

    if (!s_voice_mutex) {
        s_voice_mutex = xSemaphoreCreateMutex();
    }

    if (!s_voice_task_handle) {
        xTaskCreatePinnedToCore(voice_background_task, "voice_task", STACK_VOICE,
                                nullptr, PRIO_VOICE, &s_voice_task_handle, CORE_REALTIME_APP);
    }

    reset();
    m_initialized = true;
    ESP_LOGI(TAG, "Voice guidance initialized successfully");
    return ESP_OK;
}

void VoiceGuidance::reset() {
    xSemaphoreTake(s_voice_mutex, portMAX_DELAY);
    ESP_LOGI(TAG, "Resetting MP3 player...");
    gpio_set_level(PIN_VOICE_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    gpio_set_level(PIN_VOICE_RESET, 0);

    s_current_track = 1;
    s_is_at_track_start = true;
    m_playing = false;
    xSemaphoreGive(s_voice_mutex);
}

void VoiceGuidance::play(VoiceTrack track) {
    uint8_t target = static_cast<uint8_t>(track);
    if (target < 1 || target > TOTAL_TRACKS) {
        ESP_LOGW(TAG, "Invalid track requested: %d", target);
        return;
    }

    xSemaphoreTake(s_voice_mutex, portMAX_DELAY);

    gpio_set_level(PIN_VOICE_RESET, 0);

    if (m_playing) {
        voice_press_play_pause();
        m_playing = false;
        s_is_at_track_start = false;
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    uint8_t next_presses = 0;
    bool use_play_pause = false;

    if (target == s_current_track && s_is_at_track_start) {
        use_play_pause = true;
    } else if (target >= s_current_track) {
        next_presses = target - s_current_track;
        if (next_presses == 0) next_presses = TOTAL_TRACKS;
    } else {
        next_presses = (TOTAL_TRACKS - s_current_track) + target;
    }

    if (use_play_pause) {
        ESP_LOGI(TAG, "Playing current Track %d", target);
        voice_press_play_pause();
    } else {
        ESP_LOGI(TAG, "Advancing to Track %d with %d pulses", target, next_presses);
        for (uint8_t i = 0; i < next_presses; i++) {
            pulse_pin(PIN_VOICE_NEXT, VOICE_PULSE_MS);
            vTaskDelay(pdMS_TO_TICKS(150));
        }
    }

    s_current_track = target;
    s_track_start_time_us = esp_timer_get_time();
    m_playing = true;
    s_is_at_track_start = false;

    xSemaphoreGive(s_voice_mutex);
}

void VoiceGuidance::stop() {
    xSemaphoreTake(s_voice_mutex, portMAX_DELAY);
    if (m_playing) {
        voice_press_play_pause();
        m_playing = false;
        s_is_at_track_start = false;
        ESP_LOGI(TAG, "Voice playback stopped");
    }
    xSemaphoreGive(s_voice_mutex);
}

void VoiceGuidance::powerDown() {
    xSemaphoreTake(s_voice_mutex, portMAX_DELAY);
    gpio_set_level(PIN_VOICE_RESET, 1);
    m_playing = false;
    s_is_at_track_start = false;
    ESP_LOGI(TAG, "Voice module powered down");
    xSemaphoreGive(s_voice_mutex);
}

} // namespace medicbot

// C Bridge
extern "C" {

esp_err_t voice_guidance_init(void) {
    return medicbot::getVoice().begin();
}

void voice_guidance_reset(void) {
    medicbot::getVoice().reset();
}

void voice_guidance_play(voice_track_t track) {
    medicbot::getVoice().play(static_cast<medicbot::VoiceTrack>(track));
}

void voice_guidance_stop(void) {
    medicbot::getVoice().stop();
}

bool voice_guidance_is_playing(void) {
    return medicbot::getVoice().isPlaying();
}

void voice_guidance_power_down(void) {
    medicbot::getVoice().powerDown();
}

}
