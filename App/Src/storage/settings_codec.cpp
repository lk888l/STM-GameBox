#include "storage/settings_codec.hpp"

namespace gamebox::storage {

namespace {

constexpr std::uint16_t kSchemaTag = 0x0200U;
constexpr std::uint16_t kSchemaMask = 0xFF00U;
constexpr std::uint16_t kCheckXor = 0xA55AU;

} // namespace

std::uint16_t SettingsCodec::encode(const SettingsData& settings)
{
    const std::uint16_t sound = settings.sound_enabled ? 1U : 0U;
    const std::uint16_t motion = static_cast<std::uint16_t>(settings.motion_level & 0x03U);
    const std::uint16_t brightness =
        static_cast<std::uint16_t>(settings.brightness_level & 0x03U);
    const std::uint16_t home_header =
        static_cast<std::uint16_t>(settings.home_header_mode) & 0x03U;
    return static_cast<std::uint16_t>(kSchemaTag | sound | (motion << 1U) |
                                      (brightness << 3U) | (home_header << 5U));
}

std::uint16_t SettingsCodec::checkword(const std::uint16_t encoded)
{
    return static_cast<std::uint16_t>(encoded ^ kCheckXor);
}

bool SettingsCodec::decode(const std::uint16_t encoded,
                           const std::uint16_t stored_checkword,
                           SettingsData& settings)
{
    constexpr std::uint16_t reserved_mask = 0x0080U;
    if ((encoded & kSchemaMask) != kSchemaTag || (encoded & reserved_mask) != 0U ||
        checkword(encoded) != stored_checkword) {
        return false;
    }
    const std::uint8_t motion = static_cast<std::uint8_t>((encoded >> 1U) & 0x03U);
    const std::uint8_t brightness = static_cast<std::uint8_t>((encoded >> 3U) & 0x03U);
    if (motion > 2U || brightness > 3U) {
        return false;
    }
    settings.sound_enabled = (encoded & 0x01U) != 0U;
    settings.motion_level = motion;
    settings.brightness_level = brightness;
    settings.home_header_mode =
        static_cast<HomeHeaderMode>((encoded >> 5U) & 0x03U);
    return true;
}

} // namespace gamebox::storage
