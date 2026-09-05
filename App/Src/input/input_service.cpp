#include "input/input_service.hpp"

#include "main.h"

namespace gamebox::input {

namespace {

constexpr TickType_t kScanPeriod = pdMS_TO_TICKS(5U);
constexpr std::uint32_t kInputStackDepth = 256U;
static_assert(configTICK_RATE_HZ == 1000U,
              "Button timestamps assume one FreeRTOS tick equals one millisecond");
StackType_t input_stack[kInputStackDepth];
StaticTask_t input_task_control_block;

} // namespace

InputService::InputService()
    : AppTask("input", 4U, input_stack, kInputStackDepth, input_task_control_block)
{
}

bool InputService::onInitialize()
{
    engine_.reset();
    stable_mask_.store(0U, std::memory_order_relaxed);
    dropped_events_.store(0U, std::memory_order_relaxed);
    samples_.store(0U, std::memory_order_relaxed);
    maximum_scan_gap_ms_.store(0U, std::memory_order_relaxed);
    queue_ = xQueueCreateStatic(kQueueLength,
                                sizeof(ButtonEvent),
                                queue_storage_,
                                &queue_control_block_);
    return queue_ != nullptr && start();
}

bool InputService::onDeinitialize()
{
    if (!stop()) {
        return false;
    }
    queue_ = nullptr;
    return true;
}

bool InputService::receive(ButtonEvent& event, const TickType_t timeout)
{
    return queue_ != nullptr && xQueueReceive(queue_, &event, timeout) == pdPASS;
}

bool InputService::subscribe(ButtonEventObserver& observer)
{
    for (ButtonEventObserver* const registered : observers_) {
        if (registered == &observer) {
            return true;
        }
    }
    for (ButtonEventObserver*& slot : observers_) {
        if (slot == nullptr) {
            slot = &observer;
            return true;
        }
    }
    return false;
}

ButtonMask InputService::readHardware()
{
    // Board invariant: all eight keys share GPIOB, so one IDR read is an
    // electrically consistent snapshot. BTN_UP_GPIO_Port is CubeMX-generated.
    const std::uint32_t active_low = ~BTN_UP_GPIO_Port->IDR;
    ButtonMask pressed = 0U;
    if ((active_low & BTN_UP_Pin) != 0U) { pressed |= maskFor(Button::up); }
    if ((active_low & BTN_DOWN_Pin) != 0U) { pressed |= maskFor(Button::down); }
    if ((active_low & BTN_LEFT_Pin) != 0U) { pressed |= maskFor(Button::left); }
    if ((active_low & BTN_RIGHT_Pin) != 0U) { pressed |= maskFor(Button::right); }
    if ((active_low & BTN_JUMP_Pin) != 0U) { pressed |= maskFor(Button::jump); }
    if ((active_low & BTN_FUNC_Pin) != 0U) { pressed |= maskFor(Button::function); }
    if ((active_low & BTN_ENTER_Pin) != 0U) { pressed |= maskFor(Button::enter); }
    if ((active_low & BTN_BACK_Pin) != 0U) { pressed |= maskFor(Button::back); }
    return pressed;
}

void InputService::enqueueFromEngine(void* const context, const ButtonEvent& event)
{
    static_cast<InputService*>(context)->enqueue(event);
}

void InputService::enqueue(const ButtonEvent& event)
{
    if (xQueueSendToBack(queue_, &event, 0U) != pdPASS) {
        ButtonEvent discarded{};
        (void)xQueueReceive(queue_, &discarded, 0U);
        (void)dropped_events_.fetch_add(1U, std::memory_order_relaxed);
        (void)xQueueSendToBack(queue_, &event, 0U);
    }
    for (ButtonEventObserver* const observer : observers_) {
        if (observer != nullptr) {
            observer->onButtonEvent(event);
        }
    }
}

void InputService::run()
{
    TickType_t sampled_at = xTaskGetTickCount();
    while (!shouldExit()) {
        const TickType_t now = xTaskGetTickCount();
        const auto gap_ms = static_cast<std::uint32_t>(now - sampled_at);
        if (gap_ms > maximum_scan_gap_ms_.load(std::memory_order_relaxed)) {
            maximum_scan_gap_ms_.store(gap_ms, std::memory_order_relaxed);
        }
        sampled_at = now;
        const auto now_ms = static_cast<std::uint32_t>(now);
        engine_.sample(readHardware(), now_ms, enqueueFromEngine, this);
        stable_mask_.store(engine_.stableMask(), std::memory_order_relaxed);
        (void)samples_.fetch_add(1U, std::memory_order_relaxed);

        // Missed GPIO samples cannot be reconstructed. Schedule from the real
        // sample time and block even if processing exceeded the scan period.
        TickType_t next_wake = now;
        if (xTaskPeriodicDelay(&next_wake, kScanPeriod) == pdFALSE) {
            vTaskDelay(1U);
        }
    }
}

} // namespace gamebox::input
