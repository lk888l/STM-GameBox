#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "app/app_module.hpp"
#include "app/app_task.hpp"
#include "input/button_event_observer.hpp"
#include "usart.h"

extern "C" {
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
}

namespace gamebox::diagnostics {

/**
 * Low-priority USART1 telemetry subscriber. Button publication stays
 * synchronous and bounded; formatting and DMA transmission happen here.
 */
class UartDmaService final : public app::AppModule,
                             private app::AppTask,
                             public input::ButtonEventObserver {
public:
    explicit UartDmaService(UART_HandleTypeDef& uart);

    [[nodiscard]] etl::string_view name() const override { return "uart-dma"; }
    void onButtonEvent(const input::ButtonEvent& event) override;
    void completeTransferFromIsr(bool success);
    [[nodiscard]] std::uint32_t droppedEventCount() const
    {
        return dropped_events_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t errorCount() const
    {
        return errors_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t stackHeadroomBytes() const
    {
        return app::AppTask::stackHeadroomBytes();
    }

protected:
    [[nodiscard]] bool onInitialize() override;
    [[nodiscard]] bool onDeinitialize() override;

private:
    static constexpr UBaseType_t kQueueLength = 16U;

    void run() override;
    [[nodiscard]] bool transmit(std::uint8_t* bytes, std::uint16_t count);
    [[nodiscard]] static std::uint16_t formatEvent(const input::ButtonEvent& event,
                                                   char* output,
                                                   std::size_t capacity);

    UART_HandleTypeDef& uart_;
    QueueHandle_t queue_{nullptr};
    StaticQueue_t queue_control_block_{};
    alignas(input::ButtonEvent)
        std::uint8_t queue_storage_[kQueueLength * sizeof(input::ButtonEvent)]{};
    SemaphoreHandle_t completion_{nullptr};
    StaticSemaphore_t completion_control_block_{};
    std::atomic<bool> transfer_succeeded_{false};
    std::atomic<std::uint32_t> dropped_events_{0U};
    std::atomic<std::uint32_t> errors_{0U};
};

static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

} // namespace gamebox::diagnostics
