#include "platform/oled_transport.hpp"
#include <cstring>
extern "C" {
#include "task.h"
}

namespace gamebox::platform {
namespace {
std::atomic<OledTransport*> active_transport{nullptr};
static_assert(std::atomic<OledTransport*>::is_always_lock_free);
#if GAMEBOX_OLED_SPI
void dmaComplete(DMA_HandleTypeDef* dma)
{
    OledTransport* const transport = active_transport.load(std::memory_order_acquire);
    if (transport != nullptr && transport->owns(dma)) { transport->completeTransfer(true); }
}
void dmaError(DMA_HandleTypeDef* dma)
{
    OledTransport* const transport = active_transport.load(std::memory_order_acquire);
    if (transport != nullptr && transport->owns(dma)) { transport->completeTransfer(false); }
}
void deselect()
{
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
}
void pauseMs(const std::uint32_t milliseconds)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(milliseconds));
    } else { HAL_Delay(milliseconds); }
}
#endif
} // namespace

#if !GAMEBOX_OLED_SPI
extern "C" void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef* bus)
{
    OledTransport* const transport = active_transport.load(std::memory_order_acquire);
    if (transport != nullptr && transport->owns(bus)) { transport->completeTransfer(true); }
}
extern "C" void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* bus)
{
    OledTransport* const transport = active_transport.load(std::memory_order_acquire);
    if (transport != nullptr && transport->owns(bus)) { transport->completeTransfer(false); }
}
#endif

bool OledTransport::prepare()
{
    // FreeRTOS object creation masks IRQs until scheduler start.
    if (__get_PRIMASK() != 0U || __get_BASEPRI() != 0U) { ++error_count_; return false; }
    return resetPanel();
}
bool OledTransport::initialize()
{
    completion_ = xSemaphoreCreateBinaryStatic(&completion_control_block_);
    OledTransport* expected = nullptr;
    if (completion_ == nullptr ||
        !active_transport.compare_exchange_strong(expected, this,
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed)) {
        completion_ = nullptr;
        return false;
    }
    return true;
}
void OledTransport::deinitialize()
{
    // Rollback may run with IRQs masked: no tick-dependent HAL calls here.
    stopBus();
    OledTransport* expected = this;
    (void)active_transport.compare_exchange_strong(expected, nullptr,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed);
    completion_ = nullptr;
}
bool OledTransport::resetPanel()
{
#if GAMEBOX_OLED_SPI
    deselect();
    HAL_GPIO_WritePin(OLED_RESET_GPIO_Port, OLED_RESET_Pin, GPIO_PIN_SET);
    pauseMs(1U);
    HAL_GPIO_WritePin(OLED_RESET_GPIO_Port, OLED_RESET_Pin, GPIO_PIN_RESET);
    pauseMs(10U);
    HAL_GPIO_WritePin(OLED_RESET_GPIO_Port, OLED_RESET_Pin, GPIO_PIN_SET);
    pauseMs(10U);
    return true;
#else
    if (HAL_I2C_IsDeviceReady(&bus_, kAddress, 2U, kTimeoutMs) == HAL_OK) { return true; }
    ++error_count_;
    return false;
#endif
}
void OledTransport::completeTransfer(const bool success)
{
    if (!dma_transfer_active_.exchange(false, std::memory_order_acq_rel)) { return; }
#if GAMEBOX_OLED_SPI
    CLEAR_BIT(bus_.Instance->CR2, SPI_CR2_TXDMAEN);
#endif
    transfer_succeeded_.store(success, std::memory_order_release);
    if (completion_ == nullptr) { return; }
    // The F1 HAL can invoke errors synchronously from its DMA start path.
    if (__get_IPSR() == 0U) {
        (void)xSemaphoreGive(completion_);
    } else {
        BaseType_t higher_priority_task_woken = pdFALSE;
        (void)xSemaphoreGiveFromISR(completion_, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}
void OledTransport::stopBus()
{
#if GAMEBOX_OLED_SPI
    HAL_NVIC_DisableIRQ(DMA1_Channel3_IRQn);
    HAL_NVIC_DisableIRQ(SPI1_IRQn);
    deselect();
    CLEAR_BIT(bus_.Instance->CR2, SPI_CR2_TXDMAEN);
    if (bus_.hdmatx != nullptr) { (void)HAL_DMA_DeInit(bus_.hdmatx); }
    __HAL_SPI_DISABLE(&bus_);
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel3_IRQn);
    HAL_NVIC_ClearPendingIRQ(SPI1_IRQn);
#else
    HAL_NVIC_DisableIRQ(DMA1_Channel6_IRQn);
    HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
    // DeInit synchronously stops DMA; Master_Abort_IT has F1 RX-handle hazards.
    (void)HAL_I2C_DeInit(&bus_);
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel6_IRQn);
    HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
    HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);
#endif
    dma_transfer_active_.store(false, std::memory_order_release);
    transfer_succeeded_.store(false, std::memory_order_relaxed);
}
bool OledTransport::recoverBus()
{
    stopBus();
#if GAMEBOX_OLED_SPI
    const bool recovered = OLED_SPI_Reinitialize() == HAL_OK;
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
#else
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();
    const bool recovered = HAL_I2C_Init(&bus_) == HAL_OK;
    HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);
#endif
    while (completion_ != nullptr && xSemaphoreTake(completion_, 0U) == pdTRUE) {}
    return recovered;
}
bool OledTransport::writeCommands(const std::uint8_t* values, const std::uint16_t count)
{
    return write(false, values, count);
}
bool OledTransport::writeData(const std::uint8_t* values, const std::uint16_t count)
{
    return write(true, values, count);
}
bool OledTransport::write(const bool data, const std::uint8_t* values,
                          const std::uint16_t count)
{
    if (values == nullptr || count == 0U ||
        dma_transfer_active_.load(std::memory_order_acquire)) { return false; }
#if GAMEBOX_OLED_SPI
    // F1 can raise SCLK on SPE clear: settle direction and clock BEFORE CS low.
    deselect();
    __HAL_SPI_DISABLE(&bus_);
    SPI_1LINE_TX(&bus_);
    __HAL_SPI_ENABLE(&bus_);
    HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin,
                     data ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET);
    const bool success = transmit(values, count);
    deselect();
    return success;
#else
    if (count > sizeof(transfer_) - 1U) { return false; }
    transfer_[0] = data ? 0x40U : 0x00U;
    std::memcpy(&transfer_[1], values, count);
    return transmit(transfer_, static_cast<std::uint16_t>(count + 1U));
#endif
}
bool OledTransport::transmit(const std::uint8_t* const values, const std::uint16_t count)
{
    const bool scheduling = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (__get_PRIMASK() != 0U || __get_BASEPRI() != 0U ||
        (scheduling && completion_ == nullptr)) {
        ++error_count_;
        return false;
    }
    bool success = false;
#if GAMEBOX_OLED_SPI
    const std::uint32_t started = HAL_GetTick();
    if (!scheduling) {
        success = true;
        for (std::uint16_t offset = 0U; offset < count; ++offset) {
            while (__HAL_SPI_GET_FLAG(&bus_, SPI_FLAG_TXE) == RESET) {
                if (HAL_GetTick() - started >= kTimeoutMs) { success = false; break; }
            }
            if (!success) { ++timeout_count_; break; }
            *reinterpret_cast<volatile std::uint8_t*>(&bus_.Instance->DR) = values[offset];
        }
    } else {
        while (xSemaphoreTake(completion_, 0U) == pdTRUE) {}
        transfer_succeeded_.store(false, std::memory_order_relaxed);
        dma_transfer_active_.store(true, std::memory_order_release);
        bus_.hdmatx->XferCpltCallback = dmaComplete;
        bus_.hdmatx->XferErrorCallback = dmaError;
        bus_.hdmatx->XferHalfCpltCallback = nullptr;
        bus_.hdmatx->XferAbortCallback = nullptr;
        const auto source = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(values));
        const auto destination = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(&bus_.Instance->DR));
        // HAL SPI's normal ISR polls BSY using a lower-priority HAL tick and
        // can deadlock on faults. Our ISR only completes DMA and wakes a task.
        if (HAL_DMA_Start_IT(bus_.hdmatx, source, destination, count) == HAL_OK) {
            ++dma_transfer_count_;
            SET_BIT(bus_.Instance->CR2, SPI_CR2_TXDMAEN);
            if (xSemaphoreTake(completion_, pdMS_TO_TICKS(kTimeoutMs)) == pdTRUE) {
                success = transfer_succeeded_.load(std::memory_order_acquire);
            } else { ++timeout_count_; }
        }
    }
    if (success) {
        // DMA completion only means the last byte reached DR, not the OLED.
        while (__HAL_SPI_GET_FLAG(&bus_, SPI_FLAG_TXE) == RESET ||
               __HAL_SPI_GET_FLAG(&bus_, SPI_FLAG_BSY) != RESET) {
            if (HAL_GetTick() - started >= kTimeoutMs) {
                ++timeout_count_; success = false; break;
            }
            if (scheduling) { vTaskDelay(1U); }
        }
    }
#else
    if (!scheduling) {
        success = HAL_I2C_Master_Transmit(&bus_, kAddress,
                      const_cast<std::uint8_t*>(values), count, kTimeoutMs) == HAL_OK;
    } else {
        while (xSemaphoreTake(completion_, 0U) == pdTRUE) {}
        transfer_succeeded_.store(false, std::memory_order_relaxed);
        dma_transfer_active_.store(true, std::memory_order_release);
        if (HAL_I2C_Master_Transmit_DMA(&bus_, kAddress,
                                      const_cast<std::uint8_t*>(values), count) == HAL_OK) {
            ++dma_transfer_count_;
            if (xSemaphoreTake(completion_, pdMS_TO_TICKS(kTimeoutMs)) == pdTRUE) {
                success = transfer_succeeded_.load(std::memory_order_acquire);
            } else { ++timeout_count_; }
        }
    }
#endif
    if (!success) { ++error_count_; stopBus(); }
    return success;
}
} // namespace gamebox::platform
