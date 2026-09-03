# 实机验收清单

自动化与 SWD 检查结果见
[2026-09-03 实机测试报告](hardware-test-report-2026-09-03.md)。以下清单用于肉眼、听感、
机械按键和受控故障测试；编译或探针读数不能代替这些项目。

## 烧录前

- [ ] MCU 确认为 STM32F103C8T6，HSE 8 MHz，LSE 32.768 kHz。
- [ ] OLED 接 PB8/PB9，七位地址 0x3c，SCL/SDA 有外部上拉。
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

每个实体按键至少操作 30 次并记录误触/漏触：

- [ ] 轻按只产生一次 Click；Click 在 280 ms 双击窗口后确认。
- [ ] 两次释放间隔小于 280 ms 时只产生一次 DoubleClick，不附带 Click。
- [ ] 恰好达到 280 ms 时视为两个 Click。
- [ ] 持续约 650 ms 只产生一次 LongPress，110 ms 后开始 Repeat，之后约每 90 ms 一次。
- [ ] 第一次短按后立即长按，第一次 Click 不会被第二次 LongPress 吞掉。
- [ ] 同时按住两个键时，各自状态独立，HELD 位图正确。
- [ ] PB4 Right 正常，同时 PA13/PA14 的 SWD 仍可连接。
- [ ] 快速操作后 System 页的两路 DROP 计数仍为 0。

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

- [ ] 逻辑分析仪确认 I²C 约 400 kHz，显示数据经 DMA1 Channel 6 发送。
- [ ] 静态页面不持续发送数据；时钟文字或动画变化时只发送变化区域。
- [ ] 安全地制造一次 SDA 卡死或 I²C 时钟故障，80 ms 后进入恢复并最终重发完整前帧。
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

报告至少记录：固件 commit/diff、ELF text/data/bss、供电实测值、OLED 型号、I²C 波形、
每键误触统计、蜂鸣器听感或示波器波形，以及仍未执行的项目。
