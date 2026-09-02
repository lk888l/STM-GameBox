#pragma once

#include <cstdint>

namespace gamebox::display {

inline constexpr std::uint8_t kFontFirstCharacter = 32U;
inline constexpr std::uint8_t kFontLastCharacter = 126U;
inline constexpr std::uint8_t kFontGlyphWidth = 6U;
inline constexpr std::uint8_t kFontGlyphHeight = 8U;

[[nodiscard]] const std::uint8_t* glyph6x8(char character);

} // namespace gamebox::display
