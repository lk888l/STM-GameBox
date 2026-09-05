#pragma once

#include <cstdint>
#include "app/app_module.hpp"
#include "display/canvas.hpp"
#include "platform/oled_transport.hpp"

namespace gamebox::display {

/** Controller and frame cache; bus ownership and DMA live in OledTransport. */
class Ssd1306 final : public app::AppModule {
public:
    explicit Ssd1306(platform::OledBus& bus) : transport_(bus) {}
    [[nodiscard]] etl::string_view name() const override { return "display"; }
    /** Complete all timeout-based setup before FreeRTOS object creation. */
    [[nodiscard]] bool prepare();
    [[nodiscard]] bool flush(Canvas& canvas);
    [[nodiscard]] bool setContrast(std::uint8_t contrast);
    [[nodiscard]] bool setEnabled(bool enabled);
    [[nodiscard]] std::uint32_t errorCount() const { return transport_.errorCount(); }
    [[nodiscard]] std::uint32_t dmaTransferCount() const { return transport_.dmaTransferCount(); }
    [[nodiscard]] std::uint32_t timeoutCount() const { return transport_.timeoutCount(); }
protected:
    [[nodiscard]] bool onInitialize() override;
    [[nodiscard]] bool onDeinitialize() override;
private:
    [[nodiscard]] bool configurePanel();
    [[nodiscard]] bool commands(const std::uint8_t* values, std::uint16_t count);
    [[nodiscard]] bool ensureOnline();
    void failed();
    platform::OledTransport transport_;
    // Also the SPI DMA source; any failed transfer invalidates the whole cache.
    std::uint8_t shadow_[Canvas::kBufferSize]{};
    bool boot_prepared_{false};
    bool prepared_{false};
    bool shadow_valid_{false};
    bool enabled_{true};
    std::uint8_t contrast_{0xCFU};
    std::uint32_t retry_started_ms_{0U};
    std::uint32_t retry_delay_ms_{50U};
};
} // namespace gamebox::display
