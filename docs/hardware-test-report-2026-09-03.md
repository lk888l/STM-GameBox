# 实机测试报告：2026-09-03

## 范围

- 目标：当前连接的 STM32F103C8T6 GameBox；
- 固件：refactor/rust-embassy 工作树的 Release ELF；
- 调试器：ST-Link V2-1，序列号 01290141524300143638414B；
- 工具：probe-rs 0.31.0，SWD 100 kHz；
- 目标文件：target/thumbv7m-none-eabi/release/gamebox-f103。

探针报告 VAPP 0.49–0.59 V。用户已明确说明这是该连接的已知误报；本报告只因为 SWD
识别、写入、verify、RTT 和寄存器读取均实际成功而继续，不能把该判断套用到其他硬件。

## 构建结果

- gamebox-core：39 项主机单元测试全部通过；
- font-subset：1 项主机测试通过；
- oled-driver：all-features 构建与 doctest 通过；
- gamebox-core 和 Cortex-M3 firmware：Clippy -D warnings 通过；
- Release 链接成功，最后两个 1 KiB Flash 页未包含在链接区。

~~~text
section/load Flash:
  vector + text + rodata = 56,636 B
  data load              =     80 B
  program Flash total    = 56,716 B / 63,488 B

static SRAM:
  data + bss + uninit    =  5,936 B / 20,480 B
~~~

程序区余 6,772 B；静态分配后余 14,544 B 供任务栈和运行期调用栈。

## 烧录与启动

probe-rs 的 connect-under-reset 下载因 NRST 附着时序超时，按工具提示去掉该选项后成功。
未使用 chip erase。最终 ELF 的写入和 verify 用时 29.53 s。

复位后 RTT 输出：

~~~text
[INFO ] SSD1306 ready: 1024 data bytes
~~~

这证明 Release 镜像已进入 main，并完成 72 MHz 时钟初始化、LSE/RTC 初始化路径、
OLED I²C1 remap、DMA 配置、SSD1306 初始化和 1,024 字节全帧发送。启动日志中没有 RTC
或 OLED warning。

## 运行期只读检查

复位并运行后，通过 ELF 符号地址读取原子运行计数：

~~~text
OLED_ERRORS          0
OLED_TRANSFERS     188
STORAGE_ERRORS       0
DROPPED_KEY_EVENTS   0
DROPPED_UART_EVENTS  0
~~~

OLED_TRANSFERS 持续增长是启动/转场变化区域数；错误和两路队列丢包均为零。

RTC backup register DR1 为 0x4742，与本固件 marker 一致。连续读取 RTC CNTH/CNTL：

~~~text
0x000010DE
0x000010E4
~~~

两次独立 SWD 读取之间计数增加 6，计数器按秒前进。多次软件复位后 marker 仍保留。

GPIOB 的 CRL/CRH/IDR/ODR 只读结果为：

~~~text
88884422 888822DD 0000F3F8 0000F0F0
~~~

PB4–PB7、PB12–PB15 对应配置半字节均为 0x8，ODR 对应位均为 1；八个按钮仍由
Embassy Input 句柄保持为上拉输入，当前未按下电平也均为高。

## UART 边界

Windows 枚举出 ST-Link Virtual COM Port COM15。监听 115200 8N1 后复位 MCU，5 s 内未
收到启动行。最可能原因是该探针的 VCP RX 没有接到目标 PA9；仅连接 SWDIO/SWCLK/GND
不会自动形成串口链路。

代码侧 USART1 TX 使用 DMA1 Channel 4，启动行和固定容量事件格式均已完成 ARM 编译；
但本次不能据此声称 PA9 的板级波形或外部串口内容已验收。应把独立 USB-UART RX 接 PA9
后按验收清单复测。

## 已自动覆盖

- 六款游戏入口都由菜单可达；
- 全部 16 个 View 都可在主机渲染且不 panic；
- 游戏基本运动、碰撞、容量和终局规则；
- 按键去抖、双击互斥、280 ms 边界、长按/重复、时间回绕和多键独立；
- 第一次短按后第二次长按不会吞掉第一次 Click；
- RTC 日期转换、闰年、范围边界和秒计数 round-trip；
- 设置 schema-v2 编解码、v1 迁移、CRC 损坏拒绝和 generation 回绕；
- OLED 初始化和运行计数的板级检查。

## 尚未由本次自动化代替

- 实体 OLED 的肉眼布局、方向、四档亮度和动画主观效果；
- 八个机械按键逐键 30 次、真实触点抖动及组合按键；
- 六款游戏在实体按键上的完整人工通关/失败流程；
- 蜂鸣器音量、频率、极性和长音期间交互；
- 外接 USB-UART 对 PA9 的端到端串口验证；
- 逻辑分析仪测量 I²C/PA12/PA9 波形；
- OLED 断线、SDA 卡死等故障注入；
- Flash 擦写过程中的受控掉电和长期耐久测试。

这些项目保留在 [实机验收清单](hardware-validation.md)，不以编译通过或 SWD 读数替代。
