#pragma once

#include <cstddef>
#include <cstdint>

#include "etl/string_view.h"

namespace gamebox::display {

enum class PixelOperation : std::uint8_t {
    clear,
    set,
    invert,
};

class Canvas final {
public:
    static constexpr std::int16_t kWidth = 128;
    static constexpr std::int16_t kHeight = 64;
    static constexpr std::uint8_t kPageCount = 8U;
    static constexpr std::size_t kBufferSize = 1024U;

    void clear(bool set = false);
    void pixel(std::int16_t x, std::int16_t y, PixelOperation operation = PixelOperation::set);
    void horizontalLine(std::int16_t x,
                        std::int16_t y,
                        std::int16_t width,
                        PixelOperation operation = PixelOperation::set);
    void verticalLine(std::int16_t x,
                      std::int16_t y,
                      std::int16_t height,
                      PixelOperation operation = PixelOperation::set);
    void line(std::int16_t x0,
              std::int16_t y0,
              std::int16_t x1,
              std::int16_t y1,
              PixelOperation operation = PixelOperation::set);
    void rectangle(std::int16_t x,
                   std::int16_t y,
                   std::int16_t width,
                   std::int16_t height,
                   PixelOperation operation = PixelOperation::set);
    void fillRectangle(std::int16_t x,
                       std::int16_t y,
                       std::int16_t width,
                       std::int16_t height,
                       PixelOperation operation = PixelOperation::set);
    void roundedRectangle(std::int16_t x,
                          std::int16_t y,
                          std::int16_t width,
                          std::int16_t height,
                          std::int16_t radius,
                          PixelOperation operation = PixelOperation::set);
    void fillRoundedRectangle(std::int16_t x,
                              std::int16_t y,
                              std::int16_t width,
                              std::int16_t height,
                              std::int16_t radius,
                              PixelOperation operation = PixelOperation::set);
    void circle(std::int16_t center_x,
                std::int16_t center_y,
                std::int16_t radius,
                PixelOperation operation = PixelOperation::set);
    void drawText(std::int16_t x, std::int16_t y, etl::string_view text, bool inverted = false);
    void drawTextScaled(std::int16_t x,
                        std::int16_t y,
                        etl::string_view text,
                        std::uint8_t scale,
                        bool inverted = false);

    [[nodiscard]] const std::uint8_t* data() const { return pixels_; }
    [[nodiscard]] std::uint8_t dirtyPages() const { return dirty_pages_; }
    void clearDirty() { dirty_pages_ = 0U; }
    void forceDirty() { dirty_pages_ = 0xFFU; }

private:
    void applyPageMask(std::uint8_t page,
                       std::int16_t x_begin,
                       std::int16_t x_end,
                       std::uint8_t mask,
                       PixelOperation operation);
    std::uint8_t pixels_[kBufferSize]{};
    std::uint8_t dirty_pages_{0xFFU};
};

} // namespace gamebox::display
