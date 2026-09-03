#pragma once

#include <atomic>
#include <cstdint>

#include "app/app_module.hpp"
#include "display/canvas.hpp"
#include "i2c.h"

extern "C" {
#include "FreeRTOS.h"
#include "semphr.h"
}

namespace gamebox::display {

class Ssd1306 final : public app::AppModule {
public:
    explicit Ssd1306(I2C_HandleTypeDef& bus);

    [[nodiscard]] etl::string_view name() const override { return "display"; }
    /** Runs all timeout-based HAL setup before any FreeRTOS object exists. */
    [[nodiscard]] bool prepare();
    [[nodiscard]] bool flush(Canvas& canvas);
    [[nodiscard]] bool setContrast(std::uint8_t contrast);
    [[nodiscard]] bool setEnabled(bool enabled);
    [[nodiscard]] std::uint32_t errorCount() const { return error_count_; }
    [[nodiscard]] std::uint32_t dmaTransferCount() const { return dma_transfer_count_; }
    [[nodiscard]] std::uint32_t timeoutCount() const { return timeout_count_; }

    /** HAL callback bridge; only an I2C/DMA ISR calls this function. */
    void completeTransferFromIsr(bool success);

protected:
    [[nodiscard]] bool onInitialize() override;
    [[nodiscard]] bool onDeinitialize() override;

private:
    static constexpr std::uint16_t kAddress = 0x3CU << 1U;
    static constexpr std::uint32_t kTimeoutMs = 12U;

    [[nodiscard]] bool commands(const std::uint8_t* values, std::uint16_t count);
    [[nodiscard]] bool command(std::uint8_t value);
    [[nodiscard]] bool transmitPage(std::uint8_t page, const std::uint8_t* pixels);
    [[nodiscard]] bool transmit(std::uint8_t* bytes, std::uint16_t count);
    [[nodiscard]] bool recoverBus();

    I2C_HandleTypeDef& bus_;
    std::uint8_t shadow_[Canvas::kBufferSize]{};
    std::uint8_t transfer_[Canvas::kWidth + 1U]{};
    StaticSemaphore_t completion_control_block_{};
    SemaphoreHandle_t completion_{nullptr};
    std::atomic<bool> transfer_succeeded_{false};
    std::atomic<bool> dma_transfer_active_{false};
    bool prepared_{false};
    bool shadow_valid_{false};
    std::uint32_t error_count_{0U};
    std::uint32_t dma_transfer_count_{0U};
    std::uint32_t timeout_count_{0U};
};

static_assert(std::atomic<bool>::is_always_lock_free);

} // namespace gamebox::display
