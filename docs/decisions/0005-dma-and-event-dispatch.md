# ADR 0005：固定 DMA 映射与按键事件订阅

状态：Accepted

STM32F103 的 DMA 请求映射固定，且不同外设可能争用同一通道。固件在启用 DMA 前建立全局分配表：ADC1→DMA1 Channel 1、TIM3_UP→Channel 3、USART1_TX→Channel 4、I2C1_TX→Channel 6。四路无冲突；TIM3 使用无 IRQ 的 circular 模式，其余 Normal DMA 通过优先级 6 的 ISR 完成。

OLED 页面和命令的 DMA 源缓冲必须存活到传输确认终止。因此 OLED 统一使用服务成员缓冲，并用原子活动标志阻止缓冲在 DMA 结束前被覆盖。STM32F1 HAL 的中止 API 会在调用任务中同步执行名为 IT 的回调，且 TX 完成竞态可能误选未配置的 RX DMA 句柄；OLED 超时路径因此不调用该 API，而是同步停用并重建仅 I2C1 与 DMA1 Channel 6，清除遗留 IRQ 后再开放缓冲，下一帧完整修复显示。正常完成和错误回调仍只从 ISR 使用 FreeRTOS ISR API。UART 使用发送任务的栈缓冲且任务同步等待完成；两者都以静态二值信号量休眠并设置超时。ADC 只在调度器前执行一次有界采样。蜂鸣器由 TIM3 请求 DMA 交替写 BSRR，不再运行音频频率 ISR。

USART 不直接嵌入 InputService。InputService 作为按键事件发布者，UART 诊断实现固定容量 Observer 接口，并把收到的事件投递到自己的 Producer–Consumer 队列。订阅回调不得阻塞或格式化。这样后续可添加记录器或测试观察者，而不会让输入层依赖具体输出设备。

没有为按键、RTC、LED 或当前未使用的 UART RX 强行配置 DMA。DMA 只用于可观的连续数据搬运或高频波形；低频寄存器访问继续使用直接读写。
