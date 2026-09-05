# Vendored upstream manifest

The repository vendors only the files required by this firmware. Updates must be performed deliberately, followed by both target builds and all host tests.

| Component | Version / revision | Upstream | Local scope |
|---|---|---|---|
| FreeRTOS Kernel | V11.3.1, `3a22924e0a9ddbbc8b0758881c33b3422a5cc20d` | https://github.com/FreeRTOS/FreeRTOS-Kernel | kernel headers, tasks/queue/list, GCC ARM_CM3 port |
| Embedded Template Library | 20.48.1, `7f290365a9f1b72bf89b4b77ec7dbe39b228bd8e` | https://github.com/ETLCPP/etl | header-only ETL include tree |
| STM32CubeF1 | v1.8.7, `d12e75247d5bcedc734f829b394517ab4c2726e3` | https://github.com/STMicroelectronics/STM32CubeF1 | CMSIS and generated project baseline |
| STM32F1 HAL submodule | `fee494a92b5ad331f92ad21f76c66a5cb83773ee` | https://github.com/STMicroelectronics/stm32f1xx_hal_driver | ADC/I2C/TIM/RTC/UART/GPIO and core HAL sources |
| STM32F1 SPI HAL additions | STM32CubeF1 v1.8.7 / HAL v1.1.10 | STM32Cube `STM32Cube_FW_F1_V1.8.7` package; [upstream](https://github.com/STMicroelectronics/stm32f1xx_hal_driver) | `Inc/stm32f1xx_hal_spi.h`, `Src/stm32f1xx_hal_spi.c`, copied unchanged from the installed Cube package |
| Legacy GameBox reference | `edf37a1c9ca03923be745f88102271c0b4004c25` | https://github.com/lk888l/STM-GameBox | hardware mapping and feature inventory only; 6×8 font adapted |
| AppTask design reference | `794ad322cdbef3aeab7b979d43cf025adf60b1d4` | https://github.com/lk888l/esp32_idf/tree/main/esp_idf_template | lifecycle concepts only; implementation rewritten for static allocation |

Do not replace the FreeRTOS tree with the CubeMX middleware copy. `App/Config/FreeRTOSConfig.h` is part of the product and is reviewed together with every Kernel update.

The SPI additions match the installed Cube package byte-for-byte (SHA256 below). Their bytes have not been independently compared against the HAL Git commit listed for the original baseline:

- `stm32f1xx_hal_spi.h`: `261464a3f9e9ad0c8c6390eeec680f1bbf87cf5c72c80008ada16928070446e9`
- `stm32f1xx_hal_spi.c`: `74c20012ae5e579f6f720b941e66cae7b39ca47d80d873dc53d2274519795802`
