#pragma once

#include <cstdint>

#include "input/button.hpp"

namespace gamebox::input {

struct ButtonTiming {
    std::uint32_t debounce_ms{20U};
    std::uint32_t double_click_ms{280U};
    std::uint32_t long_press_ms{650U};
    std::uint32_t repeat_delay_ms{110U};
    std::uint32_t repeat_period_ms{90U};
};

class ButtonEngine final {
public:
    using EventCallback = void (*)(void* context, const ButtonEvent& event);

    explicit ButtonEngine(ButtonTiming timing = {});

    void reset();
    void sample(ButtonMask pressed_mask,
                std::uint32_t now_ms,
                EventCallback callback,
                void* context);

    [[nodiscard]] ButtonMask stableMask() const;

private:
    struct State {
        bool raw_pressed{false};
        bool stable_pressed{false};
        bool long_emitted{false};
        bool click_pending{false};
        std::uint32_t raw_changed_ms{0U};
        std::uint32_t pressed_ms{0U};
        std::uint32_t click_released_ms{0U};
        std::uint32_t click_held_ms{0U};
        std::uint32_t next_repeat_ms{0U};
    };

    static bool elapsed(std::uint32_t now, std::uint32_t since, std::uint32_t interval);
    static bool deadlineReached(std::uint32_t now, std::uint32_t deadline);
    static void emit(EventCallback callback,
                     void* context,
                     Button button,
                     ButtonEventType type,
                     std::uint32_t timestamp_ms,
                     std::uint32_t held_ms);

    ButtonTiming timing_;
    State states_[kButtonCount]{};
};

} // namespace gamebox::input
