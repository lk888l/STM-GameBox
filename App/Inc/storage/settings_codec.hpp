#pragma once

#include <cstdint>

namespace gamebox::storage {

struct SettingsData {
    bool sound_enabled{true};
    std::uint8_t motion_level{0U};
    std::uint8_t brightness_level{3U};
};

class SettingsCodec final {
public:
    static constexpr std::uint16_t kMagic = 0x4742U;

    [[nodiscard]] static std::uint16_t encode(const SettingsData& settings);
    [[nodiscard]] static std::uint16_t checkword(std::uint16_t encoded);
    [[nodiscard]] static bool decode(std::uint16_t encoded,
                                     std::uint16_t stored_checkword,
                                     SettingsData& settings);
};

} // namespace gamebox::storage
