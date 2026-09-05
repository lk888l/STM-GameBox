/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    dma.c
  * @brief   DMA1 clock and interrupt configuration.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "dma.h"

void MX_DMA_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* All handlers that call FreeRTOS FromISR APIs must have a numerically
     equal-or-lower urgency than configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY. */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
#if GAMEBOX_OLED_SPI
  HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
#else
  HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);
#endif
}
