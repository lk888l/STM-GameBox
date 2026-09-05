#include "display/canvas.hpp"

#include "display/font6x8.hpp"

namespace gamebox::display {

namespace {

struct Fraction {
    std::int32_t numerator;
    std::int32_t denominator;
};

constexpr int compare(const Fraction left, const Fraction right)
{
    const std::int64_t left_scaled =
        static_cast<std::int64_t>(left.numerator) * right.denominator;
    const std::int64_t right_scaled =
        static_cast<std::int64_t>(right.numerator) * left.denominator;
    if (left_scaled < right_scaled) {
        return -1;
    }
    if (left_scaled > right_scaled) {
        return 1;
    }
    return 0;
}

bool narrowInterval(const std::int32_t direction,
                    const std::int32_t distance,
                    Fraction& entering,
                    Fraction& leaving)
{
    if (direction == 0) {
        return distance >= 0;
    }

    const bool entering_boundary = direction < 0;
    Fraction candidate{distance, direction};
    if (candidate.denominator < 0) {
        candidate.numerator = -candidate.numerator;
        candidate.denominator = -candidate.denominator;
    }

    if (entering_boundary) {
        if (compare(candidate, leaving) > 0) {
            return false;
        }
        if (compare(candidate, entering) > 0) {
            entering = candidate;
        }
    } else {
        if (compare(candidate, entering) < 0) {
            return false;
        }
        if (compare(candidate, leaving) < 0) {
            leaving = candidate;
        }
    }
    return true;
}

std::int32_t interpolate(const std::int32_t start,
                         const std::int32_t delta,
                         const Fraction fraction)
{
    const std::int64_t scaled =
        static_cast<std::int64_t>(delta) * fraction.numerator;
    const std::int64_t half = fraction.denominator / 2;
    const std::int64_t rounded = scaled >= 0 ? scaled + half : scaled - half;
    return start + static_cast<std::int32_t>(rounded / fraction.denominator);
}

constexpr std::int32_t clamp(const std::int32_t value,
                             const std::int32_t minimum,
                             const std::int32_t maximum)
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

bool clipLine(std::int32_t& x0,
              std::int32_t& y0,
              std::int32_t& x1,
              std::int32_t& y1)
{
    constexpr std::int32_t maximum_x = Canvas::kWidth - 1;
    constexpr std::int32_t maximum_y = Canvas::kHeight - 1;
    const std::int32_t delta_x = x1 - x0;
    const std::int32_t delta_y = y1 - y0;
    Fraction entering{0, 1};
    Fraction leaving{1, 1};

    // Liang-Barsky clipping keeps work bounded to four edge tests. Fractions
    // are compared exactly, avoiding floating point and direction-dependent
    // false rejection at a viewport corner.
    if (!narrowInterval(-delta_x, x0, entering, leaving) ||
        !narrowInterval(delta_x, maximum_x - x0, entering, leaving) ||
        !narrowInterval(-delta_y, y0, entering, leaving) ||
        !narrowInterval(delta_y, maximum_y - y0, entering, leaving)) {
        return false;
    }

    const std::int32_t clipped_x0 = interpolate(x0, delta_x, entering);
    const std::int32_t clipped_y0 = interpolate(y0, delta_y, entering);
    const std::int32_t clipped_x1 = interpolate(x0, delta_x, leaving);
    const std::int32_t clipped_y1 = interpolate(y0, delta_y, leaving);
    x0 = clamp(clipped_x0, 0, maximum_x);
    y0 = clamp(clipped_y0, 0, maximum_y);
    x1 = clamp(clipped_x1, 0, maximum_x);
    y1 = clamp(clipped_y1, 0, maximum_y);
    return true;
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
    fillRectangle(x, y, width, 1, operation);
}

void Canvas::verticalLine(const std::int16_t x,
                          const std::int16_t y,
                          const std::int16_t height,
                          const PixelOperation operation)
{
    fillRectangle(x, y, 1, height, operation);
}

void Canvas::line(std::int16_t x0,
                  std::int16_t y0,
                  const std::int16_t x1,
                  const std::int16_t y1,
                  const PixelOperation operation)
{
    std::int32_t start_x = x0;
    std::int32_t start_y = y0;
    std::int32_t end_x = x1;
    std::int32_t end_y = y1;
    if (!clipLine(start_x, start_y, end_x, end_y)) {
        return;
    }

    const std::int32_t dx = start_x < end_x ? end_x - start_x : start_x - end_x;
    const std::int32_t step_x = start_x < end_x ? 1 : -1;
    const std::int32_t dy = -(start_y < end_y ? end_y - start_y : start_y - end_y);
    const std::int32_t step_y = start_y < end_y ? 1 : -1;
    std::int32_t error = dx + dy;
    for (;;) {
        pixel(static_cast<std::int16_t>(start_x),
              static_cast<std::int16_t>(start_y),
              operation);
        if (start_x == end_x && start_y == end_y) {
            break;
        }
        const std::int32_t twice_error = 2 * error;
        if (twice_error >= dy) {
            error += dy;
            start_x += step_x;
        }
        if (twice_error <= dx) {
            error += dx;
            start_y += step_y;
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
    if (width <= 0 || height <= 0) {
        return;
    }
    const auto x_begin = static_cast<std::int16_t>(clamp(x, 0, kWidth));
    const auto x_end = static_cast<std::int16_t>(
        clamp(static_cast<std::int32_t>(x) + width, 0, kWidth));
    const auto y_begin = static_cast<std::int16_t>(clamp(y, 0, kHeight));
    const auto y_end = static_cast<std::int16_t>(
        clamp(static_cast<std::int32_t>(y) + height, 0, kHeight));
    if (x_begin >= x_end || y_begin >= y_end) {
        return;
    }

    // SSD1306 stores eight vertical pixels in each byte. Fill a clipped span
    // once per page instead of revisiting each byte for all eight pixel rows.
    const auto first_page = static_cast<std::uint8_t>(y_begin / 8);
    const auto last_page = static_cast<std::uint8_t>((y_end - 1) / 8);
    for (std::uint8_t page = first_page; page <= last_page; ++page) {
        const auto first_bit = static_cast<std::uint8_t>(page == first_page ? y_begin % 8 : 0);
        const auto end_bit = static_cast<std::uint8_t>(page == last_page ? (y_end - 1) % 8 + 1 : 8);
        const auto mask = static_cast<std::uint8_t>(
            (0xFFU << first_bit) & (0xFFU >> (8U - end_bit)));
        applyPageMask(page, x_begin, x_end, mask, operation);
    }
}

void Canvas::applyPageMask(const std::uint8_t page,
                           const std::int16_t x_begin,
                           const std::int16_t x_end,
                           const std::uint8_t mask,
                           const PixelOperation operation)
{
    std::uint8_t* const row = &pixels_[static_cast<std::size_t>(page) * kWidth];
    bool changed = false;
    for (std::int16_t x = x_begin; x < x_end; ++x) {
        const std::uint8_t before = row[x];
        switch (operation) {
        case PixelOperation::clear: row[x] &= static_cast<std::uint8_t>(~mask); break;
        case PixelOperation::set: row[x] |= mask; break;
        case PixelOperation::invert: row[x] ^= mask; break;
        }
        changed = changed || row[x] != before;
    }
    if (changed) {
        dirty_pages_ |= static_cast<std::uint8_t>(1U << page);
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
    if (y <= -static_cast<std::int16_t>(kFontGlyphHeight) || y >= kHeight) {
        return;
    }
    // Floor division keeps the page/shift valid when a glyph enters from above.
    const std::int16_t first_page = y < 0 ? -1 : static_cast<std::int16_t>(y / 8);
    const auto shift = static_cast<std::uint8_t>(y - first_page * 8);
    const auto glyph_mask = static_cast<std::uint16_t>(0xFFU << shift);
    std::int32_t cursor = x;
    for (const char character : text) {
        if (cursor >= kWidth) {
            break;
        }
        const std::uint8_t* const glyph = glyph6x8(character);
        for (std::uint8_t column = 0U; column < kFontGlyphWidth; ++column) {
            const std::int32_t screen_x = cursor + column;
            if (screen_x < 0 || screen_x >= kWidth) {
                continue;
            }
            const auto column_bits = inverted ? static_cast<std::uint8_t>(~glyph[column]) : glyph[column];
            const auto glyph_bits = static_cast<std::uint16_t>(static_cast<std::uint32_t>(column_bits) << shift);
            for (std::uint8_t offset = 0U; offset < 2U; ++offset) {
                const auto page = static_cast<std::int16_t>(first_page + offset);
                const auto page_mask = static_cast<std::uint8_t>(glyph_mask >> (offset * 8U));
                if (page < 0 || page >= kPageCount || page_mask == 0U) {
                    continue;
                }
                const auto bits = static_cast<std::uint8_t>(glyph_bits >> (offset * 8U));
                std::uint8_t& target = pixels_[static_cast<std::size_t>(page) * kWidth +
                                               static_cast<std::size_t>(screen_x)];
                const auto after = static_cast<std::uint8_t>((target & ~page_mask) | bits);
                if (target != after) {
                    target = after;
                    dirty_pages_ |= static_cast<std::uint8_t>(1U << page);
                }
            }
        }
        cursor += kFontGlyphWidth;
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
