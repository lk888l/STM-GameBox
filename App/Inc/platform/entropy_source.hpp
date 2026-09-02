#pragma once

#include <cstdint>

#include "adc.h"

namespace gamebox::platform {

class EntropySource final {
public:
    explicit EntropySource(ADC_HandleTypeDef& adc) : adc_(adc) {}

    /** Collects a non-cryptographic game seed before the scheduler starts. */
    [[nodiscard]] std::uint32_t collectSeed();

private:
    static std::uint32_t mix(std::uint32_t state, std::uint32_t value);

    ADC_HandleTypeDef& adc_;
};

} // namespace gamebox::platform
