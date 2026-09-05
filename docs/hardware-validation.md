# 实机验收清单

以下清单用于肉眼、听感、机械按键和受控故障测试；编译或探针读数不能代替这些项目。
构建和产物校验使用 [README 中的 Cargo 命令](../README.md#构建与检查)。

## 烧录前

- [ ] 根据模块选择 `cargo build-spi` 或 `cargo build-i2c`，使用文件名带对应接口的发布文件，
  核对同名 JSON 中的 SHA-256；不可混用旧的无接口名称产物。
- [ ] MCU 确认为 STM32F103C8T6，HSE 8 MHz，LSE 32.768 kHz。
- [ ] BOOT0 为低，正常复位后执行用户 Flash；多探针时按序列号锁定 GameBox 的 ST-Link。
- [ ] SPI 版：PA5=SCLK、PA7=SDIN、PA6=D/C#、PA4=CS#、PA8=RES#；不接 MISO。
- [ ] I²C 版：PB8=SCL、PB9=SDA，地址 `0x3c`，外部上拉可靠；PA4～PA8 不接显示信号。
- [ ] OLED 逻辑电源与接口电平符合所用模块规格；不要把 SPI 模块的 SCL/SDA 标注误认为 I²C。
- [ ] PA12 接无源蜂鸣器；有源蜂鸣器需要替换音频策略。
- [ ] 若要保留现有设置，烧录工具不得执行 chip erase；先备份最后两个 Flash 页。
- [ ] 独立确认 3.3 V 和 GND。只有供电正常且识别、写入、校验都成功时，才可按已知情况
  忽略 ST-Link 的 VAPP 误报。

## 启动与主页

- [ ] 冷启动 20 次均显示 GameBox 启动画面，约 700 ms 后进入主页，无花屏。
- [ ] 启动期间按任意键可跳过，且该键释放后不会误触主页。
- [ ] 主页四张卡片依次为 Games、Tools、Clock、Settings，方向键可双向循环。
- [ ] Home Header 的 Time、Date、Pet、Title 四种模式显示正确。
- [ ] Full、Reduced、Off 三档动画均无残影，Off 立即吸附到目标。
- [ ] Low、Med、High、Max 四档亮度均能立即改变 OLED 对比度。

## 八键与手势

可先执行真实 GPIO→去抖→UI 链路的自动回归：

~~~sh
cargo xtask buttons --probe 01380173524300183638414B
~~~

此可选命令需要 PATH 中的 OpenOCD。默认 ELF 为 `artifacts/firmware/gamebox-f103-spi.elf`，
其他版本用 `--elf <path>` 指定，且必须已烧录到板上；命令本身不烧录。可先加 `--dry-run`
验证 ELF 及生成 Tcl，这时不连接探针，也不需要安装 OpenOCD。

工具核对板内固件与 ELF 一致，然后每键 30 次切换内部上/下拉，检查按下与松开边沿、
事件数量、扫描间隔、处理延迟和错误计数。过程中不要按实体键；引脚始终为输入，结束
或断言失败后复位恢复上拉与正常应用。Tcl、OpenOCD 日志和 JSON 报告在
`target/button-regression/`，可用 `--output-dir` 修改。默认要求扫描
最大间隔不超过 20 ms、单键 Pressed 送达不超过 20 ms；这项软件链路测试不替代机械触点测试。

每个实体按键至少操作 30 次并记录误触/漏触：

- [ ] 轻按只产生一次 Click；Click 在 280 ms 双击窗口后确认。
- [ ] 两次释放间隔小于 280 ms 时只产生一次 DoubleClick，不附带 Click。
- [ ] 恰好达到 280 ms 时视为两个 Click。
- [ ] 持续约 650 ms 只产生一次 LongPress，110 ms 后开始 Repeat，之后约每 90 ms 一次。
- [ ] 第一次短按后立即长按，第一次 Click 不会被第二次 LongPress 吞掉。
- [ ] 同时按住两个键时，各自状态独立，HELD 位图正确。
- [ ] PB4 Right 正常，同时 PA13/PA14 的 SWD 仍可连接。
- [ ] 快速操作后 System 页的两路 DROP 计数仍为 0。
- [ ] 在 Clock 等静态页面停留后，短按、松开仍及时识别，不能只在画面变化时响应。

## 页面与快捷键

- [ ] Games 列表能进入全部六款游戏，Back 均可返回。
- [ ] Tools 能进入 Stopwatch、Countdown、Input Lab、System。
- [ ] Settings 五项均可访问，修改后 Toast 文案和值一致。
- [ ] 非游戏页长按 Func 打开 Input Lab，双击 Func 打开 Clock。
- [ ] Clock 可编辑 hour、minute、year、month、day；跨月、闰年和日期截断正确。
- [ ] 复位和断电/VBAT 场景下 RTC 行为符合板上供电设计。
- [ ] Stopwatch 启停/清零正确；Countdown 可调、暂停、继续、归零并响 880 Hz 提示音。

## 游戏

- [ ] Dino 可开始、跳跃、碰撞结束、重开，随得分加速。
- [ ] Snake 禁止直接反向，食物不在身体内，撞墙/自身结束，最高分复位后保留。
- [ ] Air Raid 可连续上下移动，最多三发子弹，三条生命耗尽后结束。
- [ ] Tetris 七种方块可移动、软降、硬降、双向旋转、消行计分、堆顶结束。
- [ ] Pong 两名玩家可同时移动，漏球后球拍缩短，耗尽后显示胜者。
- [ ] Piano 八键对应 C4–C5；短按 Back 发 C5，长按 Back 退出且不误发音符。
- [ ] 所有音效播放时按键、游戏 tick 和 OLED 动画仍持续响应。

## OLED、UART 与故障恢复

先核对 RTT 启动日志中的接口名称及频率，再按实际后端执行对应项。不能只凭 DMA 成功或
零错误计数认定屏幕正常，尤其 SPI 没有 ACK。切换模块前断电并烧录匹配固件。

SPI 专用项：

- [ ] 逻辑分析仪确认 SPI Mode 0、MSB first、SCLK 约 9 MHz，显示数据经 DMA1 Channel 3
  发送，D/C# 在命令/数据间正确切换。
- [ ] 上电时 PA8 产生高-低-高硬复位序列，CS# 仅在完整 SPI payload 期间为低。
- [ ] 首次发送及恢复发送前，CS# 为高时已令 SPI1 `SPE=0`，避免首次禁用 SPI 在
  CS# 有效期间产生多余的 SCLK 上升沿。
- [ ] 任一变化帧只发一个全屏窗口命令和一个连续 1,024 字节
  数据事务。
- [ ] 安全地门控 SPI1 或 DMA1 Channel 3 制造超时；20 ms 后 CS# 已释放，恢复链路最终
  硬复位 OLED 并重发完整前帧。

I²C 专用项：

- [ ] 逻辑分析仪确认 PB8/PB9 约 400 kHz、地址 `0x3c`；命令前缀 `0x00`、数据前缀 `0x40`。
- [ ] TX 使用 DMA1 CH6，CH7 为 HAL 所需 RX 资源；SPI DMA1 CH3 未绑定。
- [ ] 局部变化仅发送对应脏区；全屏发送约需 23 ms 以上，不被 SPI 的 20 ms 截止时间误取消。
- [ ] 没有硬复位信号的四针模块正常上电初始化，PA8 始终不由显示后端操作。
- [ ] 受控通信故障触发有界超时（外层 80 ms），恢复 I²C1 后重初始化并重发完整前帧。

共用项：

- [ ] 连续动画的生产者调度目标为 5 ms；分别记录实际 flush 速率、可见帧率和肉眼效果，
  不把 I²C 全屏传输或 SSD1306 扫描直接写成 200 FPS。
- [ ] 静态页面不持续发送数据；两版 `OLED TX` 都按成功的非空刷新次数计数，而非脏区数。
- [ ] PA9 使用外接 USB-UART 时收到 GAMEBOX FW2 UART-TX-DMA READY。
- [ ] 每个按键事件串口格式为 BTN <ms> <key> <event> <held_ms>。
- [ ] System 页 OLED ERR、UART ERR、OLED TX 和 Flash 错误计数符合注入结果。

## Flash 与掉电

- [ ] 连续修改设置只在停止约 750 ms 后写一次。
- [ ] Sound、Motion、Brightness、Home Header 和 Snake 最高分复位后恢复。
- [ ] Page A/B 在连续保存时交替，generation 递增，CRC 有效。
- [ ] 在擦除、写入和回读期间分别受控掉电，重启后至少有一个有效快照。
- [ ] 长期擦写没有越过 0x0800_F800..0x0801_0000。

## 验收记录

报告至少记录：固件 commit/diff、接口 feature、发布文件 SHA-256、ELF text/data/bss、
供电实测值、OLED 型号、SPI / I²C 波形、
每键误触统计、蜂鸣器听感或示波器波形，以及仍未执行的项目。
