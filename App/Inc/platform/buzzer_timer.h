#pragma once

#include <stdint.h>

#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

HAL_StatusTypeDef GameBox_Buzzer_Start(uint16_t frequency_hz);
void GameBox_Buzzer_Stop(void);

#ifdef __cplusplus
}
#endif
