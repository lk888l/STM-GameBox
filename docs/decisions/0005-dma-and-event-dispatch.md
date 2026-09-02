# ADR 0005：固定 DMA 映射与按键事件订阅

状态：Accepted

STM32F103 的 DMA 请求映射固定，且不同外设可能争用同一通道。固件在启用 DMA 前建立全局分配表：ADC1→DMA1 Channel 1、TIM3_UP→Channel 3、USART1_TX→Channel 4、I2C1_TX→Channel 6。四路无冲突；TIM3 使用无 IRQ 的 circular 模式，其余 Normal DMA 通过优先级 6 的 ISR 完成。

OLED 页面和 UART 文本的 DMA 源缓冲必须存活到回调结束。因此 OLED 使用服务成员页缓冲，UART 使用发送任务的栈缓冲且任务同步等待完成；两者都以静态二值信号量休眠、设置超时并在失败时 abort。ADC 只在调度器前执行一次有界采样。蜂鸣器由 TIM3 请求 DMA 交替写 BSRR，不再运行音频频率 ISR。

USART 不直接嵌入 InputService。InputService 作为按键事件发布者，UART 诊断实现固定容量 Observer 接口，并把收到的事件投递到自己的 Producer–Consumer 队列。订阅回调不得阻塞或格式化。这样后续可添加记录器或测试观察者，而不会让输入层依赖具体输出设备。

没有为按键、RTC、LED 或当前未使用的 UART RX 强行配置 DMA。DMA 只用于可观的连续数据搬运或高频波形；低频寄存器访问继续使用直接读写。
