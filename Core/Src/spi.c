/* Four-wire SSD1306: SPI1 mode 0, MSB first, PCLK2 / 8 = 9 MHz.
   PA5 SCK, PA7 MOSI, PA6 DC, PA4 CS, PA8 RESET. PA6 is never SPI MISO. */
#include "spi.h"

#if GAMEBOX_OLED_SPI

SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;
static HAL_StatusTypeDef spi_dma_status = HAL_ERROR;

static void OLED_SPI_HoldClockLow(void)
{
  GPIO_InitTypeDef gpio = {0};
  HAL_GPIO_WritePin(OLED_SCK_GPIO_Port, OLED_SCK_Pin, GPIO_PIN_RESET);
  gpio.Pin = OLED_SCK_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(OLED_SCK_GPIO_Port, &gpio);
}

HAL_StatusTypeDef OLED_SPI_Reinitialize(void)
{
  GPIO_InitTypeDef gpio = {0};
  HAL_StatusTypeDef status;

  __HAL_RCC_GPIOA_CLK_ENABLE();
  HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
  HAL_NVIC_DisableIRQ(DMA1_Channel3_IRQn);
  HAL_NVIC_DisableIRQ(SPI1_IRQn);
  if (hspi1.Instance == SPI1)
  {
    /* The transport has already stopped the transfer. DeInit clears stale
       HAL state as well as callbacks left by a failed DMA transaction. */
    status = HAL_SPI_DeInit(&hspi1);
    if (status != HAL_OK)
    {
      return status;
    }
  }

  __HAL_RCC_SPI1_CLK_ENABLE();
  OLED_SPI_HoldClockLow();
  __HAL_RCC_SPI1_FORCE_RESET();
  __HAL_RCC_SPI1_RELEASE_RESET();

  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_1LINE;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7U;
  status = HAL_SPI_Init(&hspi1);
  if (status != HAL_OK || spi_dma_status != HAL_OK)
  {
    return status != HAL_OK ? status : spi_dma_status;
  }

  /* On STM32F1, disabling SPI may raise SCK. Establish the peripheral idle
     level before routing SCK to AF, then normalize SPE with CS released.
     HAL's one-line transmit also clears SPE: the transport must clear SPE
     BEFORE asserting CS for every transaction, including the first one. */
  SPI_1LINE_TX(&hspi1);
  __HAL_SPI_ENABLE(&hspi1);
  gpio.Pin = OLED_SCK_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(OLED_SCK_GPIO_Port, &gpio);
  __HAL_SPI_DISABLE(&hspi1);

  HAL_NVIC_ClearPendingIRQ(DMA1_Channel3_IRQn);
  HAL_NVIC_ClearPendingIRQ(SPI1_IRQn);
  HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 5U, 0U);
  HAL_NVIC_SetPriority(SPI1_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
  HAL_NVIC_EnableIRQ(SPI1_IRQn);
  return HAL_OK;
}

void MX_SPI1_Init(void)
{
  if (OLED_SPI_Reinitialize() != HAL_OK)
  {
    Error_Handler();
  }
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *spi)
{
  if (spi->Instance == SPI1)
  {
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* SCK is still held low as a GPIO until HAL has configured SPI. */
    OLED_SPI_HoldClockLow();
    gpio.Pin = OLED_MOSI_Pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OLED_MOSI_GPIO_Port, &gpio);

    hdma_spi1_tx.Instance = DMA1_Channel3;
    hdma_spi1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_spi1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_spi1_tx.Init.Mode = DMA_NORMAL;
    hdma_spi1_tx.Init.Priority = DMA_PRIORITY_HIGH;
    spi_dma_status = HAL_DMA_Init(&hdma_spi1_tx);
    __HAL_LINKDMA(spi, hdmatx, hdma_spi1_tx);
  }
}

void HAL_SPI_MspDeInit(SPI_HandleTypeDef *spi)
{
  if (spi->Instance == SPI1)
  {
    HAL_NVIC_DisableIRQ(DMA1_Channel3_IRQn);
    HAL_NVIC_DisableIRQ(SPI1_IRQn);
    if (spi->hdmatx != NULL)
    {
      (void)HAL_DMA_DeInit(spi->hdmatx);
    }
    OLED_SPI_HoldClockLow();
    HAL_GPIO_DeInit(OLED_MOSI_GPIO_Port, OLED_MOSI_Pin);
    __HAL_RCC_SPI1_CLK_DISABLE();
  }
}

#endif
