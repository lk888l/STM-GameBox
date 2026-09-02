# ADR 0003：CubeMX 只拥有平台 C 层

状态：Accepted

CubeMX 对 STM32F1 时钟、GPIO、ADC、I2C、RTC、TIM 和启动代码很有价值，但生成器不是产品架构工具。工程保留其 C 输出，在 USER CODE 中只调用一个 `extern "C"` 启动桥；所有业务模块位于 `App`，不会被再生成覆盖。

FreeRTOS 不使用 CubeMX middleware 版本。根 CMake 独立编译锁定的上游 Kernel，并把生成的空 Cortex 异常处理函数重命名。这使 `.ioc` 可继续维护，同时 RTOS 版本和 C++ 架构独立升级。
