#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t ignored; } GPIO_TypeDef;
typedef struct { uint32_t CR1, CR2, SR, DR; } SPI_TypeDef;
typedef struct { uint32_t ignored; } I2C_TypeDef;
typedef struct __DMA_HandleTypeDef {
    void (*XferCpltCallback)(struct __DMA_HandleTypeDef*);
    void (*XferErrorCallback)(struct __DMA_HandleTypeDef*);
    void (*XferHalfCpltCallback)(struct __DMA_HandleTypeDef*);
    void (*XferAbortCallback)(struct __DMA_HandleTypeDef*);
} DMA_HandleTypeDef;
typedef struct { SPI_TypeDef* Instance; DMA_HandleTypeDef* hdmatx; } SPI_HandleTypeDef;
typedef struct { I2C_TypeDef* Instance; DMA_HandleTypeDef* hdmatx; } I2C_HandleTypeDef;

enum {
    RESET = 0,
    SPI_CR1_SPE = 0x40,
    SPI_CR1_BIDIOE = 0x4000,
    SPI_CR2_TXDMAEN = 2,
    SPI_FLAG_TXE = 2,
    SPI_FLAG_BSY = 0x80,
    DMA1_Channel3_IRQn = 3,
    DMA1_Channel6_IRQn = 6,
    SPI1_IRQn = 10,
    I2C1_EV_IRQn = 11,
    I2C1_ER_IRQn = 12,
    OLED_CS_Pin = 0x10,
    OLED_DC_Pin = 0x40,
    OLED_RESET_Pin = 0x100
};
extern GPIO_TypeDef fake_gpio;
#define OLED_CS_GPIO_Port (&fake_gpio)
#define OLED_DC_GPIO_Port (&fake_gpio)
#define OLED_RESET_GPIO_Port (&fake_gpio)

void fake_set_bit(uint32_t* value, uint32_t bits);
void fake_clear_bit(uint32_t* value, uint32_t bits);
uint32_t fake_spi_get_flag(SPI_HandleTypeDef* bus, uint32_t flag);
#define SET_BIT(value, bits) fake_set_bit(&(value), (bits))
#define CLEAR_BIT(value, bits) fake_clear_bit(&(value), (bits))
#define __HAL_SPI_DISABLE(bus) CLEAR_BIT((bus)->Instance->CR1, SPI_CR1_SPE)
#define __HAL_SPI_ENABLE(bus) SET_BIT((bus)->Instance->CR1, SPI_CR1_SPE)
#define SPI_1LINE_TX(bus) SET_BIT((bus)->Instance->CR1, SPI_CR1_BIDIOE)
#define __HAL_SPI_GET_FLAG(bus, flag) fake_spi_get_flag((bus), (flag))
#define __HAL_RCC_I2C1_FORCE_RESET() fake_i2c_force_reset()
#define __HAL_RCC_I2C1_RELEASE_RESET() fake_i2c_release_reset()

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t milliseconds);
uint32_t __get_PRIMASK(void);
uint32_t __get_BASEPRI(void);
uint32_t __get_IPSR(void);
void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState value);
void HAL_NVIC_DisableIRQ(int irq);
void HAL_NVIC_EnableIRQ(int irq);
void HAL_NVIC_ClearPendingIRQ(int irq);
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef* dma, uint32_t source,
                                 uint32_t destination, uint32_t count);
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef* dma);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef* bus, uint16_t address,
                                     uint32_t trials, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef* bus, uint16_t address,
                                       uint8_t* data, uint16_t count, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Transmit_DMA(I2C_HandleTypeDef* bus, uint16_t address,
                                           uint8_t* data, uint16_t count);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef* bus);
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef* bus);
HAL_StatusTypeDef OLED_SPI_Reinitialize(void);
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef* bus);
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* bus);
void fake_i2c_force_reset(void);
void fake_i2c_release_reset(void);

#ifdef __cplusplus
}
#endif
