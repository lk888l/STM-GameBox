#pragma once

#include "input/button.hpp"

namespace gamebox::input {

/**
 * Synchronous observer boundary for button events. Implementations must only
 * perform bounded, non-blocking work (normally a zero-timeout queue write).
 */
class ButtonEventObserver {
public:
    ButtonEventObserver() = default;
    ButtonEventObserver(const ButtonEventObserver&) = delete;
    ButtonEventObserver& operator=(const ButtonEventObserver&) = delete;
    virtual void onButtonEvent(const ButtonEvent& event) = 0;

protected:
    ~ButtonEventObserver() = default;
};

} // namespace gamebox::input
