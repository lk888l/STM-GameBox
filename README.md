# STM32 GameBox / Rust + Embassy

这是 STM32F103C8T6 GameBox 固件的 Rust + Embassy 实现。它以
refactor/modernize-cpp 分支为功能基准，但按 Rust 的所有权、纯状态机和异步硬件所有者
重新组织；不是对 C++/FreeRTOS 控制流的逐行翻译。

当前版本已具备相同的产品入口：

- 四卡片主页：Games、Tools、Clock、Settings；
- 六款游戏：Dino、Snake、Air Raid、Tetris、Pong 2P、Piano；
- 四项工具：Stopwatch、Countdown、Input Lab、System；
- RTC 时钟/日期显示与校时；
- Sound、Motion、Brightness、Home Header 和 About；
- 主页顶栏可在 Time、Date、Pet、Title 间切换；
- 设置与 Snake 最高分掉电保存；
- OLED 差分页刷新、I²C DMA、USART1 TX DMA、蜂鸣器反馈和运行状态计数。

## 操作

通用界面：

- 主页：方向键切换卡片，Enter 或 Jump 确认；
- 列表：Up/Down 选择，Enter 或 Jump 确认，Back 返回；
- Settings：Left/Right、Enter 或 Jump 修改当前设置；
- 非游戏页面长按 Func 打开 Input Lab，双击 Func 打开 Clock；
- 长按 Enter 显示当前菜单项说明；
- Clock：Enter/Jump 开始校时，Left/Right 选字段，Up/Down 调整，
  Enter/Jump 逐字段前进并保存，Back 或 Func 取消；
- Stopwatch：Enter/Jump 启停，Func 清零；
- Countdown：Up/Down 调分钟，Left/Right 调秒，Enter/Jump 启停，
  Func 恢复 05:00。

游戏：

| 游戏 | 操作 |
| --- | --- |
| Dino | Jump、Up 或 Enter 开始/跳跃；速度随分数提高 |
| Snake | 方向键转向；Enter 或 Jump 开始/重开 |
| Air Raid | Up/Down 连续移动，Jump 发射，Enter 开始/重开；三条生命、最多三发子弹 |
| Tetris | Left/Right 移动，Down 软降，Up 硬降，Jump/Func 左旋/右旋，Enter 开始/重开 |
| Pong 2P | 左玩家 Up/Down，右玩家 Jump/Func，Enter 开始/重开 |
| Piano | Up、Left、Right、Down、Jump、Func、Enter、Back 对应 C4–C5；长按 Back 退出 |

Click 要等 280 ms 双击窗口结束后才确认，因此 Click 与 DoubleClick 互斥；即时游戏操作
使用 Pressed/Repeat。输入任务每 5 ms 采样，参数为 20 ms 去抖、650 ms 长按、
长按后 110 ms 首次重复及 90 ms 重复周期。

## 硬件契约

| 功能 | 引脚/资源 | 配置 |
| --- | --- | --- |
| MCU | STM32F103C8T6 | 64 KiB Flash / 20 KiB SRAM |
| HSE | PD0 / PD1 | 8 MHz，PLL ×9，SYSCLK 72 MHz |
| LSE / RTC | PC14 / PC15 | 32.768 kHz；RTC 计数器保存 2000–2099 日期时间 |
| OLED SCL / SDA | PB8 / PB9 | I²C1 remap，400 kHz，DMA1 CH6/CH7，地址 0x3c |
| Up / Down / Left / Right | PB7 / PB5 / PB6 / PB4 | 内部上拉，低电平按下 |
| Jump / Func / Enter / Back | PB12 / PB13 / PB14 / PB15 | 内部上拉，低电平按下 |
| USART1 TX | PA9 | 115200 8N1，DMA1 CH4 |
| 蜂鸣器 | PA12 | 异步任务产生方波，普通反馈为 1350 Hz / 12 ms |
| 随机熵 | PA1 / ADC1_IN1 | 启动时 16 次采样 |
| LED 1..4 | PB0 / PB1 / PB10 / PB11 | 推挽输出，正常运行保持低电平 |
| SWD | PA13 / PA14 | 保留 SWD，仅关闭 JTAG 以释放 PB4 |
| 设置日志 A / B | 0x0800_F800 / 0x0800_FC00 | 各 1 KiB，程序链接区不可见 |

PB8/PB9 必须有外部上拉。旧板没有经过确认的电池分压参数，因此本固件不显示虚构的电池
电压。

与 C++ 实现相比，DMA 的具体分配不是兼容性接口：Embassy 版保留 OLED 和 USART TX
DMA，ADC 启动采样及蜂鸣器由异步驱动完成。用户可见功能、按键映射、时序和页面信息架构
保持一致。

## 架构

~~~text
GPIO ──5 ms──> ButtonBank ─┬─> UI queue ─> App ─> UiRenderer ─> OLED diff/DMA
                           └─> UART queue ────────────────> USART1 TX DMA

App effects ─┬─> Flash 双页日志任务
             ├─> RTC adapter
             └─> 蜂鸣器任务
~~~

- gamebox-core：无堆、无 Embassy 依赖的应用、游戏、日历、输入和渲染状态机；
- firmware：F103 时钟/引脚、RTC、Flash、异步任务和 composition root；
- crates/oled-driver：保留并纳入工作区的 SSD1306 缓冲驱动；
- tools/font-subset：旧中文字模提取工具，保留供后续本地化使用。

UI 每 33 ms 生成一帧原生 SSD1306 page-layout 数据；OLED 驱动比较前帧，只发送变化区域。
每个 OLED 操作有 80 ms 截止时间，失败后取消 DMA、复位 I²C1、指数退避重连并重发前帧。

设置采用两个 Flash 页 copy-on-write：新页完成擦除、写入、CRC 解码和回读比对后才成为
活动页。750 ms 静默窗口会合并连续修改。schema v2 可迁移本 Rust 分支原有的 schema v1
记录。

详细设计见 [架构说明](docs/architecture.md)。

## 构建与检查

安装固定工具链和 Cortex-M3 目标：

~~~powershell
rustup toolchain install 1.96.0 --profile minimal
rustup target add --toolchain 1.96.0 thumbv7m-none-eabi
~~~

质量门禁：

~~~powershell
cargo test --locked -p gamebox-core --target x86_64-pc-windows-msvc
cargo test --locked -p font-subset --target x86_64-pc-windows-msvc
cargo test --locked -p oled-driver --target x86_64-pc-windows-msvc --all-features

cargo clippy --locked -p gamebox-core --target x86_64-pc-windows-msvc --all-targets -- -D warnings
cargo clippy --locked -p gamebox-f103-firmware --bin gamebox-f103 -- -D warnings
cargo build --locked --release -p gamebox-f103-firmware --bin gamebox-f103
~~~

工作区默认目标是 thumbv7m-none-eabi；font-subset 是主机工具，因此不要直接用无目标覆盖的
cargo check --workspace。

当前 Release：

| 资源 | 使用量 | 边界/余量 |
| --- | ---: | ---: |
| 程序 Flash | 56,716 B | 63,488 B 链接区，余 6,772 B |
| 设置 Flash | 2,048 B | 独占最后两页 |
| 静态 SRAM | 5,936 B | 20,480 B 总量，余 14,544 B 给栈 |

Release 关闭全局整数溢出检查以满足 Cortex-M3 体积预算；所有依赖回绕语义的时间、代数和
计数路径均显式使用 wrapping、saturating 或 checked 运算，主机测试仍保留溢出检查。

## 烧录与日志

普通 ST-Link：

~~~powershell
probe-rs download --chip STM32F103C8Tx --protocol swd --speed 100 --verify --disable-progressbars target/thumbv7m-none-eabi/release/gamebox-f103
~~~

本板的 NRST 连接不适合 probe-rs 的 connect-under-reset 下载；普通 SWD 附着已完成写后
校验。若探针误报低电压，只能在芯片识别、写入和 verify 均成功且实际供电已确认时忽略。

查看 RTT：

~~~powershell
probe-rs run --chip STM32F103C8Tx --protocol swd --speed 100 --preverify --verify target/thumbv7m-none-eabi/release/gamebox-f103
~~~

USART1 启动字符串为：

~~~text
GAMEBOX FW2 UART-TX-DMA READY
~~~

随后每个事件格式为：

~~~text
BTN <ms> <key> <event> <held_ms>
~~~

最新实机结果见 [2026-09-03 测试报告](docs/hardware-test-report-2026-09-03.md)，剩余人工项目
见 [实机验收清单](docs/hardware-validation.md)。
