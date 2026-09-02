# 硬件、DMA 与引脚

引脚映射由旧版 `STM-GameBox_Handheld` 固件、当前工程和 PCB 行为恢复，并同步到根目录 `.ioc`。目标器件为 LQFP48 封装的 STM32F103C8T6，固件按官方容量 64 KiB Flash / 20 KiB SRAM 约束构建。

## 引脚分配

| 功能 | MCU 引脚 | 配置 | 备注 |
|---|---|---|---|
| OLED SCL | PB8 | I2C1 remap, AF open-drain | 400 kHz |
| OLED SDA | PB9 | I2C1 remap, AF open-drain | SSD1306 地址 0x3C |
| Up | PB7 | input pull-up | 低电平按下 |
| Down | PB5 | input pull-up | 低电平按下 |
| Left | PB6 | input pull-up | 低电平按下 |
| Right | PB4 | input pull-up | JTAG 关闭、SWD 保留后释放 PB4 |
| Jump | PB12 | input pull-up | 低电平按下 |
| Func | PB13 | input pull-up | 低电平按下 |
| Enter | PB14 | input pull-up | 低电平按下 |
| Back | PB15 | input pull-up | 低电平按下 |
| LED 1..4 | PB0/PB1/PB10/PB11 | push-pull output | 启动低电平；故障码输出 |
| Passive buzzer | PA12 | push-pull output | TIM3 update + DMA 写 GPIOA BSRR |
| Entropy ADC | PA1 / ADC1_IN1 | analog, 55.5 cycles | 与 UID、启动时刻混合为游戏随机种子 |
| UART TX/RX | PA9/PA10 | USART1 async, 115200 8N1 | 当前只发送诊断；TX 使用 DMA |
| HSE | PD0/PD1 | 8 MHz crystal | PLL ×9 = 72 MHz |
| LSE | PC14/PC15 | 32.768 kHz crystal | RTC 与 backup domain |
| SWD | PA13/PA14 | SWDIO/SWCLK | 保留；JTAG 被禁用 |

当前未分配 PA0、PA2–PA8、PA11、PA15、PB3、PC13。PB2 通常参与 BOOT1 配置，不把它列为无条件可用 GPIO。引出新功能前必须再与实际 PCB 原理图核对。

## 时钟和定时器

- SYSCLK/HCLK：72 MHz；
- APB1：36 MHz，TIM2–4 定时器时钟 72 MHz；
- APB2：72 MHz；
- ADC：PCLK2 / 6 = 12 MHz；
- I2C1：400 kHz，standard duty 2；
- USART1：115200 baud，8 data bits，no parity，1 stop bit；
- TIM4：HAL 1 ms timebase；
- SysTick：FreeRTOS 1 ms tick；
- TIM3：预分频 71，得到 1 MHz 计数器；ARR 按音调半周期设置。

SysTick、PendSV、SVC 归 FreeRTOS Cortex-M3 port 所有。TIM4 只推进 HAL tick。两套时基都不占用外部引脚。

## DMA 分配

STM32F103 的 DMA 请求与通道固定绑定，因此选择 DMA 前必须先做通道冲突表。

| DMA 通道 | 请求 | 方向/宽度 | 模式 | 优先级 | 完成中断 |
|---|---|---|---|---|---|
| DMA1 Channel 1 | ADC1 | peripheral→memory，halfword | Normal | Low | 开启，NVIC 6 |
| DMA1 Channel 3 | TIM3_UP | memory→peripheral，word | Circular | Very high | 关闭 |
| DMA1 Channel 4 | USART1_TX | memory→peripheral，byte | Normal | Medium | 开启，NVIC 6 |
| DMA1 Channel 6 | I2C1_TX | memory→peripheral，byte | Normal | High | 开启，NVIC 6 |

I2C1 EV/ER 和 USART1 全局中断也设为 NVIC 6。该值在数值上不高于 FreeRTOS 允许调用 `xSemaphoreGiveFromISR` 的最高紧迫度，因而 DMA 完成回调可以安全唤醒任务。

TIM3_UP 的循环 DMA 在两个 32-bit 字之间轮转：一个写 PA12 的 BSRR set 位，一个写对应 reset 位。TIM3 每个半周期产生一次 DMA 请求，所以 PA12 形成方波。它不需要 DMA 完成中断，避免旧实现每个音调周期两次进入 CPU。

ADC DMA 在创建任何 FreeRTOS 对象前收集 16 个样本。它有 5 ms 超时和失败回退；样本与三段 UID 和启动 tick 混合。该顺序很重要：Cortex-M3 FreeRTOS port 可能在调度器启动前保持内核临界区中断屏蔽。该输入只能增加游戏种子的变化性，不是密码学随机数源。

USART1 TX 由独立低优先级任务拥有。事件先进入固定队列，任务在自己的 64-byte 栈缓冲中格式化，再启动 Normal DMA 并睡眠等待完成信号量。DMA 生命周期结束前缓冲始终有效。

## OLED 刷新协议

画布采用 SSD1306 原生 page layout：8 页 × 128 字节。每帧比较 Canvas 与最后成功发送的 shadow，只处理变化页。每页先发送页/列地址命令，再发送 `0x40 + 128 bytes` 数据包。

初始化发生在任何 FreeRTOS 队列、信号量或任务创建前，因此少量 SSD1306 初始化命令使用有界的 blocking HAL 调用。完成首帧后才创建显示 DMA 信号量。调度器运行后，所有 OLED 数据和设置命令均改用 I2C TX DMA；UI 任务等待二值信号量而不是忙等。失败时不清 dirty 状态，下一帧会重试，System 页同时累计错误、超时和 DMA 传输统计。

全屏在 400 kHz I2C 上约需 24 ms，低于 33 ms UI 帧周期。静态画面通常是零传输。

## 为什么其余功能不用 DMA

- 八个按键都在 GPIOB；一次读取 IDR 即得到一致快照。STM32F1 没有适合该用途的 GPIO DMA 请求；
- RTC、backup register 和 LED 都是低频、少量寄存器访问，引入 DMA 状态机得不偿失；
- USART1 RX 尚无命令协议。为不存在的消费者常驻 circular buffer 会浪费 SRAM；后续增加 CLI 时再实现 RX circular DMA + IDLE line；
- SSD1306 每页需要独立控制前缀，且差分页数不定。当前逐页 DMA保留了错误重试边界，也避免额外 1 KiB 拼包缓冲。

## RTC 日期持久化

STM32F1 RTC 硬件只保存秒计数器，HAL 日期结构位于 SRAM。固件用 DR4–DR6 保存带校验的日期锚点；启动时先读取原始 RTC 计数器，把累计整天合并到锚点，再交给 HAL。DR1–DR3 保存用户设置。冷启动且无有效备份记录时，日期基线为 2026-01-01，可在 Clock 页校准。

## 真机验收重点

1. 上电后 OLED 出现 GAMEBOX 主页，USART1 TX 输出 DMA READY；
2. System 页的 OLED DMA TX 随页面变化增加，静态停留时基本不增加；
3. 快速连续按键不会影响 5 ms 消抖，UART 丢弃数正常为 0；
4. 长音播放时 UI 和按键仍流畅，证明不再有音频频率 TIM3 ISR；
5. 依次运行六个游戏至少数分钟，检查栈余量、I2C/UART 错误和队列丢弃数；
6. 断电保留 backup domain 后重新上电，检查日期和设置。
