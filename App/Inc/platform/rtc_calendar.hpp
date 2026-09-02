#pragma once

#include <cstdint>

#include "stm32f1xx_hal.h"
#include "storage/calendar_codec.hpp"

namespace gamebox::platform {

struct DateTime {
    std::uint8_t hours{0U};
    std::uint8_t minutes{0U};
    std::uint8_t seconds{0U};
    storage::CalendarDate date{};
};

/**
 * Owns the STM32F1 HAL software-date workaround.
 *
 * F1 stores only seconds in hardware; HAL keeps the date in SRAM. This adapter
 * anchors the date in backup registers and merges any whole days accumulated
 * in RTC_CNT before allowing HAL to normalize the counter.
 */
class RtcCalendar final {
public:
    explicit RtcCalendar(RTC_HandleTypeDef& rtc) : rtc_(rtc) {}

    [[nodiscard]] bool restore();
    [[nodiscard]] bool read(DateTime& date_time);
    [[nodiscard]] bool set(const DateTime& date_time);

private:
    [[nodiscard]] std::uint32_t readRawCounter() const;
    [[nodiscard]] bool loadDate(storage::CalendarDate& date) const;
    void persistDate(const storage::CalendarDate& date);

    RTC_HandleTypeDef& rtc_;
    std::uint16_t last_persisted_{0U};
    bool restored_{false};
};

} // namespace gamebox::platform
