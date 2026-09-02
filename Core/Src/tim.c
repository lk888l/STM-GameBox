/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim.c
  * @brief   TIM3 DMA-paced passive buzzer carrier.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "tim.h"

TIM_HandleTypeDef htim3;
DMA_HandleTypeDef hdma_tim3_up;

void MX_TIM3_Init(void)
{
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 71U; /* 72 MHz timer clock -> 1 MHz counter. */
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 999U;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* tim_baseHandle)
{
  if (tim_baseHandle->Instance == TIM3)
  {
    __HAL_RCC_TIM3_CLK_ENABLE();

    hdma_tim3_up.Instance = DMA1_Channel3;
    hdma_tim3_up.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_tim3_up.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_tim3_up.Init.MemInc = DMA_MINC_ENABLE;
    hdma_tim3_up.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_tim3_up.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_tim3_up.Init.Mode = DMA_CIRCULAR;
    hdma_tim3_up.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    if (HAL_DMA_Init(&hdma_tim3_up) != HAL_OK)
    {
      Error_Handler();
    }
    __HAL_LINKDMA(tim_baseHandle, hdma[TIM_DMA_ID_UPDATE], hdma_tim3_up);
  }
}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef* tim_baseHandle)
{
  if (tim_baseHandle->Instance == TIM3)
  {
    HAL_DMA_DeInit(tim_baseHandle->hdma[TIM_DMA_ID_UPDATE]);
    __HAL_RCC_TIM3_CLK_DISABLE();
  }
}
