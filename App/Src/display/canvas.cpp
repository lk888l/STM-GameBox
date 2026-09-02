#include "display/canvas.hpp"

#include "display/font6x8.hpp"

namespace gamebox::display {

namespace {

constexpr std::int16_t absolute(const std::int16_t value)
{
    return value < 0 ? static_cast<std::int16_t>(-value) : value;
}

constexpr std::int16_t smaller(const std::int16_t left, const std::int16_t right)
{
    return left < right ? left : right;
}

constexpr std::int16_t clampRadius(const std::int16_t radius,
                                   const std::int16_t width,
                                   const std::int16_t height)
{
    const std::int16_t dimension_limit =
        static_cast<std::int16_t>((smaller(width, height) - 1) / 2);
    const std::int16_t limit = smaller(dimension_limit, 3);
    return radius < 0 ? 0 : smaller(radius, limit);
}

} // namespace

void Canvas::clear(const bool set)
{
    const std::uint8_t value = set ? 0xFFU : 0x00U;
    for (std::uint8_t page = 0U; page < kPageCount; ++page) {
        std::uint8_t* const page_begin = &pixels_[static_cast<std::size_t>(page) * kWidth];
        bool changed = false;
        for (std::int16_t x = 0; x < kWidth; ++x) {
            if (page_begin[x] != value) {
                page_begin[x] = value;
                changed = true;
            }
        }
        if (changed) {
            dirty_pages_ |= static_cast<std::uint8_t>(1U << page);
        }
    }
}

void Canvas::pixel(const std::int16_t x,
                   const std::int16_t y,
                   const PixelOperation operation)
{
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
        return;
    }

    const std::uint8_t page = static_cast<std::uint8_t>(y / 8);
    const std::uint8_t bit = static_cast<std::uint8_t>(1U << (y & 0x07));
    std::uint8_t& target = pixels_[static_cast<std::size_t>(page) * kWidth +
                                   static_cast<std::size_t>(x)];
    const std::uint8_t before = target;
    switch (operation) {
    case PixelOperation::clear:
        target &= static_cast<std::uint8_t>(~bit);
        break;
    case PixelOperation::set:
        target |= bit;
        break;
    case PixelOperation::invert:
        target ^= bit;
        break;
    }
    if (target != before) {
        dirty_pages_ |= static_cast<std::uint8_t>(1U << page);
    }
}

void Canvas::horizontalLine(const std::int16_t x,
                            const std::int16_t y,
                            const std::int16_t width,
                            const PixelOperation operation)
{
    for (std::int16_t offset = 0; offset < width; ++offset) {
        pixel(static_cast<std::int16_t>(x + offset), y, operation);
    }
}

void Canvas::verticalLine(const std::int16_t x,
                          const std::int16_t y,
                          const std::int16_t height,
                          const PixelOperation operation)
{
    for (std::int16_t offset = 0; offset < height; ++offset) {
        pixel(x, static_cast<std::int16_t>(y + offset), operation);
    }
}

void Canvas::line(std::int16_t x0,
                  std::int16_t y0,
                  const std::int16_t x1,
                  const std::int16_t y1,
                  const PixelOperation operation)
{
    const std::int16_t dx = absolute(static_cast<std::int16_t>(x1 - x0));
    const std::int16_t sx = x0 < x1 ? 1 : -1;
    const std::int16_t dy = static_cast<std::int16_t>(-absolute(static_cast<std::int16_t>(y1 - y0)));
    const std::int16_t sy = y0 < y1 ? 1 : -1;
    std::int16_t error = static_cast<std::int16_t>(dx + dy);
    for (;;) {
        pixel(x0, y0, operation);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const std::int16_t twice_error = static_cast<std::int16_t>(2 * error);
        if (twice_error >= dy) {
            error = static_cast<std::int16_t>(error + dy);
            x0 = static_cast<std::int16_t>(x0 + sx);
        }
        if (twice_error <= dx) {
            error = static_cast<std::int16_t>(error + dx);
            y0 = static_cast<std::int16_t>(y0 + sy);
        }
    }
}

void Canvas::rectangle(const std::int16_t x,
                       const std::int16_t y,
                       const std::int16_t width,
                       const std::int16_t height,
                       const PixelOperation operation)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    horizontalLine(x, y, width, operation);
    horizontalLine(x, static_cast<std::int16_t>(y + height - 1), width, operation);
    verticalLine(x, y, height, operation);
    verticalLine(static_cast<std::int16_t>(x + width - 1), y, height, operation);
}

void Canvas::fillRectangle(const std::int16_t x,
                           const std::int16_t y,
                           const std::int16_t width,
                           const std::int16_t height,
                           const PixelOperation operation)
{
    for (std::int16_t row = 0; row < height; ++row) {
        horizontalLine(x, static_cast<std::int16_t>(y + row), width, operation);
    }
}

void Canvas::roundedRectangle(const std::int16_t x,
                              const std::int16_t y,
                              const std::int16_t width,
                              const std::int16_t height,
                              const std::int16_t radius,
                              const PixelOperation operation)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    const std::int16_t r = clampRadius(radius, width, height);
    horizontalLine(static_cast<std::int16_t>(x + r), y,
                   static_cast<std::int16_t>(width - 2 * r), operation);
    horizontalLine(static_cast<std::int16_t>(x + r),
                   static_cast<std::int16_t>(y + height - 1),
                   static_cast<std::int16_t>(width - 2 * r), operation);
    verticalLine(x, static_cast<std::int16_t>(y + r),
                 static_cast<std::int16_t>(height - 2 * r), operation);
    verticalLine(static_cast<std::int16_t>(x + width - 1),
                 static_cast<std::int16_t>(y + r),
                 static_cast<std::int16_t>(height - 2 * r), operation);
    for (std::int16_t offset = 0; offset < r; ++offset) {
        const std::int16_t inset = static_cast<std::int16_t>(r - offset);
        pixel(static_cast<std::int16_t>(x + inset), static_cast<std::int16_t>(y + offset), operation);
        pixel(static_cast<std::int16_t>(x + width - 1 - inset),
              static_cast<std::int16_t>(y + offset), operation);
        pixel(static_cast<std::int16_t>(x + inset),
              static_cast<std::int16_t>(y + height - 1 - offset), operation);
        pixel(static_cast<std::int16_t>(x + width - 1 - inset),
              static_cast<std::int16_t>(y + height - 1 - offset), operation);
    }
}

void Canvas::fillRoundedRectangle(const std::int16_t x,
                                  const std::int16_t y,
                                  const std::int16_t width,
                                  const std::int16_t height,
                                  const std::int16_t radius,
                                  const PixelOperation operation)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    const std::int16_t r = clampRadius(radius, width, height);
    for (std::int16_t row = 0; row < height; ++row) {
        const std::int16_t edge = smaller(row, static_cast<std::int16_t>(height - 1 - row));
        const std::int16_t inset = edge < r ? static_cast<std::int16_t>(r - edge) : 0;
        horizontalLine(static_cast<std::int16_t>(x + inset),
                       static_cast<std::int16_t>(y + row),
                       static_cast<std::int16_t>(width - 2 * inset),
                       operation);
    }
}

void Canvas::circle(const std::int16_t center_x,
                    const std::int16_t center_y,
                    const std::int16_t radius,
                    const PixelOperation operation)
{
    std::int16_t x = radius;
    std::int16_t y = 0;
    std::int16_t error = 1 - radius;
    while (x >= y) {
        pixel(static_cast<std::int16_t>(center_x + x), static_cast<std::int16_t>(center_y + y), operation);
        pixel(static_cast<std::int16_t>(center_x + y), static_cast<std::int16_t>(center_y + x), operation);
        pixel(static_cast<std::int16_t>(center_x - y), static_cast<std::int16_t>(center_y + x), operation);
        pixel(static_cast<std::int16_t>(center_x - x), static_cast<std::int16_t>(center_y + y), operation);
        pixel(static_cast<std::int16_t>(center_x - x), static_cast<std::int16_t>(center_y - y), operation);
        pixel(static_cast<std::int16_t>(center_x - y), static_cast<std::int16_t>(center_y - x), operation);
        pixel(static_cast<std::int16_t>(center_x + y), static_cast<std::int16_t>(center_y - x), operation);
        pixel(static_cast<std::int16_t>(center_x + x), static_cast<std::int16_t>(center_y - y), operation);
        ++y;
        if (error < 0) {
            error = static_cast<std::int16_t>(error + 2 * y + 1);
        } else {
            --x;
            error = static_cast<std::int16_t>(error + 2 * (y - x) + 1);
        }
    }
}

void Canvas::drawText(std::int16_t x,
                      const std::int16_t y,
                      const etl::string_view text,
                      const bool inverted)
{
    for (const char character : text) {
        const std::uint8_t* const glyph = glyph6x8(character);
        for (std::uint8_t column = 0U; column < kFontGlyphWidth; ++column) {
            for (std::uint8_t row = 0U; row < kFontGlyphHeight; ++row) {
                const bool foreground = (glyph[column] & (1U << row)) != 0U;
                pixel(static_cast<std::int16_t>(x + column),
                      static_cast<std::int16_t>(y + row),
                      foreground != inverted ? PixelOperation::set : PixelOperation::clear);
            }
        }
        x = static_cast<std::int16_t>(x + kFontGlyphWidth);
    }
}

void Canvas::drawTextScaled(std::int16_t x,
                            const std::int16_t y,
                            const etl::string_view text,
                            const std::uint8_t scale,
                            const bool inverted)
{
    if (scale == 0U) {
        return;
    }
    for (const char character : text) {
        const std::uint8_t* const glyph = glyph6x8(character);
        for (std::uint8_t column = 0U; column < kFontGlyphWidth; ++column) {
            for (std::uint8_t row = 0U; row < kFontGlyphHeight; ++row) {
                const bool foreground = (glyph[column] & (1U << row)) != 0U;
                fillRectangle(
                    static_cast<std::int16_t>(x + static_cast<std::int16_t>(column * scale)),
                    static_cast<std::int16_t>(y + static_cast<std::int16_t>(row * scale)),
                    scale,
                    scale,
                    foreground != inverted ? PixelOperation::set : PixelOperation::clear);
            }
        }
        x = static_cast<std::int16_t>(x + static_cast<std::int16_t>(kFontGlyphWidth * scale));
    }
}

} // namespace gamebox::display
