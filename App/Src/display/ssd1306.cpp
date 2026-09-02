#include "display/ssd1306.hpp"

#include <cstring>

namespace gamebox::display {

namespace {

std::atomic<Ssd1306*> active_display{nullptr};

} // namespace

extern "C" void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef* const bus)
{
    Ssd1306* const display = active_display.load(std::memory_order_acquire);
    if (display != nullptr && bus != nullptr && bus->Instance == I2C1) {
        display->completeTransferFromIsr(true);
    }
}

extern "C" void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* const bus)
{
    Ssd1306* const display = active_display.load(std::memory_order_acquire);
    if (display != nullptr && bus != nullptr && bus->Instance == I2C1) {
        display->completeTransferFromIsr(false);
    }
}

extern "C" void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef* const bus)
{
    Ssd1306* const display = active_display.load(std::memory_order_acquire);
    if (display != nullptr && bus != nullptr && bus->Instance == I2C1) {
        display->completeTransferFromIsr(false);
    }
}

Ssd1306::Ssd1306(I2C_HandleTypeDef& bus) : bus_(bus)
{
}

bool Ssd1306::commands(const std::uint8_t* const values, const std::uint16_t count)
{
    std::uint8_t packet[32]{};
    if (values == nullptr || count == 0U ||
        static_cast<std::size_t>(count) + 1U > sizeof(packet)) {
        return false;
    }
    packet[0] = 0x00U;
    std::memcpy(&packet[1], values, count);
    return transmit(packet, static_cast<std::uint16_t>(count + 1U));
}

bool Ssd1306::command(const std::uint8_t value)
{
    return commands(&value, 1U);
}

bool Ssd1306::prepare()
{
    static constexpr std::uint8_t init_sequence[] = {
        0xAEU, 0xD5U, 0x80U, 0xA8U, 0x3FU, 0xD3U, 0x00U, 0x40U,
        0xA1U, 0xC8U, 0xDAU, 0x12U, 0x81U, 0xCFU, 0xD9U, 0xF1U,
        0xDBU, 0x30U, 0xA4U, 0xA6U, 0x8DU, 0x14U, 0x20U, 0x02U,
        0xAFU,
    };

    error_count_ = 0U;
    dma_transfer_count_ = 0U;
    timeout_count_ = 0U;
    prepared_ = false;
    shadow_valid_ = false;
    if (__get_PRIMASK() != 0U) {
        ++error_count_;
        return false;
    }
    if (HAL_I2C_IsDeviceReady(&bus_, kAddress, 2U, kTimeoutMs) != HAL_OK) {
        ++error_count_;
        return false;
    }
    prepared_ = commands(init_sequence,
                         static_cast<std::uint16_t>(sizeof(init_sequence)));
    return prepared_;
}

bool Ssd1306::onInitialize()
{
    if (!prepared_) {
        return false;
    }
    completion_ = xSemaphoreCreateBinaryStatic(&completion_control_block_);
    Ssd1306* expected = nullptr;
    if (completion_ == nullptr ||
        !active_display.compare_exchange_strong(expected,
                                                this,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {
        completion_ = nullptr;
        return false;
    }
    return true;
}

bool Ssd1306::onDeinitialize()
{
    shadow_valid_ = false;
    prepared_ = false;
    Ssd1306* expected = this;
    (void)active_display.compare_exchange_strong(expected,
                                                 nullptr,
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed);
    completion_ = nullptr;
    // Do not perform a timeout-based HAL transfer here: pre-scheduler rollback
    // can happen after FreeRTOS has intentionally masked interrupts.
    return true;
}

void Ssd1306::completeTransferFromIsr(const bool success)
{
    transfer_succeeded_.store(success, std::memory_order_release);
    if (completion_ == nullptr) {
        return;
    }
    BaseType_t higher_priority_task_woken = pdFALSE;
    (void)xSemaphoreGiveFromISR(completion_, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

bool Ssd1306::transmit(std::uint8_t* const bytes, const std::uint16_t count)
{
    if (bytes == nullptr || count == 0U) {
        return false;
    }
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        if (__get_PRIMASK() != 0U) {
            ++error_count_;
            return false;
        }
        if (HAL_I2C_Master_Transmit(&bus_, kAddress, bytes, count, kTimeoutMs) == HAL_OK) {
            return true;
        }
        ++error_count_;
        return false;
    }
    if (completion_ == nullptr) {
        ++error_count_;
        return false;
    }

    while (xSemaphoreTake(completion_, 0U) == pdTRUE) {
    }
    transfer_succeeded_.store(false, std::memory_order_relaxed);
    if (HAL_I2C_Master_Transmit_DMA(&bus_, kAddress, bytes, count) != HAL_OK) {
        ++error_count_;
        return false;
    }
    ++dma_transfer_count_;
    if (xSemaphoreTake(completion_, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) {
        ++timeout_count_;
        ++error_count_;
        (void)HAL_I2C_Master_Abort_IT(&bus_, kAddress);
        (void)xSemaphoreTake(completion_, pdMS_TO_TICKS(2U));
        return false;
    }
    if (!transfer_succeeded_.load(std::memory_order_acquire)) {
        ++error_count_;
        return false;
    }
    return true;
}

bool Ssd1306::transmitPage(const std::uint8_t page, const std::uint8_t* const pixels)
{
    const std::uint8_t address[] = {
        static_cast<std::uint8_t>(0xB0U | page), 0x00U, 0x10U,
    };
    if (!commands(address, sizeof(address))) {
        return false;
    }

    transfer_[0] = 0x40U;
    std::memcpy(&transfer_[1], pixels, Canvas::kWidth);
    return transmit(transfer_, static_cast<std::uint16_t>(sizeof(transfer_)));
}

bool Ssd1306::flush(Canvas& canvas)
{
    const std::uint8_t dirty = canvas.dirtyPages();
    if (dirty == 0U && shadow_valid_) {
        return true;
    }

    for (std::uint8_t page = 0U; page < Canvas::kPageCount; ++page) {
        const std::uint8_t page_bit = static_cast<std::uint8_t>(1U << page);
        if (shadow_valid_ && (dirty & page_bit) == 0U) {
            continue;
        }
        const std::size_t offset = static_cast<std::size_t>(page) * Canvas::kWidth;
        const std::uint8_t* const source = &canvas.data()[offset];
        if (shadow_valid_ && std::memcmp(source, &shadow_[offset], Canvas::kWidth) == 0) {
            continue;
        }
        if (!transmitPage(page, source)) {
            return false;
        }
        std::memcpy(&shadow_[offset], source, Canvas::kWidth);
    }

    shadow_valid_ = true;
    canvas.clearDirty();
    return true;
}

bool Ssd1306::setContrast(const std::uint8_t contrast)
{
    const std::uint8_t values[] = {0x81U, contrast};
    return commands(values, sizeof(values));
}

bool Ssd1306::setEnabled(const bool enabled)
{
    return command(enabled ? 0xAFU : 0xAEU);
}

} // namespace gamebox::display
