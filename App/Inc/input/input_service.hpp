#pragma once

#include <atomic>
#include <cstdint>

#include "app/app_module.hpp"
#include "app/app_task.hpp"
#include "input/button_engine.hpp"
#include "input/button_event_observer.hpp"

extern "C" {
#include "queue.h"
}

namespace gamebox::input {

class InputService final : public app::AppModule, private app::AppTask {
public:
    InputService();

    [[nodiscard]] etl::string_view name() const override { return "input"; }
    [[nodiscard]] bool receive(ButtonEvent& event, TickType_t timeout = 0U);
    [[nodiscard]] bool subscribe(ButtonEventObserver& observer);
    [[nodiscard]] ButtonMask pressedMask() const
    {
        return stable_mask_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t droppedEventCount() const
    {
        return dropped_events_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t sampleCount() const
    {
        return samples_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t maximumScanGapMs() const
    {
        return maximum_scan_gap_ms_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t stackHeadroomBytes() const
    {
        return app::AppTask::stackHeadroomBytes();
    }

protected:
    [[nodiscard]] bool onInitialize() override;
    [[nodiscard]] bool onDeinitialize() override;

private:
    static constexpr UBaseType_t kQueueLength = 32U;
    static constexpr std::size_t kObserverCapacity = 3U;

    void run() override;
    static void enqueueFromEngine(void* context, const ButtonEvent& event);
    void enqueue(const ButtonEvent& event);
    [[nodiscard]] static ButtonMask readHardware();

    ButtonEngine engine_{};
    QueueHandle_t queue_{nullptr};
    StaticQueue_t queue_control_block_{};
    alignas(ButtonEvent) std::uint8_t queue_storage_[kQueueLength * sizeof(ButtonEvent)]{};
    std::atomic<ButtonMask> stable_mask_{0U};
    std::atomic<std::uint32_t> dropped_events_{0U};
    std::atomic<std::uint32_t> samples_{0U};
    std::atomic<std::uint32_t> maximum_scan_gap_ms_{0U};
    ButtonEventObserver* observers_[kObserverCapacity]{};
};

static_assert(std::atomic<ButtonMask>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

} // namespace gamebox::input
