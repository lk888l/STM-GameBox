#include "input/button_engine.hpp"

namespace gamebox::input {

ButtonEngine::ButtonEngine(const ButtonTiming timing) : timing_(timing)
{
    if (timing_.debounce_ms == 0U) {
        timing_.debounce_ms = 1U;
    }
    if (timing_.double_click_ms < timing_.debounce_ms) {
        timing_.double_click_ms = timing_.debounce_ms;
    }
    if (timing_.long_press_ms <= timing_.double_click_ms) {
        timing_.long_press_ms = timing_.double_click_ms + 1U;
    }
    if (timing_.repeat_period_ms == 0U) {
        timing_.repeat_period_ms = 1U;
    }
}

void ButtonEngine::reset()
{
    for (State& state : states_) {
        state = {};
    }
}

bool ButtonEngine::elapsed(const std::uint32_t now,
                           const std::uint32_t since,
                           const std::uint32_t interval)
{
    return static_cast<std::uint32_t>(now - since) >= interval;
}

bool ButtonEngine::deadlineReached(const std::uint32_t now, const std::uint32_t deadline)
{
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

void ButtonEngine::emit(const EventCallback callback,
                        void* const context,
                        const Button button,
                        const ButtonEventType type,
                        const std::uint32_t timestamp_ms,
                        const std::uint32_t held_ms)
{
    if (callback != nullptr) {
        callback(context, ButtonEvent{button, type, timestamp_ms, held_ms});
    }
}

void ButtonEngine::sample(const ButtonMask pressed_mask,
                          const std::uint32_t now_ms,
                          const EventCallback callback,
                          void* const context)
{
    for (std::size_t index = 0U; index < kButtonCount; ++index) {
        const auto button = static_cast<Button>(index);
        State& state = states_[index];
        const bool raw_pressed = isPressed(pressed_mask, button);

        if (raw_pressed != state.raw_pressed) {
            state.raw_pressed = raw_pressed;
            state.raw_changed_ms = now_ms;
        }

        if (state.raw_pressed != state.stable_pressed &&
            elapsed(now_ms, state.raw_changed_ms, timing_.debounce_ms)) {
            state.stable_pressed = state.raw_pressed;

            if (state.stable_pressed) {
                state.pressed_ms = now_ms;
                state.long_emitted = false;
                emit(callback, context, button, ButtonEventType::pressed, now_ms, 0U);
            } else {
                const std::uint32_t held_ms = now_ms - state.pressed_ms;
                emit(callback, context, button, ButtonEventType::released, now_ms, held_ms);

                if (!state.long_emitted) {
                    if (state.click_pending &&
                        !elapsed(now_ms, state.click_released_ms, timing_.double_click_ms)) {
                        state.click_pending = false;
                        emit(callback,
                             context,
                             button,
                             ButtonEventType::double_click,
                             now_ms,
                             held_ms);
                    } else {
                        if (state.click_pending) {
                            emit(callback,
                                 context,
                                 button,
                                 ButtonEventType::click,
                                 state.click_released_ms,
                                 state.click_held_ms);
                        }
                        state.click_pending = true;
                        state.click_released_ms = now_ms;
                        state.click_held_ms = held_ms;
                    }
                }
            }
        }

        if (state.stable_pressed) {
            const std::uint32_t held_ms = now_ms - state.pressed_ms;
            if (!state.long_emitted && held_ms >= timing_.long_press_ms) {
                state.long_emitted = true;
                state.next_repeat_ms = now_ms + timing_.repeat_delay_ms;
                emit(callback,
                     context,
                     button,
                     ButtonEventType::long_press,
                     now_ms,
                     held_ms);
            } else if (state.long_emitted && deadlineReached(now_ms, state.next_repeat_ms)) {
                do {
                    state.next_repeat_ms += timing_.repeat_period_ms;
                } while (deadlineReached(now_ms, state.next_repeat_ms));
                emit(callback,
                     context,
                     button,
                     ButtonEventType::repeat,
                     now_ms,
                     held_ms);
            }
        }

        if (state.click_pending &&
            elapsed(now_ms, state.click_released_ms, timing_.double_click_ms)) {
            state.click_pending = false;
            emit(callback,
                 context,
                 button,
                 ButtonEventType::click,
                 state.click_released_ms,
                 state.click_held_ms);
        }
    }
}

ButtonMask ButtonEngine::stableMask() const
{
    ButtonMask result = 0U;
    for (std::size_t index = 0U; index < kButtonCount; ++index) {
        if (states_[index].stable_pressed) {
            result |= maskFor(static_cast<Button>(index));
        }
    }
    return result;
}

} // namespace gamebox::input
