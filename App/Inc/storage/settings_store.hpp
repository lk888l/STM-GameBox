#pragma once

#include "rtc.h"
#include "storage/settings_codec.hpp"

namespace gamebox::storage {

class SettingsStore final {
public:
    explicit SettingsStore(RTC_HandleTypeDef& rtc) : rtc_(rtc) {}

    [[nodiscard]] bool load(SettingsData& settings) const;
    [[nodiscard]] bool save(const SettingsData& settings) const;

private:
    RTC_HandleTypeDef& rtc_;
};

} // namespace gamebox::storage
