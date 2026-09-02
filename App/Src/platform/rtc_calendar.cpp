#include "platform/rtc_calendar.hpp"

#include "stm32f1xx_hal_rtc_ex.h"

namespace gamebox::platform {

namespace {

constexpr std::uint32_t kSecondsPerDay = 24U * 60U * 60U;
constexpr storage::CalendarDate kDefaultDate{26U, 1U, 1U};

[[nodiscard]] bool sameDate(const RTC_DateTypeDef& lhs, const RTC_DateTypeDef& rhs)
{
    return lhs.Year == rhs.Year && lhs.Month == rhs.Month && lhs.Date == rhs.Date;
}

} // namespace

std::uint32_t RtcCalendar::readRawCounter() const
{
    const std::uint16_t high_first = static_cast<std::uint16_t>(
        READ_REG(rtc_.Instance->CNTH) & RTC_CNTH_RTC_CNT);
    std::uint16_t low = static_cast<std::uint16_t>(
        READ_REG(rtc_.Instance->CNTL) & RTC_CNTL_RTC_CNT);
    const std::uint16_t high_second = static_cast<std::uint16_t>(
        READ_REG(rtc_.Instance->CNTH) & RTC_CNTH_RTC_CNT);
    if (high_first != high_second) {
        low = static_cast<std::uint16_t>(READ_REG(rtc_.Instance->CNTL) & RTC_CNTL_RTC_CNT);
    }
    return (static_cast<std::uint32_t>(high_second) << 16U) | low;
}

bool RtcCalendar::loadDate(storage::CalendarDate& date) const
{
    const auto magic = static_cast<std::uint16_t>(
        HAL_RTCEx_BKUPRead(&rtc_, RTC_BKP_DR6));
    if (magic != storage::CalendarCodec::kMagic) {
        return false;
    }
    const auto encoded = static_cast<std::uint16_t>(
        HAL_RTCEx_BKUPRead(&rtc_, RTC_BKP_DR4));
    const auto checkword = static_cast<std::uint16_t>(
        HAL_RTCEx_BKUPRead(&rtc_, RTC_BKP_DR5));
    return storage::CalendarCodec::decode(encoded, checkword, date);
}

void RtcCalendar::persistDate(const storage::CalendarDate& date)
{
    const std::uint16_t encoded = storage::CalendarCodec::encode(date);
    if (restored_ && encoded == last_persisted_) {
        return;
    }
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&rtc_, RTC_BKP_DR4, encoded);
    HAL_RTCEx_BKUPWrite(&rtc_, RTC_BKP_DR5, storage::CalendarCodec::checkword(encoded));
    // Commit marker is intentionally written last so torn updates fail validation.
    HAL_RTCEx_BKUPWrite(&rtc_, RTC_BKP_DR6, storage::CalendarCodec::kMagic);
    last_persisted_ = encoded;
}

bool RtcCalendar::restore()
{
    storage::CalendarDate date{};
    if (!loadDate(date)) {
        date = kDefaultDate;
    }

    // Read before HAL_RTC_SetDate: that HAL call discards whole days from RTC_CNT
    // without applying them to the software date on STM32F1.
    const std::uint32_t raw_counter = readRawCounter();
    (void)storage::CalendarCodec::advance(date, raw_counter / kSecondsPerDay);

    RTC_DateTypeDef hal_date{};
    hal_date.Year = date.year;
    hal_date.Month = date.month;
    hal_date.Date = date.day;
    RTC_TimeTypeDef hal_time{};
    const std::uint32_t seconds_today = raw_counter % kSecondsPerDay;
    hal_time.Hours = static_cast<std::uint8_t>(seconds_today / 3600U);
    hal_time.Minutes = static_cast<std::uint8_t>((seconds_today / 60U) % 60U);
    hal_time.Seconds = static_cast<std::uint8_t>(seconds_today % 60U);

    if (HAL_RTC_SetDate(&rtc_, &hal_date, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_SetTime(&rtc_, &hal_time, RTC_FORMAT_BIN) != HAL_OK) {
        return false;
    }
    restored_ = true;
    persistDate(date);
    return true;
}

bool RtcCalendar::read(DateTime& date_time)
{
    if (!restored_) {
        return false;
    }

    RTC_DateTypeDef date_before{};
    RTC_DateTypeDef date_after{};
    RTC_TimeTypeDef time{};
    bool stable = false;
    for (std::uint8_t attempt = 0U; attempt < 2U; ++attempt) {
        if (HAL_RTC_GetDate(&rtc_, &date_before, RTC_FORMAT_BIN) != HAL_OK ||
            HAL_RTC_GetTime(&rtc_, &time, RTC_FORMAT_BIN) != HAL_OK ||
            HAL_RTC_GetDate(&rtc_, &date_after, RTC_FORMAT_BIN) != HAL_OK) {
            return false;
        }
        if (sameDate(date_before, date_after)) {
            stable = true;
            break;
        }
    }
    if (!stable) {
        return false;
    }

    const storage::CalendarDate date{date_after.Year, date_after.Month, date_after.Date};
    if (!storage::CalendarCodec::isValid(date)) {
        return false;
    }
    date_time = {time.Hours, time.Minutes, time.Seconds, date};
    persistDate(date);
    return true;
}

bool RtcCalendar::set(const DateTime& date_time)
{
    if (!restored_ || date_time.hours > 23U || date_time.minutes > 59U ||
        date_time.seconds > 59U || !storage::CalendarCodec::isValid(date_time.date)) {
        return false;
    }

    RTC_TimeTypeDef hal_time{date_time.hours, date_time.minutes, date_time.seconds};
    RTC_DateTypeDef hal_date{};
    hal_date.Year = date_time.date.year;
    hal_date.Month = date_time.date.month;
    hal_date.Date = date_time.date.day;
    if (HAL_RTC_SetTime(&rtc_, &hal_time, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_SetDate(&rtc_, &hal_date, RTC_FORMAT_BIN) != HAL_OK) {
        return false;
    }
    persistDate(date_time.date);
    return true;
}

} // namespace gamebox::platform
