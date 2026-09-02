#pragma once

#include <atomic>
#include <cstdint>

#include "app/app_module.hpp"
#include "app/app_task.hpp"

extern "C" {
#include "queue.h"
}

namespace gamebox::audio {

struct Tone {
    std::uint16_t frequency_hz{0U};
    std::uint16_t duration_ms{0U};
};

class AudioService final : public app::AppModule, private app::AppTask {
public:
    AudioService();

    [[nodiscard]] etl::string_view name() const override { return "audio"; }
    [[nodiscard]] bool play(Tone tone);
    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const
    {
        return enabled_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint32_t droppedToneCount() const
    {
        return dropped_tones_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t stackHeadroomBytes() const
    {
        return app::AppTask::stackHeadroomBytes();
    }

protected:
    [[nodiscard]] bool onInitialize() override;
    [[nodiscard]] bool onDeinitialize() override;

private:
    static constexpr UBaseType_t kQueueLength = 4U;

    void run() override;

    QueueHandle_t queue_{nullptr};
    StaticQueue_t queue_control_block_{};
    alignas(Tone) std::uint8_t queue_storage_[kQueueLength * sizeof(Tone)]{};
    std::atomic<bool> enabled_{true};
    std::atomic<std::uint32_t> dropped_tones_{0U};
};

static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

} // namespace gamebox::audio
