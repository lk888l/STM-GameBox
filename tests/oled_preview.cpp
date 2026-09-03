#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "display/canvas.hpp"
#include "ui/menu_model.hpp"

namespace {

using gamebox::display::Canvas;
using gamebox::ui::Icon;
using gamebox::ui::View;

std::int16_t textWidth(const char* text)
{
    std::int16_t length = 0;
    while (*text++ != '\0') {
        ++length;
    }
    return static_cast<std::int16_t>(length * 6);
}

void formatPosition(const std::uint8_t index,
                    const std::uint8_t count,
                    char (&output)[6])
{
    const std::uint8_t position = static_cast<std::uint8_t>(index + 1U);
    output[0] = static_cast<char>('0' + position / 10U);
    output[1] = static_cast<char>('0' + position % 10U);
    output[2] = '/';
    output[3] = static_cast<char>('0' + count / 10U);
    output[4] = static_cast<char>('0' + count % 10U);
    output[5] = '\0';
}

void renderIcon(Canvas& canvas,
                const Icon icon,
                const std::int16_t x,
                const std::int16_t y)
{
    switch (icon) {
    case Icon::gamepad:
        canvas.roundedRectangle(x, y, 24, 16, 3);
        canvas.horizontalLine(static_cast<std::int16_t>(x + 4),
                              static_cast<std::int16_t>(y + 8), 7);
        canvas.verticalLine(static_cast<std::int16_t>(x + 7),
                            static_cast<std::int16_t>(y + 5), 7);
        canvas.fillRoundedRectangle(static_cast<std::int16_t>(x + 16),
                                    static_cast<std::int16_t>(y + 5), 3, 3, 1);
        canvas.fillRoundedRectangle(static_cast<std::int16_t>(x + 20),
                                    static_cast<std::int16_t>(y + 9), 3, 3, 1);
        break;
    case Icon::tools:
        canvas.line(static_cast<std::int16_t>(x + 2), static_cast<std::int16_t>(y + 2),
                    static_cast<std::int16_t>(x + 21), static_cast<std::int16_t>(y + 14));
        canvas.line(static_cast<std::int16_t>(x + 21), static_cast<std::int16_t>(y + 2),
                    static_cast<std::int16_t>(x + 2), static_cast<std::int16_t>(y + 14));
        canvas.circle(static_cast<std::int16_t>(x + 3), static_cast<std::int16_t>(y + 2), 2);
        break;
    case Icon::clock:
        canvas.circle(static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 8), 8);
        canvas.line(static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 8),
                    static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 3));
        canvas.line(static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 8),
                    static_cast<std::int16_t>(x + 17), static_cast<std::int16_t>(y + 11));
        break;
    case Icon::settings:
        canvas.circle(static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 8), 7);
        canvas.circle(static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 8), 3);
        canvas.horizontalLine(x, static_cast<std::int16_t>(y + 8), 24);
        canvas.verticalLine(static_cast<std::int16_t>(x + 12), y, 16);
        break;
    default:
        break;
    }
}

void renderHomeCard(Canvas& canvas, const std::uint8_t index, const std::int16_t x_offset)
{
    const auto* const home = gamebox::ui::menuFor(View::home);
    const auto& item = home->entries[index];
    char position[6]{};
    formatPosition(index, home->count, position);

    canvas.roundedRectangle(static_cast<std::int16_t>(x_offset + 2), 1, 124, 62, 3);
    canvas.drawText(static_cast<std::int16_t>(x_offset + 7), 3, "GAMEBOX");
    canvas.fillRoundedRectangle(static_cast<std::int16_t>(x_offset + 92), 2, 32, 9, 2);
    canvas.drawText(static_cast<std::int16_t>(x_offset + 93), 3, position, true);
    canvas.horizontalLine(static_cast<std::int16_t>(x_offset + 4), 12, 120);
    canvas.roundedRectangle(static_cast<std::int16_t>(x_offset + 7), 16, 34, 27, 3);
    renderIcon(canvas, item.icon, static_cast<std::int16_t>(x_offset + 12), 21);
    canvas.fillRoundedRectangle(static_cast<std::int16_t>(x_offset + 45),
                                17,
                                static_cast<std::int16_t>(textWidth(item.label) + 8),
                                11,
                                2);
    canvas.drawText(static_cast<std::int16_t>(x_offset + 49), 19, item.label, true);
    canvas.horizontalLine(static_cast<std::int16_t>(x_offset + 46), 30, 77);
    canvas.drawText(static_cast<std::int16_t>(x_offset + 46), 33, item.subtitle);
    canvas.horizontalLine(static_cast<std::int16_t>(x_offset + 4), 47, 120);
    for (std::uint8_t dot = 0U; dot < home->count; ++dot) {
        const std::int16_t dot_x = static_cast<std::int16_t>(
            x_offset + 8 + static_cast<std::int16_t>(dot) * 7);
        if (dot == index) {
            canvas.fillRoundedRectangle(dot_x, 54, 5, 3, 1);
        } else {
            canvas.pixel(static_cast<std::int16_t>(dot_x + 2), 55);
        }
    }
    canvas.drawText(static_cast<std::int16_t>(x_offset + 42), 52, "<> MOVE");
    canvas.drawText(static_cast<std::int16_t>(x_offset + 92), 52, "ENTER");
}

void renderList(Canvas& canvas, const View view, const std::uint8_t selected)
{
    const auto* const menu = gamebox::ui::menuFor(view);
    constexpr std::uint8_t visible_rows = 3U;
    const std::uint8_t maximum_top = menu->count > visible_rows
                                         ? static_cast<std::uint8_t>(menu->count - visible_rows)
                                         : 0U;
    const std::uint8_t centered_top = selected > 0U
                                          ? static_cast<std::uint8_t>(selected - 1U)
                                          : 0U;
    const std::uint8_t top = centered_top < maximum_top ? centered_top : maximum_top;
    const std::int16_t scroll_y = static_cast<std::int16_t>(
        -static_cast<std::int16_t>(top) * 16);
    char position[6]{};
    formatPosition(selected, menu->count, position);
    for (std::uint8_t item = 0U; item < menu->count; ++item) {
        const std::int16_t y = static_cast<std::int16_t>(
            14 + static_cast<std::int16_t>(item) * 16 + scroll_y);
        if (y <= -8 || y >= 64) {
            continue;
        }
        canvas.drawText(8, y, menu->entries[item].label);
        if (view == View::settings) {
            constexpr const char* values[] = {"ON", "FULL", "MAX", ""};
            const char* const value = values[item];
            std::size_t length = 0U;
            while (value[length] != '\0') { ++length; }
            canvas.drawText(static_cast<std::int16_t>(122 - length * 6U),
                            y,
                            value);
        }
    }
    const std::int16_t highlight_y = static_cast<std::int16_t>(
        14 + static_cast<std::int16_t>(selected - top) * 16);
    const std::int16_t highlight_width = static_cast<std::int16_t>(
        textWidth(menu->entries[selected].label) + 20);
    canvas.fillRoundedRectangle(3,
                                highlight_y,
                                highlight_width,
                                15,
                                2,
                                gamebox::display::PixelOperation::invert);
    canvas.verticalLine(125, 15, 46);
    const std::uint16_t thumb = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(selected) * 42U /
        static_cast<std::uint16_t>(menu->count - 1U));
    canvas.fillRoundedRectangle(123, static_cast<std::int16_t>(15 + thumb), 5, 4, 1);
    canvas.fillRectangle(0, 0, 128, 12, gamebox::display::PixelOperation::clear);
    canvas.drawText(4, 2, menu->title);
    canvas.drawText(96, 2, position);
    canvas.horizontalLine(0, 11, 128);
}

void write16(std::ofstream& output, const std::uint16_t value)
{
    output.put(static_cast<char>(value & 0xFFU));
    output.put(static_cast<char>((value >> 8U) & 0xFFU));
}

void write32(std::ofstream& output, const std::uint32_t value)
{
    write16(output, static_cast<std::uint16_t>(value & 0xFFFFU));
    write16(output, static_cast<std::uint16_t>((value >> 16U) & 0xFFFFU));
}

bool pixelAt(const Canvas& canvas, const int x, const int y)
{
    const std::size_t offset = static_cast<std::size_t>(y / 8) * 128U +
                               static_cast<std::size_t>(x);
    return (canvas.data()[offset] & (1U << (y & 7))) != 0U;
}

bool writePreview(const std::string& path, const std::array<Canvas, 4>& panels)
{
    constexpr int scale = 4;
    constexpr int gap = 4;
    constexpr int panel_width = 128 * scale;
    constexpr int width = panel_width * 4 + gap * 3;
    constexpr int height = 64 * scale;
    constexpr std::uint32_t row_size = static_cast<std::uint32_t>((width * 3 + 3) & ~3);
    constexpr std::uint32_t image_size = row_size * height;
    constexpr std::uint32_t file_size = 54U + image_size;

    std::ofstream output(path, std::ios::binary);
    if (!output) { return false; }
    output.put('B'); output.put('M');
    write32(output, file_size);
    write32(output, 0U);
    write32(output, 54U);
    write32(output, 40U);
    write32(output, static_cast<std::uint32_t>(width));
    write32(output, static_cast<std::uint32_t>(height));
    write16(output, 1U);
    write16(output, 24U);
    write32(output, 0U);
    write32(output, image_size);
    write32(output, 2835U); write32(output, 2835U);
    write32(output, 0U); write32(output, 0U);

    std::vector<char> row(row_size, 0);
    for (int output_y = 0; output_y < height; ++output_y) {
        const int source_y = 63 - output_y / scale;
        for (int x = 0; x < width; ++x) {
            const int panel_index = x / (panel_width + gap);
            const int panel_origin = panel_index * (panel_width + gap);
            const int local_x = x - panel_origin;
            bool on = false;
            if (panel_index >= 0 && panel_index < 4 && local_x >= 0 && local_x < panel_width) {
                on = pixelAt(panels[static_cast<std::size_t>(panel_index)],
                             local_x / scale,
                             source_y);
            }
            const std::size_t byte = static_cast<std::size_t>(x) * 3U;
            row[byte + 0U] = static_cast<char>(on ? 226 : 15);
            row[byte + 1U] = static_cast<char>(on ? 255 : 12);
            row[byte + 2U] = static_cast<char>(on ? 224 : 8);
        }
        output.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(output);
}

} // namespace

int main(int argc, char** argv)
{
    std::array<Canvas, 4> panels{};
    for (Canvas& panel : panels) { panel.clear(); }
    renderHomeCard(panels[0], 0U, 0);
    renderHomeCard(panels[1], 0U, -84);
    renderHomeCard(panels[1], 1U, 44);
    renderList(panels[2], View::games, 2U);
    renderList(panels[3], View::settings, 1U);
    const std::string output = argc > 1 ? argv[1] : "oled_menu_preview.bmp";
    if (!writePreview(output, panels)) {
        std::cerr << "Unable to write " << output << '\n';
        return 1;
    }
    std::cout << output << '\n';
    return 0;
}
