/* SPI1 is the default four-wire SSD1306 bus; see spi.c for F1 clock handling. */
#ifndef GAMEBOX_SPI_H
#define GAMEBOX_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern SPI_HandleTypeDef hspi1;
extern DMA_HandleTypeDef hdma_spi1_tx;

void MX_SPI1_Init(void);

/* The caller must stop DMA and release CS before recovery. Returns with CS
   high, one-line TX selected, SPE clear and no DMA request enabled. */
HAL_StatusTypeDef OLED_SPI_Reinitialize(void);

#ifdef __cplusplus
}
#endif

#endif
