#include "diagnostics/uart_dma_service.hpp"

namespace gamebox::diagnostics {

namespace {

constexpr std::uint32_t kTaskStackDepth = 224U;
constexpr TickType_t kTransmitTimeout = pdMS_TO_TICKS(20U);
StackType_t uart_stack[kTaskStackDepth];
StaticTask_t uart_task_control_block;
std::atomic<UartDmaService*> active_service{nullptr};

const char* buttonName(const input::Button button)
{
    switch (button) {
    case input::Button::up: return "UP";
    case input::Button::down: return "DOWN";
    case input::Button::left: return "LEFT";
    case input::Button::right: return "RIGHT";
    case input::Button::jump: return "JUMP";
    case input::Button::function: return "FUNC";
    case input::Button::enter: return "ENTER";
    case input::Button::back: return "BACK";
    default: return "?";
    }
}

const char* eventName(const input::ButtonEventType type)
{
    switch (type) {
    case input::ButtonEventType::pressed: return "DOWN";
    case input::ButtonEventType::released: return "UP";
    case input::ButtonEventType::click: return "CLICK";
    case input::ButtonEventType::double_click: return "DOUBLE";
    case input::ButtonEventType::long_press: return "LONG";
    case input::ButtonEventType::repeat: return "REPEAT";
    }
    return "?";
}

void append(char* const output,
            const std::size_t capacity,
            std::size_t& size,
            const char* text)
{
    while (*text != '\0' && size + 1U < capacity) {
        output[size++] = *text++;
    }
}

void appendUnsigned(char* const output,
                    const std::size_t capacity,
                    std::size_t& size,
                    std::uint32_t value)
{
    char reversed[10]{};
    std::size_t digits = 0U;
    do {
        reversed[digits++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && digits < sizeof(reversed));
    while (digits > 0U && size + 1U < capacity) {
        output[size++] = reversed[--digits];
    }
}

} // namespace

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* const uart)
{
    UartDmaService* const service = active_service.load(std::memory_order_acquire);
    if (service != nullptr && uart != nullptr && uart->Instance == USART1) {
        service->completeTransferFromIsr(true);
    }
}

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef* const uart)
{
    UartDmaService* const service = active_service.load(std::memory_order_acquire);
    if (service != nullptr && uart != nullptr && uart->Instance == USART1) {
        service->completeTransferFromIsr(false);
    }
}

UartDmaService::UartDmaService(UART_HandleTypeDef& uart)
    : AppTask("uart-dma", 1U, uart_stack, kTaskStackDepth, uart_task_control_block),
      uart_(uart)
{
}

bool UartDmaService::onInitialize()
{
    dropped_events_.store(0U, std::memory_order_relaxed);
    errors_.store(0U, std::memory_order_relaxed);
    queue_ = xQueueCreateStatic(kQueueLength,
                                sizeof(input::ButtonEvent),
                                queue_storage_,
                                &queue_control_block_);
    completion_ = xSemaphoreCreateBinaryStatic(&completion_control_block_);
    UartDmaService* expected = nullptr;
    if (queue_ == nullptr || completion_ == nullptr ||
        !active_service.compare_exchange_strong(expected,
                                                this,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {
        queue_ = nullptr;
        completion_ = nullptr;
        return false;
    }
    return start();
}

bool UartDmaService::onDeinitialize()
{
    if (!stop()) {
        return false;
    }
    (void)HAL_UART_AbortTransmit(&uart_);
    UartDmaService* expected = this;
    (void)active_service.compare_exchange_strong(expected,
                                                 nullptr,
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed);
    queue_ = nullptr;
    completion_ = nullptr;
    return true;
}

void UartDmaService::onButtonEvent(const input::ButtonEvent& event)
{
    if (queue_ == nullptr) {
        return;
    }
    if (xQueueSendToBack(queue_, &event, 0U) == pdPASS) {
        return;
    }
    input::ButtonEvent discarded{};
    (void)xQueueReceive(queue_, &discarded, 0U);
    (void)dropped_events_.fetch_add(1U, std::memory_order_relaxed);
    (void)xQueueSendToBack(queue_, &event, 0U);
}

void UartDmaService::completeTransferFromIsr(const bool success)
{
    transfer_succeeded_.store(success, std::memory_order_release);
    if (completion_ == nullptr) {
        return;
    }
    BaseType_t higher_priority_task_woken = pdFALSE;
    (void)xSemaphoreGiveFromISR(completion_, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

bool UartDmaService::transmit(std::uint8_t* const bytes, const std::uint16_t count)
{
    while (xSemaphoreTake(completion_, 0U) == pdTRUE) {
    }
    transfer_succeeded_.store(false, std::memory_order_relaxed);
    if (HAL_UART_Transmit_DMA(&uart_, bytes, count) != HAL_OK) {
        (void)errors_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    if (xSemaphoreTake(completion_, kTransmitTimeout) != pdTRUE) {
        (void)HAL_UART_AbortTransmit(&uart_);
        (void)errors_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    if (!transfer_succeeded_.load(std::memory_order_acquire)) {
        (void)errors_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    return true;
}

std::uint16_t UartDmaService::formatEvent(const input::ButtonEvent& event,
                                          char* const output,
                                          const std::size_t capacity)
{
    std::size_t size = 0U;
    append(output, capacity, size, "BTN ");
    appendUnsigned(output, capacity, size, event.timestamp_ms);
    append(output, capacity, size, " ");
    append(output, capacity, size, buttonName(event.button));
    append(output, capacity, size, " ");
    append(output, capacity, size, eventName(event.type));
    append(output, capacity, size, " ");
    appendUnsigned(output, capacity, size, event.held_ms);
    append(output, capacity, size, "\r\n");
    if (capacity > 0U) {
        output[size < capacity ? size : capacity - 1U] = '\0';
    }
    return static_cast<std::uint16_t>(size);
}

void UartDmaService::run()
{
    char boot[] = "GAMEBOX FW2 UART-TX-DMA READY\r\n";
    (void)transmit(reinterpret_cast<std::uint8_t*>(boot),
                   static_cast<std::uint16_t>(sizeof(boot) - 1U));
    while (!shouldExit()) {
        input::ButtonEvent event{};
        if (xQueueReceive(queue_, &event, pdMS_TO_TICKS(50U)) != pdPASS) {
            continue;
        }
        char line[64]{};
        const std::uint16_t length = formatEvent(event, line, sizeof(line));
        (void)transmit(reinterpret_cast<std::uint8_t*>(line), length);
    }
}

} // namespace gamebox::diagnostics
