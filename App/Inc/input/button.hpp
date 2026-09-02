#pragma once

#include <cstddef>
#include <cstdint>

namespace gamebox::input {

enum class Button : std::uint8_t {
    up,
    down,
    left,
    right,
    jump,
    function,
    enter,
    back,
    count,
};

enum class ButtonEventType : std::uint8_t {
    pressed,
    released,
    click,
    double_click,
    long_press,
    repeat,
};

using ButtonMask = std::uint8_t;

struct ButtonEvent {
    Button button{Button::up};
    ButtonEventType type{ButtonEventType::pressed};
    std::uint32_t timestamp_ms{0U};
    std::uint32_t held_ms{0U};
};

inline constexpr std::size_t kButtonCount = static_cast<std::size_t>(Button::count);

[[nodiscard]] constexpr ButtonMask maskFor(const Button button)
{
    return static_cast<ButtonMask>(1U << static_cast<std::uint8_t>(button));
}

[[nodiscard]] constexpr bool isPressed(const ButtonMask mask, const Button button)
{
    return (mask & maskFor(button)) != 0U;
}

} // namespace gamebox::input
