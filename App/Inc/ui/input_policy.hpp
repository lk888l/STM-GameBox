#pragma once

#include "input/button.hpp"

namespace gamebox::ui {

[[nodiscard]] constexpr bool isImmediateConfirmation(const input::ButtonEvent& event)
{
    return (event.button == input::Button::enter || event.button == input::Button::jump) &&
           event.type == input::ButtonEventType::pressed;
}

/**
 * Filters recognizer tail events after an immediate menu confirmation.
 *
 * A later debounced Pressed event is always allowed because it represents a
 * new physical press. Released/Click/DoubleClick and hold events from the
 * guarded press are consumed so they cannot cascade into the newly opened
 * view.
 */
class ConfirmationGuard final {
public:
    void reset()
    {
        guarded_button_ = input::Button::count;
        long_press_seen_ = false;
    }

    void begin(const input::Button button)
    {
        guarded_button_ = button;
        long_press_seen_ = false;
    }

    [[nodiscard]] bool consume(const input::ButtonEvent& event)
    {
        if (guarded_button_ == input::Button::count || event.button != guarded_button_) {
            return false;
        }

        switch (event.type) {
        case input::ButtonEventType::pressed:
            // The recognizer cannot emit another debounced press until the
            // guarded physical press has been released. Let the new action
            // run immediately while retaining protection for its tail.
            long_press_seen_ = false;
            return false;
        case input::ButtonEventType::released:
            if (long_press_seen_) {
                reset();
            }
            return true;
        case input::ButtonEventType::click:
        case input::ButtonEventType::double_click:
            reset();
            return true;
        case input::ButtonEventType::long_press:
            long_press_seen_ = true;
            return true;
        case input::ButtonEventType::repeat:
            return true;
        }
        return true;
    }

private:
    input::Button guarded_button_{input::Button::count};
    bool long_press_seen_{false};
};

static_assert(sizeof(ConfirmationGuard) == 2U);

} // namespace gamebox::ui
