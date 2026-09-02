/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim.h
  * @brief   TIM3 DMA time base used by the passive buzzer tone generator.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __TIM_H__
#define __TIM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern TIM_HandleTypeDef htim3;
extern DMA_HandleTypeDef hdma_tim3_up;

void MX_TIM3_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __TIM_H__ */
