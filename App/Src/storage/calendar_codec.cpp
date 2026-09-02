#include "storage/calendar_codec.hpp"

namespace gamebox::storage {

namespace {

constexpr std::uint16_t kCheckXor = 0x5AA5U;

[[nodiscard]] constexpr bool isLeapYear(const std::uint8_t year)
{
    // STM32F1 HAL represents years as 2000..2099, where every year divisible
    // by four is a leap year and the 2100 exception is outside the range.
    return (year % 4U) == 0U;
}

} // namespace

std::uint8_t CalendarCodec::daysInMonth(const std::uint8_t year,
                                        const std::uint8_t month)
{
    constexpr std::uint8_t lengths[] = {
        0U, 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
    };
    if (month == 0U || month > 12U) {
        return 0U;
    }
    if (month == 2U && isLeapYear(year)) {
        return 29U;
    }
    return lengths[month];
}

bool CalendarCodec::isValid(const CalendarDate& date)
{
    const std::uint8_t maximum_day = daysInMonth(date.year, date.month);
    return date.year <= 99U && maximum_day != 0U && date.day != 0U &&
           date.day <= maximum_day;
}

std::uint16_t CalendarCodec::encode(const CalendarDate& date)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(date.year & 0x7FU) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(date.month & 0x0FU) << 7U) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(date.day & 0x1FU) << 11U));
}

std::uint16_t CalendarCodec::checkword(const std::uint16_t encoded)
{
    return static_cast<std::uint16_t>(encoded ^ kCheckXor);
}

bool CalendarCodec::decode(const std::uint16_t encoded,
                           const std::uint16_t stored_checkword,
                           CalendarDate& date)
{
    if (checkword(encoded) != stored_checkword) {
        return false;
    }
    CalendarDate decoded{
        static_cast<std::uint8_t>(encoded & 0x7FU),
        static_cast<std::uint8_t>((encoded >> 7U) & 0x0FU),
        static_cast<std::uint8_t>((encoded >> 11U) & 0x1FU),
    };
    if (!isValid(decoded)) {
        return false;
    }
    date = decoded;
    return true;
}

bool CalendarCodec::advance(CalendarDate& date, std::uint32_t days)
{
    if (!isValid(date)) {
        return false;
    }
    while (days > 0U) {
        const std::uint8_t maximum_day = daysInMonth(date.year, date.month);
        if (date.day < maximum_day) {
            ++date.day;
        } else if (date.month < 12U) {
            ++date.month;
            date.day = 1U;
        } else if (date.year < 99U) {
            ++date.year;
            date.month = 1U;
            date.day = 1U;
        } else {
            date = {99U, 12U, 31U};
            return false;
        }
        --days;
    }
    return true;
}

} // namespace gamebox::storage
