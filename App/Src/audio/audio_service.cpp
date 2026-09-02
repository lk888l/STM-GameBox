#include "audio/audio_service.hpp"

#include "platform/buzzer_timer.h"

namespace gamebox::audio {

namespace {

constexpr std::uint32_t kAudioStackDepth = 192U;
constexpr TickType_t kServicePeriod = pdMS_TO_TICKS(5U);
StackType_t audio_stack[kAudioStackDepth];
StaticTask_t audio_task_control_block;

} // namespace

AudioService::AudioService()
    : AppTask("audio", 3U, audio_stack, kAudioStackDepth, audio_task_control_block)
{
}

bool AudioService::onInitialize()
{
    dropped_tones_.store(0U, std::memory_order_relaxed);
    queue_ = xQueueCreateStatic(kQueueLength,
                                sizeof(Tone),
                                queue_storage_,
                                &queue_control_block_);
    GameBox_Buzzer_Stop();
    return queue_ != nullptr && start();
}

bool AudioService::onDeinitialize()
{
    enabled_.store(false, std::memory_order_release);
    if (!stop()) {
        GameBox_Buzzer_Stop();
        return false;
    }
    GameBox_Buzzer_Stop();
    queue_ = nullptr;
    return true;
}

bool AudioService::play(const Tone tone)
{
    if (!enabled_.load(std::memory_order_acquire) || queue_ == nullptr ||
        tone.frequency_hz < 100U || tone.frequency_hz > 5000U ||
        tone.duration_ms == 0U) {
        return false;
    }
    if (xQueueSendToBack(queue_, &tone, 0U) == pdPASS) {
        return true;
    }
    Tone discarded{};
    (void)xQueueReceive(queue_, &discarded, 0U);
    (void)dropped_tones_.fetch_add(1U, std::memory_order_relaxed);
    return xQueueSendToBack(queue_, &tone, 0U) == pdPASS;
}

void AudioService::setEnabled(const bool enabled)
{
    enabled_.store(enabled, std::memory_order_release);
    if (!enabled && queue_ != nullptr) {
        (void)xQueueReset(queue_);
    }
}

void AudioService::run()
{
    bool playing = false;
    TickType_t stop_at = 0U;
    while (!shouldExit()) {
        Tone newest{};
        Tone queued{};
        bool received = false;
        while (xQueueReceive(queue_, &queued, 0U) == pdPASS) {
            newest = queued;
            received = true;
        }

        const TickType_t now = xTaskGetTickCount();
        if (!enabled_.load(std::memory_order_acquire)) {
            if (playing) {
                GameBox_Buzzer_Stop();
                playing = false;
            }
        } else if (received) {
            playing = GameBox_Buzzer_Start(newest.frequency_hz) == HAL_OK;
            stop_at = now + pdMS_TO_TICKS(newest.duration_ms);
        } else if (playing && static_cast<std::int32_t>(now - stop_at) >= 0) {
            GameBox_Buzzer_Stop();
            playing = false;
        }
        waitInterruptible(kServicePeriod);
    }
    GameBox_Buzzer_Stop();
}

} // namespace gamebox::audio
