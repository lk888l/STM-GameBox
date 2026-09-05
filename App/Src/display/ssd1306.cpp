#include "display/ssd1306.hpp"
#include <cstring>

namespace gamebox::display {

bool Ssd1306::commands(const std::uint8_t* values, const std::uint16_t count)
{
    if (transport_.writeCommands(values, count)) { return true; }
    failed();
    return false;
}

bool Ssd1306::configurePanel()
{
    const std::uint8_t init_sequence[] = {
        0xAEU, 0xD5U, GAMEBOX_OLED_SPI ? 0xF0U : 0x80U,
        0xA8U, 0x3FU, 0xD3U, 0x00U, 0x40U,
        0xA1U, 0xC8U, 0xDAU, 0x12U, 0x81U, contrast_, 0xD9U, 0xF1U,
        0xDBU, 0x20U, 0xA4U, 0xA6U, 0x8DU, 0x14U,
        0x20U, GAMEBOX_OLED_SPI ? 0x00U : 0x02U,
        static_cast<std::uint8_t>(enabled_ ? 0xAFU : 0xAEU),
    };
    prepared_ = commands(init_sequence, sizeof(init_sequence));
    shadow_valid_ = false;
    return prepared_;
}

bool Ssd1306::prepare()
{
    if (__get_PRIMASK() != 0U || __get_BASEPRI() != 0U) { return false; }
    prepared_ = false;
    shadow_valid_ = false;
    if (!transport_.prepare() || !configurePanel()) { failed(); }
    // A missing display must not stop input, audio, or subsequent link retries.
    boot_prepared_ = true;
    return true;
}

bool Ssd1306::onInitialize()
{
    return boot_prepared_ && transport_.initialize();
}

bool Ssd1306::onDeinitialize()
{
    transport_.deinitialize();
    boot_prepared_ = false;
    prepared_ = false;
    shadow_valid_ = false;
    return true;
}

void Ssd1306::failed()
{
    prepared_ = false;
    shadow_valid_ = false;
    retry_started_ms_ = HAL_GetTick();
}

bool Ssd1306::ensureOnline()
{
    if (prepared_) { return true; }
    if (HAL_GetTick() - retry_started_ms_ < retry_delay_ms_) { return false; }
    // A recovered link includes the full framebuffer replay. Keep increasing
    // the delay if short init commands work but the subsequent data DMA fails.
    retry_delay_ms_ = retry_delay_ms_ < 1'000U ? retry_delay_ms_ * 2U : 2'000U;
    if (transport_.recoverBus() && transport_.resetPanel() && configurePanel()) {
        return true;
    }
    failed();
    return false;
}

bool Ssd1306::flush(Canvas& canvas)
{
    if (!ensureOnline()) { return false; }
    const std::uint8_t dirty = canvas.dirtyPages();
    if (shadow_valid_ && (dirty == 0U ||
        std::memcmp(canvas.data(), shadow_, sizeof(shadow_)) == 0)) {
        canvas.clearDirty();
        return true;
    }
#if GAMEBOX_OLED_SPI
    // Horizontal addressing streams the full frame in one DMA operation.
    static constexpr std::uint8_t address[] = {0x21U, 0x00U, 0x7FU, 0x22U, 0x00U, 0x07U};
    if (!commands(address, sizeof(address))) { return false; }
    shadow_valid_ = false;
    std::memcpy(shadow_, canvas.data(), sizeof(shadow_));
    if (!transport_.writeData(shadow_, sizeof(shadow_))) { failed(); return false; }
#else
    for (std::uint8_t page = 0U; page < Canvas::kPageCount; ++page) {
        const std::uint8_t page_bit = static_cast<std::uint8_t>(1U << page);
        if (shadow_valid_ && (dirty & page_bit) == 0U) { continue; }
        const std::size_t offset = static_cast<std::size_t>(page) * Canvas::kWidth;
        const std::uint8_t* const source = &canvas.data()[offset];
        if (shadow_valid_ && std::memcmp(source, &shadow_[offset], Canvas::kWidth) == 0) {
            continue;
        }
        const std::uint8_t address[] = {
            static_cast<std::uint8_t>(0xB0U | page), 0x00U, 0x10U,
        };
        if (!commands(address, sizeof(address))) { return false; }
        if (!transport_.writeData(source, Canvas::kWidth)) { failed(); return false; }
        std::memcpy(&shadow_[offset], source, Canvas::kWidth);
    }
#endif
    shadow_valid_ = true;
    retry_delay_ms_ = 50U;
    canvas.clearDirty();
    return true;
}

bool Ssd1306::setContrast(const std::uint8_t contrast)
{
    contrast_ = contrast;
    if (!prepared_) { return false; }
    const std::uint8_t values[] = {0x81U, contrast};
    return commands(values, sizeof(values));
}

bool Ssd1306::setEnabled(const bool enabled)
{
    enabled_ = enabled;
    if (!prepared_) { return false; }
    const std::uint8_t value = enabled ? 0xAFU : 0xAEU;
    return commands(&value, 1U);
}
} // namespace gamebox::display
