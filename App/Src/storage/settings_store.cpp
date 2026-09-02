#include "storage/settings_store.hpp"

namespace gamebox::storage {

bool SettingsStore::load(SettingsData& settings) const
{
    const auto magic = static_cast<std::uint16_t>(
        HAL_RTCEx_BKUPRead(&rtc_, RTC_BKP_DR1));
    const auto encoded = static_cast<std::uint16_t>(
        HAL_RTCEx_BKUPRead(&rtc_, RTC_BKP_DR2));
    const auto stored_checkword = static_cast<std::uint16_t>(
        HAL_RTCEx_BKUPRead(&rtc_, RTC_BKP_DR3));
    return magic == SettingsCodec::kMagic &&
           SettingsCodec::decode(encoded, stored_checkword, settings);
}

bool SettingsStore::save(const SettingsData& settings) const
{
    const std::uint16_t encoded = SettingsCodec::encode(settings);
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&rtc_, RTC_BKP_DR2, encoded);
    HAL_RTCEx_BKUPWrite(&rtc_, RTC_BKP_DR3, SettingsCodec::checkword(encoded));
    // Commit marker is intentionally last so a reset during a write is rejected.
    HAL_RTCEx_BKUPWrite(&rtc_, RTC_BKP_DR1, SettingsCodec::kMagic);
    return true;
}

} // namespace gamebox::storage
