#include "platform/entropy_source.hpp"

#include <atomic>

#include "main.h"

namespace gamebox::platform {

namespace {

std::atomic<bool> adc_dma_complete{false};
std::atomic<bool> adc_dma_failed{false};
static_assert(std::atomic<bool>::is_always_lock_free);

} // namespace

extern "C" void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* const adc)
{
    if (adc != nullptr && adc->Instance == ADC1) {
        adc_dma_complete.store(true, std::memory_order_release);
    }
}

extern "C" void HAL_ADC_ErrorCallback(ADC_HandleTypeDef* const adc)
{
    if (adc != nullptr && adc->Instance == ADC1) {
        adc_dma_failed.store(true, std::memory_order_release);
    }
}

std::uint32_t EntropySource::mix(std::uint32_t state, const std::uint32_t value)
{
    state ^= value + 0x9E3779B9U + (state << 6U) + (state >> 2U);
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

std::uint32_t EntropySource::collectSeed()
{
    std::uint32_t seed = mix(HAL_GetUIDw0(), HAL_GetUIDw1());
    seed = mix(seed, HAL_GetUIDw2());
    seed = mix(seed, HAL_GetTick());

    // The Cortex-M3 FreeRTOS port can keep PRIMASK set after a kernel object is
    // created but before scheduling starts. DMA IRQ and HAL tick cannot make
    // progress in that state, so retain the deterministic fallback seed.
    if (__get_PRIMASK() != 0U) {
        return seed != 0U ? seed : 0xA341316CU;
    }
    if (HAL_ADCEx_Calibration_Start(&adc_) != HAL_OK) {
        return seed != 0U ? seed : 0xA341316CU;
    }
    alignas(std::uint32_t) std::uint16_t samples[16]{};
    adc_dma_complete.store(false, std::memory_order_relaxed);
    adc_dma_failed.store(false, std::memory_order_relaxed);
    if (HAL_ADC_Start_DMA(&adc_,
                          reinterpret_cast<std::uint32_t*>(samples),
                          static_cast<std::uint32_t>(sizeof(samples) / sizeof(samples[0]))) !=
        HAL_OK) {
        return seed != 0U ? seed : 0xA341316CU;
    }

    const std::uint32_t started_ms = HAL_GetTick();
    while (!adc_dma_complete.load(std::memory_order_acquire) &&
           !adc_dma_failed.load(std::memory_order_acquire) &&
           static_cast<std::uint32_t>(HAL_GetTick() - started_ms) < 5U) {
        __NOP();
    }
    (void)HAL_ADC_Stop_DMA(&adc_);
    if (adc_dma_complete.load(std::memory_order_acquire) &&
        !adc_dma_failed.load(std::memory_order_acquire)) {
        for (std::uint32_t sample = 0U;
             sample < static_cast<std::uint32_t>(sizeof(samples) / sizeof(samples[0]));
             ++sample) {
            seed = mix(seed, static_cast<std::uint32_t>(samples[sample]) | (sample << 16U));
        }
    }
    return seed != 0U ? seed : 0xA341316CU;
}

} // namespace gamebox::platform
