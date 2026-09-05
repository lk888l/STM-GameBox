#include "platform/buzzer_timer.h"

#include <cstdint>

#include "main.h"
#include "tim.h"

namespace {

// DMA alternates a GPIO set and reset write on every TIM2 update. This turns
// PA12 into a hardware-paced square wave without an interrupt per half-cycle.
alignas(std::uint32_t) std::uint32_t buzzer_edges[2] = {
    static_cast<std::uint32_t>(BUZZER_Pin),
    static_cast<std::uint32_t>(BUZZER_Pin) << 16U,
};

} // namespace

extern "C" HAL_StatusTypeDef GameBox_Buzzer_Start(const std::uint16_t frequency_hz)
{
    if (frequency_hz < 100U || frequency_hz > 5000U) {
        return HAL_ERROR;
    }

    const std::uint32_t half_period = 1'000'000U / (2U * frequency_hz);
    if (half_period == 0U) {
        return HAL_ERROR;
    }

    GameBox_Buzzer_Stop();
    __HAL_TIM_SET_AUTORELOAD(&htim2, half_period - 1U);
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
    const auto source = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(buzzer_edges));
    const auto destination = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&BUZZER_GPIO_Port->BSRR));
    if (HAL_DMA_Start(&hdma_tim2_up, source, destination, 2U) != HAL_OK) {
        return HAL_ERROR;
    }
    __HAL_TIM_ENABLE_DMA(&htim2, TIM_DMA_UPDATE);
    __HAL_TIM_ENABLE(&htim2);
    return HAL_OK;
}

extern "C" void GameBox_Buzzer_Stop(void)
{
    __HAL_TIM_DISABLE(&htim2);
    __HAL_TIM_DISABLE_DMA(&htim2, TIM_DMA_UPDATE);
    if (hdma_tim2_up.State == HAL_DMA_STATE_BUSY) {
        (void)HAL_DMA_Abort(&hdma_tim2_up);
    }
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
}
