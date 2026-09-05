#pragma once

#include <atomic>
#include <cstdint>
#if GAMEBOX_OLED_SPI
#include "spi.h"
#else
#include "i2c.h"
#endif
extern "C" {
#include "FreeRTOS.h"
#include "semphr.h"
}

namespace gamebox::platform {
#if GAMEBOX_OLED_SPI
using OledBus = SPI_HandleTypeDef;
#else
using OledBus = I2C_HandleTypeDef;
#endif

/** Compile-time adapter owned by the UI task. Writes sleep on a static DMA
 * semaphore. Every return, including errors, leaves no DMA reading the data. */
class OledTransport final {
public:
    explicit OledTransport(OledBus& bus) : bus_(bus) {}
    [[nodiscard]] bool prepare();
    [[nodiscard]] bool initialize();
    void deinitialize();
    [[nodiscard]] bool resetPanel();
    [[nodiscard]] bool recoverBus();
    [[nodiscard]] bool writeCommands(const std::uint8_t* values, std::uint16_t count);
    [[nodiscard]] bool writeData(const std::uint8_t* values, std::uint16_t count);
    [[nodiscard]] std::uint32_t errorCount() const { return error_count_; }
    [[nodiscard]] std::uint32_t dmaTransferCount() const { return dma_transfer_count_; }
    [[nodiscard]] std::uint32_t timeoutCount() const { return timeout_count_; }
    [[nodiscard]] bool owns(const OledBus* bus) const { return bus == &bus_; }
    [[nodiscard]] bool owns(const DMA_HandleTypeDef* dma) const { return dma == bus_.hdmatx; }
    void completeTransfer(bool success);

private:
    static constexpr std::uint16_t kAddress = 0x3CU << 1U;
    static constexpr std::uint32_t kTimeoutMs = GAMEBOX_OLED_SPI ? 20U : 80U;
    [[nodiscard]] bool write(bool data, const std::uint8_t* values, std::uint16_t count);
    [[nodiscard]] bool transmit(const std::uint8_t* values, std::uint16_t count);
    void stopBus();
    OledBus& bus_;
#if !GAMEBOX_OLED_SPI
    std::uint8_t transfer_[129]{};
#endif
    StaticSemaphore_t completion_control_block_{};
    SemaphoreHandle_t completion_{nullptr};
    std::atomic<bool> transfer_succeeded_{false};
    std::atomic<bool> dma_transfer_active_{false};
    std::uint32_t error_count_{0U};
    std::uint32_t dma_transfer_count_{0U};
    std::uint32_t timeout_count_{0U};
};
static_assert(std::atomic<bool>::is_always_lock_free);
} // namespace gamebox::platform
