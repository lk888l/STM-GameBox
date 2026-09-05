# 硬件、DMA 与引脚

引脚映射由旧版 `STM-GameBox_Handheld` 固件、当前工程和 SPI 迁移约定恢复，并同步到根目录 `.ioc`。目标器件为 LQFP48 封装的 STM32F103C8T6，固件按官方容量 64 KiB Flash / 20 KiB SRAM 约束构建。默认构建使用四线 SPI OLED；旧 I2C 屏通过 `GAMEBOX_OLED_INTERFACE=I2C` 选择，两种模式只初始化各自需要的显示引脚。

## 引脚分配

| 功能 | MCU 引脚 | 配置 | 备注 |
|---|---|---|---|
| SPI OLED SCK | PA5 | SPI1 SCK, AF push-pull | mode 0，9 MHz；仅 SPI 构建 |
| SPI OLED MOSI | PA7 | SPI1 MOSI, AF push-pull | 单向发送；仅 SPI 构建 |
| SPI OLED DC | PA6 | push-pull output | 命令低、数据高；不是 MISO |
| SPI OLED CS | PA4 | push-pull output | 低有效，启动/出错时保持高 |
| SPI OLED RESET | PA8 | push-pull output | 低有效，启动/恢复时硬复位 |
| I2C OLED SCL | PB8 | I2C1 remap, AF open-drain | 400 kHz；仅 I2C 构建 |
| I2C OLED SDA | PB9 | I2C1 remap, AF open-drain | SSD1306 地址 0x3C；仅 I2C 构建 |
| Up | PB7 | input pull-up | 低电平按下 |
| Down | PB5 | input pull-up | 低电平按下 |
| Left | PB6 | input pull-up | 低电平按下 |
| Right | PB4 | input pull-up | JTAG 关闭、SWD 保留后释放 PB4 |
| Jump | PB12 | input pull-up | 低电平按下 |
| Func | PB13 | input pull-up | 低电平按下 |
| Enter | PB14 | input pull-up | 低电平按下 |
| Back | PB15 | input pull-up | 低电平按下 |
| LED 1..4 | PB0/PB1/PB10/PB11 | push-pull output | 启动低电平；故障码输出 |
| Passive buzzer | PA12 | push-pull output | TIM2 update + DMA 写 GPIOA BSRR |
| Entropy ADC | PA1 / ADC1_IN1 | analog, 55.5 cycles | 与 UID、启动时刻混合为游戏随机种子 |
| UART TX/RX | PA9/PA10 | USART1 async, 115200 8N1 | 当前只发送诊断；TX 使用 DMA |
| HSE | PD0/PD1 | 8 MHz crystal | PLL ×9 = 72 MHz |
| LSE | PC14/PC15 | 32.768 kHz crystal | RTC 与 backup domain |
| SWD | PA13/PA14 | SWDIO/SWCLK | 保留；JTAG 被禁用 |

两种构建均未分配 PA0、PA2、PA3、PA11、PA15、PB3、PC13；SPI 构建不使用 PB8/PB9，I2C 构建不使用 PA4–PA8。熵 ADC 继续使用 PA1/ADC1_IN1，不与 SPI 的 PA7 MOSI 重叠。PB2 通常参与 BOOT1 配置，不把它列为无条件可用 GPIO。引出新功能前必须再与实际 PCB 原理图核对。

## 时钟和定时器

- SYSCLK/HCLK：72 MHz；
- APB1：36 MHz，TIM2–4 定时器时钟 72 MHz；
- APB2：72 MHz；
- ADC：PCLK2 / 6 = 12 MHz；
- SPI1：PCLK2 / 8 = 9 MHz，8-bit、MSB first、CPOL=0/CPHA=0、软件 NSS、one-line TX；
- I2C1：400 kHz，standard duty 2；
- USART1：115200 baud，8 data bits，no parity，1 stop bit；
- TIM4：HAL 1 ms timebase；
- SysTick：FreeRTOS 1 ms tick；
- TIM2：预分频 71，得到 1 MHz 计数器；ARR 按音调半周期设置。

SysTick、PendSV、SVC 归 FreeRTOS Cortex-M3 port 所有。TIM4 只推进 HAL tick。两套时基都不占用外部引脚。

## DMA 分配

STM32F103 的 DMA 请求与通道固定绑定，因此选择 DMA 前必须先做通道冲突表。

| DMA 通道 | 请求 | 方向/宽度 | 模式 | 优先级 | 完成中断 |
|---|---|---|---|---|---|
| DMA1 Channel 1 | ADC1 | peripheral→memory，halfword | Normal | Low | 开启，NVIC 6 |
| DMA1 Channel 2 | TIM2_UP | memory→peripheral，word | Circular | Very high | 关闭 |
| DMA1 Channel 3 | SPI1_TX（SPI 构建） | memory→peripheral，byte | Normal | High | 开启，NVIC 5 |
| DMA1 Channel 4 | USART1_TX | memory→peripheral，byte | Normal | Medium | 开启，NVIC 6 |
| DMA1 Channel 6 | I2C1_TX（I2C 构建） | memory→peripheral，byte | Normal | High | 开启，NVIC 6 |

SPI1 全局中断设为 NVIC 5，I2C1 EV/ER 和 USART1 全局中断为 NVIC 6。这些值满足 FreeRTOS `xSemaphoreGiveFromISR` 的优先级限制。默认 SPI 构建只启用 Channel 3 的显示 IRQ，I2C 构建只启用 Channel 6 的显示 IRQ。

蜂鸣器从原来的 TIM3_UP/Channel 3 迁到 TIM2_UP/Channel 2，为固定映射的 SPI1_TX 腾出 Channel 3。循环 DMA 在两个 32-bit 字之间轮转：一个写 PA12 的 BSRR set 位，一个写对应 reset 位。TIM2 每个半周期产生一次 DMA 请求，所以 PA12 形成方波。它不需要 DMA 完成中断，播放音调不产生逐边沿 CPU 中断。

ADC DMA 在创建任何 FreeRTOS 对象前收集 16 个样本。它有 5 ms 超时和失败回退；样本与三段 UID 和启动 tick 混合。该顺序很重要：Cortex-M3 FreeRTOS port 可能在调度器启动前保持内核临界区中断屏蔽。该输入只能增加游戏种子的变化性，不是密码学随机数源。

USART1 TX 由独立低优先级任务拥有。事件先进入固定队列，任务在自己的 64-byte 栈缓冲中格式化，再启动 Normal DMA 并睡眠等待完成信号量。DMA 生命周期结束前缓冲始终有效。

## OLED 刷新协议

画布采用 SSD1306 原生 page layout：8 页 × 128 字节。每帧比较 Canvas 与最后成功发送的 shadow；画布未变则不启动任何传输。`Ssd1306` 管理控制器状态和刷新策略，`OledTransport` 管理选定的 SPI/I2C、GPIO、DMA 与完成信号量。

- SPI：控制器使用水平寻址，变化帧发送列/页范围命令，把 Canvas 拷入控制器拥有的 shadow，再由 DMA 发送连续 1024 B；成功前该 shadow 保持无效。DC 区分命令/数据，不添加 I2C 控制前缀。
- I2C：控制器使用页寻址，仅对变化页发送页/列地址，再由成员缓冲发送 `0x40 + 128 bytes`。命令的控制前缀为 `0x00`，地址保持 0x3C。

硬件初始化和首帧尝试发生在任何 FreeRTOS 队列、信号量或任务创建前，使用有界 blocking 调用；第二阶段才创建静态 DMA 完成信号量。屏幕未接或传输失败不会阻止其余任务启动，显示进入离线退避并在后续帧重试。运行期数据和设置命令均走 TX DMA，UI 任务睡眠等待二值信号量。SPI 单次操作超时 20 ms，I2C 为 80 ms。

SPI 的 DMA 完成只表示末字节进入 DR，不等于末位已送出。DMA ISR 只关闭 TX DMA 请求并唤醒 UI，任务在 TXE=1/BSY=0 后才释放 CS。BSY 等待使用可推进的 TIM4 tick 并在任务中阻塞；不调用会在高优先级 ISR 内轮询 BSY 的 HAL SPI 默认 DMA 完成路径。

STM32F1 切换 SPE 可能产生 SCK 边沿。启动时先把 PA5 作为 GPIO 保持低，完成 SPI 配置后才切到复用；每次命令、数据和恢复操作都先保持 CS 高，设置发送方向及使能状态，再置 DC 并拉低 CS。复位顺序为 RESET 高 1 ms、低 10 ms、再高 10 ms。

错误返回前同步停用所选 DMA 和总线、释放 SPI CS，并清除挂起中断。控制器使 shadow 失效，进入 50 ms 起步、翻倍至 2000 ms 的恢复退避；到期重建总线、复位/重初始化控制器、恢复缓存的亮度和开关状态、重发完整画布。失败后 staging shadow 即使已改变也不会被当成成功帧用于跳过刷新，恢复期间输入、游戏和音频任务继续运行。I2C 不调用 STM32F1 HAL 中存在回调上下文与 RX DMA 句柄竞态的 `HAL_I2C_Master_Abort_IT`。

SPI 的 1024 B 纯数据线速时间约 0.91 ms，目标 UI 周期为 5 ms；400 kHz I2C 全屏含页协议约 24 ms，目标周期为 33 ms。这些是线速估算和调度目标，不是本次真机测量。绘图、系统负载和总线恢复可拉长一帧；UI 会丢弃过期节拍并至少阻塞 1 tick。System 页的 OLED DMA TX 统计底层 DMA 次数，不能直接当作帧数。

## 为什么其余功能不用 DMA

- 八个按键都在 GPIOB；一次读取 IDR 即得到一致快照。STM32F1 没有适合该用途的 GPIO DMA 请求；
- RTC、backup register 和 LED 都是低频、少量寄存器访问，引入 DMA 状态机得不偿失；
- USART1 RX 尚无命令协议。为不存在的消费者常驻 circular buffer 会浪费 SRAM；后续增加 CLI 时再实现 RX circular DMA + IDLE line；
- I2C SSD1306 每页需要独立控制前缀，且差分页数不定。逐页 DMA 保留重试边界并避免额外 1 KiB 拼包缓冲；SPI 无需控制前缀，复用完整 shadow 作为 DMA 源。

## RTC 日期持久化

STM32F1 RTC 硬件只保存秒计数器，HAL 日期结构位于 SRAM。固件用 DR4–DR6 保存带校验的日期锚点；启动时先读取原始 RTC 计数器，把累计整天合并到锚点，再交给 HAL。DR1–DR3 保存声音、动画、亮度和主页顶栏模式；旧记录未使用的顶栏位为零，因此升级后自然选择默认的 Time 模式。冷启动且无有效备份记录时，日期基线为 2026-01-01，可在 Clock 页校准。

## 真机验收重点

1. 上电后 OLED 出现 GAMEBOX 主页，USART1 TX 输出 DMA READY；
2. System 页的 OLED DMA TX 随页面变化增加，静态停留时基本不增加；
3. 快速连续按键时仍保持 5 ms 采样与 20 ms 稳定消抖，UART 丢弃数正常为 0；
4. 长音播放时 UI 和按键仍流畅，确认 TIM2/Channel 2 与 SPI1/Channel 3 不冲突；
5. 依次运行六个游戏至少数分钟，检查栈余量、OLED/UART 错误和队列丢弃数；
6. 断电保留 backup domain 后重新上电，检查日期和设置。

迁移后的两种接口都需要分别完成以上验收。另检查 `sampleCount()` 持续增长、`maximumScanGapMs()` 与 `maximumPressAgeMs()`，以及 OLED 故障后的有限重试和恢复完整帧；本轮代码迁移没有执行烧录、引脚注入或真机验收。
