# ADR 0005：固定 DMA 映射与按键事件订阅

状态：Accepted

STM32F103 的 DMA 请求映射固定，且不同外设可能争用同一通道。固件在启用 DMA 前建立全局分配表：ADC1→DMA1 Channel 1、TIM2_UP→Channel 2、SPI1_TX→Channel 3、USART1_TX→Channel 4、I2C1_TX→Channel 6。蜂鸣器由原 TIM3_UP/Channel 3 迁到 TIM2，避免与 SPI1_TX 冲突。TIM2 使用无 IRQ 的 circular 模式；SPI DMA/全局 IRQ 优先级为 5，其余启用的 Normal DMA 为 6，均符合 FreeRTOS FromISR API 约束。

显示接口由构建参数选择，默认 SPI，保留 I2C；未选接口不配置引脚或 DMA。控制器层 `Ssd1306` 管理 SSD1306 命令、shadow、缓存设置和离线退避，传输层 `OledTransport` 管理总线、GPIO、DMA 和静态二值完成信号量。变化帧在 SPI 上一次发送 1024 B，I2C 上按 128 B 差分页发送；相同画面不发送。

OLED 页面和命令的 DMA 源缓冲必须存活到传输确认终止。SPI 帧数据先复制到控制器拥有的 shadow，再以它作为 DMA 源；成功前 shadow 无效，失败后不能拿已暂存的数据当成成功帧。SPI 命令缓冲存活于本次同步调用期间；I2C 使用带控制前缀的 129 B 传输成员缓冲。传输接口只在 DMA 已结束或同步停止后返回，UI 才能再次渲染或复用源数据。原子活动标志保护完成/错误通知；回调根据实际 ISR/任务上下文选择 FreeRTOS API，不能仅根据 HAL 回调名称判断执行上下文。

STM32F1 HAL 的 I2C 中止 API 可能在任务中同步调用名为 IT 的回调，且 TX 完成竞态可能误选未配置的 RX DMA 句柄，因此超时路径不调用该 API。SPI 的默认 HAL DMA 完成回调会在高优先级 ISR 内轮询 BSY，此时低优先级 TIM4 tick 无法推进，可能使所谓超时永久等待。SPI 使用 HAL DMA 的专用完成回调，只通知任务；任务以 20 ms 上限等待 TXE/BSY 后释放 CS。I2C DMA 等待上限为 80 ms。

错误路径同步停用所选外设和 DMA、清除遗留 IRQ，并释放 SPI CS；控制器使 shadow 失效，以 50 ms 起步、翻倍至 2000 ms 的退避重建总线和控制器，再恢复亮度/开关缓存并重发完整画面。SPI 的 SPE、方向和 SCK 空闲状态都在 CS 高时建立，避免 F1 使能切换造成的额外选中时钟边沿。UART 同样使用有界信号量等待，源缓冲位于发送任务栈并存活到 DMA 停止。ADC 只在调度器前执行一次有界采样；蜂鸣器由 TIM2 请求 DMA 交替写 BSRR，不运行音频频率 ISR。

USART 不直接嵌入 InputService。InputService 作为按键事件发布者，UART 诊断实现固定容量 Observer 接口，并把收到的事件投递到自己的 Producer–Consumer 队列。订阅回调不得阻塞或格式化。这样后续可添加记录器或测试观察者，而不会让输入层依赖具体输出设备。

输入仍以高于 UI 的优先级每 5 ms 采样；UI 目标周期为 SPI 5 ms / I2C 33 ms。两个任务都从本次实际开始时间安排下一次唤醒，丢弃过期节拍并在超时后至少阻塞 1 tick。动画单独按 8 ms 固定步推进，最多补 8 步，从而使高速 SPI 和故障恢复后的慢帧都不会改变正常动画速度或造成无限追帧。

没有为按键、RTC、LED 或当前未使用的 UART RX 强行配置 DMA。DMA 只用于可观的连续数据搬运或高频波形；低频寄存器访问继续使用直接读写。
