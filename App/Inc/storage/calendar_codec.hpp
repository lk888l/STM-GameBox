#pragma once

#include <cstdint>

namespace gamebox::storage {

struct CalendarDate {
    std::uint8_t year{26U};
    std::uint8_t month{1U};
    std::uint8_t day{1U};

    [[nodiscard]] constexpr bool operator==(const CalendarDate&) const = default;
};

/** Pure codec and calendar arithmetic for the STM32F1 software date. */
class CalendarCodec final {
public:
    static constexpr std::uint16_t kMagic = 0x4344U;

    [[nodiscard]] static bool isValid(const CalendarDate& date);
    [[nodiscard]] static std::uint8_t daysInMonth(std::uint8_t year,
                                                  std::uint8_t month);
    [[nodiscard]] static std::uint16_t encode(const CalendarDate& date);
    [[nodiscard]] static std::uint16_t checkword(std::uint16_t encoded);
    [[nodiscard]] static bool decode(std::uint16_t encoded,
                                     std::uint16_t stored_checkword,
                                     CalendarDate& date);

    /** Advances within the HAL's 2000-2099 range; saturates and returns false on overflow. */
    [[nodiscard]] static bool advance(CalendarDate& date, std::uint32_t days);
};

} // namespace gamebox::storage
