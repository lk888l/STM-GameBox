# 实机测试报告：2026-09-02

## 范围与环境

- 目标：用户连接的 STM32F103C8T6 GameBox PCB。
- 调试器：ST-Link V2-1，`probe-rs 0.31.0`，SWD。
- 探针持续误报 VAPP 约 0.38–0.58 V；用户已确认这是该探针的已知误报。SWD 内存访问、
  擦写、校验和连续运行均正常。本结论不应推广到其他出现低电压警告的硬件。
- 固件：`thumbv7m-none-eabi` release，写入后由 probe-rs 完整 verify。

## 已通过项目

### 启动、时钟与引脚

- 连续执行 10 次调试复位，均进入相同的运行配置。
- `RCC_CFGR = 0x001D840A`：PLL 作为 SYSCLK、HSE ×9、AHB ÷1、APB1 ÷2、APB2 ÷1、
  ADC ÷6，即 72/36/72/12 MHz。
- `AFIO_MAPR = 0x02000002`：I²C1 重映射至 PB8/PB9；JTAG 关闭、SWD 保留。
- GPIOB 配置读回为 `CRL=0x88884444`、`CRH=0x888844DD`；八个按键全部为上拉输入，
  PB8/PB9 为复用开漏输出。空闲 `IDR=0x0000FFFB`，全部八键为未按下。

### OLED 与 DMA

- SSD1306 地址 `0x3c` 应答，启动整帧传输为 1,024 数据字节。
- I²C1 读回 `CCR=0x801E`、`TRISE=0x0B`，对应 PCLK1 36 MHz 下的 400 kHz 快速模式。
- DMA1 Channel 6 外设地址为 I²C1 DR；Channel 7 保持未用，因为当前 OLED 只写不读。
- 菜单静止 2 秒后清除的 Channel 6 标志保持为零；移动一项后出现
  `DMA_ISR & 0x00F00000 = 0x00500000`，证明静止画面不轮询刷新而状态变化使用 DMA。
- 从 MCU 前缓冲抓取 1,024 字节并按 SSD1306 页格式渲染，主页的中文标题、游戏/工具/设置、
  选择胶囊和滚动条均在 128×64 边界内。

### I²C 故障恢复

第一次门控 I²C1 时钟时发现 Embassy 0.6 的 STM32F1 异步事务可能永久等待中断，原来的
错误返回后重试逻辑无法介入。固件因此增加以下修复：

1. 每个 OLED 操作由独立的 80 ms Embassy Timer 截止；
2. 超时会丢弃 Future，使 DMA drop guard 取消传输；
3. 唯一板级 adapter 重新使能/复位 I²C1，执行 RM0008 SWRST 勘误规避并恢复时序；
4. 指数退避后重新初始化 SSD1306，并完整重发保留的前缓冲。

修复后重复通过两种故障注入：

- 运行期门控 I²C1 时钟：RTT 出现一次 `OLED transfer timed out`，随后出现
  `OLED link recovered and framebuffer resent`，积压按键事件继续被消费。
- 把 PB9 临时配置为开漏低电平模拟 SDA 卡死：`IDR` 从 `0xFFFB` 变为 `0xFDFB`；释放后
  恢复 `0xFFFB`，同样在一次超时后重连并重发成功。

### 八键和手势

利用输入上拉和空闲 TIM3 + DMA1 Channel 3 生成可重复的实物 GPIO 波形，覆盖所有引脚：

| 键 | 引脚 | 已验证事件 |
| --- | --- | --- |
| 上 | PB7 | Pressed、Released、LongPress、Repeat、Click |
| 下 | PB5 | Pressed、Released、Click |
| 左 | PB6 | Pressed、Released、Click |
| 右 | PB4 | Pressed、Released、Click；SWD 同时保持连接 |
| 跳跃 | PB12 | Pressed、Released、DoubleClick |
| 功能 | PB13 | Pressed、Released、Click，直达设置页 |
| 确认 | PB14 | Pressed、Released、Click，进入页面/启动应用 |
| 返回 | PB15 | Pressed、Released、Click，退出应用 |

- 双击波形为按下 80 ms、释放 80 ms、按下 80 ms、释放；RTT 只出现一次 DoubleClick，
  没有附带 Click。
- 长按在 650 ms 产生一次 LongPress，280 ms 后开始 Repeat，随后约每 95 ms 一次。
- PB7 与 PB4 同时拉低时，两键得到相同 `at_ms`，各自完整产生 Pressed/Released/Click。
- 全程没有 `button event queue drops`，即 `DROPPED_KEY_EVENTS = 0`。
- 实际走通“主页 → 游戏 → 贪吃蛇 → 方向控制 → 返回”和“功能键 → 设置 → 修改”。

### Flash 日志

- 首次设置提交写入 Page A：magic `GBX2`、schema 1、generation 1、CRC 有效；Page B 保持
  擦除态。
- 复位后从 Page A 恢复设置，再次修改写入 Page B generation 2，证明不是回退到默认值。
- 后续交替提交得到 Page A generation 3、Page B generation 4；旧页在新页验证前一直有效。
- 最终有效快照为 Page B generation 4，声音开启，其余设置为当前默认值。
- 程序区烧录没有越过 `0x0800_F800`；两页设置日志在固件更新后保留。

## 构建结果

- `gamebox-core`：26 项主机测试全部通过。
- `font-subset`：1 项测试通过。
- 主机 core、工具和 Cortex-M3 固件 Clippy 均为 `-D warnings` 通过。
- production `info` release：Flash 地址占用 60,712 / 63,488 字节，静态 SRAM
  5,124 / 20,480 字节。

## 仍需人工或专用仪器完成

- 肉眼确认实体 OLED 的方向、对比度、启动动画主观流畅度和不同批次面板表现。
- 实际机械按键每键至少 30 次，以及真实触点抖动、手感和误触统计。
- 耳听或示波器确认无源蜂鸣器音量、频率和板级极性；当前寄存器只确认 PA12 为 2 MHz
  推挽输出且空闲为低。
- 用逻辑分析仪测量 SCL 实际频率、上升沿和单次局部/整帧传输时间。
- 在 Flash 擦除、写入、回读三个阶段分别执行受控掉电，并做长期擦写台架测试。
- 断开并重新接入真实 OLED 模块的人工试验；本报告使用等效时钟门控和 SDA 卡死注入。
