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
using gamebox::ui::View;

void header(Canvas& canvas, const char* const title)
{
    canvas.fillRectangle(0, 0, 128, 11);
    canvas.drawText(4, 2, title, true);
    canvas.pixel(119, 3, gamebox::display::PixelOperation::clear);
    canvas.pixel(122, 3, gamebox::display::PixelOperation::clear);
    canvas.pixel(125, 3, gamebox::display::PixelOperation::clear);
}

void footer(Canvas& canvas, const char* const hint)
{
    canvas.horizontalLine(0, 54, 128);
    canvas.drawText(4, 56, hint);
}

void renderHome(Canvas& canvas)
{
    const auto& item = gamebox::ui::entryAt(View::home, 0U);
    header(canvas, "GAMEBOX");
    canvas.roundedRectangle(10, 14, 108, 34, 3);
    canvas.roundedRectangle(18, 22, 24, 16, 3);
    canvas.horizontalLine(22, 30, 7);
    canvas.verticalLine(25, 27, 7);
    canvas.fillRoundedRectangle(34, 27, 3, 3, 1);
    canvas.fillRoundedRectangle(38, 31, 3, 3, 1);
    canvas.drawText(49, 20, item.label);
    canvas.horizontalLine(49, 30, 59);
    canvas.drawText(49, 35, item.subtitle);
    canvas.fillRoundedRectangle(48, 50, 5, 3, 1);
    canvas.pixel(58, 51);
    canvas.pixel(66, 51);
    canvas.pixel(74, 51);
    footer(canvas, "<> SELECT  ENTER");
}

void renderList(Canvas& canvas, const View view, const std::uint8_t selected)
{
    const auto* const menu = gamebox::ui::menuFor(view);
    header(canvas, menu->title);
    canvas.fillRoundedRectangle(2, static_cast<std::int16_t>(13 + selected * 10), 124, 9, 2);
    for (std::uint8_t row = 0U; row < 4U && row < menu->count; ++row) {
        canvas.drawText(7,
                        static_cast<std::int16_t>(13 + row * 10),
                        menu->entries[row].label,
                        row == selected);
        if (view == View::settings) {
            constexpr const char* values[] = {"ON", "FULL", "MAX", ""};
            const char* const value = values[row];
            std::size_t length = 0U;
            while (value[length] != '\0') { ++length; }
            canvas.drawText(static_cast<std::int16_t>(122 - length * 6U),
                            static_cast<std::int16_t>(13 + row * 10),
                            value,
                            row == selected);
        }
    }
    footer(canvas, "UP/DN  ENTER  BACK");
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

bool writePreview(const std::string& path, const std::array<Canvas, 3>& panels)
{
    constexpr int scale = 4;
    constexpr int gap = 4;
    constexpr int panel_width = 128 * scale;
    constexpr int width = panel_width * 3 + gap * 2;
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
            if (panel_index >= 0 && panel_index < 3 && local_x >= 0 && local_x < panel_width) {
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
    std::array<Canvas, 3> panels{};
    for (Canvas& panel : panels) { panel.clear(); }
    renderHome(panels[0]);
    renderList(panels[1], View::games, 2U);
    renderList(panels[2], View::settings, 1U);
    const std::string output = argc > 1 ? argv[1] : "oled_menu_preview.bmp";
    if (!writePreview(output, panels)) {
        std::cerr << "Unable to write " << output << '\n';
        return 1;
    }
    std::cout << output << '\n';
    return 0;
}
