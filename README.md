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
- 编译期可选 OLED SPI1 TX DMA / I²C1 DMA、变化检测、USART1 TX DMA、蜂鸣器反馈和运行状态计数。

## 操作

通用界面：

- 主页：方向键切换卡片，Enter 或 Jump 确认；
- 列表：Up/Down 选择，Enter 或 Jump 确认，Back 返回；
- Settings：Left/Right、Enter 或 Jump 修改当前设置；
- 菜单页单击 Func 显示当前项说明；非游戏页长按 Func 打开 Input Lab，双击 Func 打开 Clock；
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

所有菜单和功能页的 Enter/Jump 确认都在 20 ms 去抖后直接使用 Pressed；进行中的
卡片或页面动画不会锁住输入。同一次物理按键后续的 Released/Click 会被吞掉，
但新的一次消抖 Pressed 仍可立即执行。需要区分单击/双击的 Click 仍等待 280 ms 窗口；
即时游戏操作使用 Pressed/Repeat。输入任务每 5 ms 采样，参数为 20 ms 去抖、650 ms 长按、
长按后 110 ms 首次重复及 90 ms 重复周期。

## 硬件契约

| 功能 | 引脚/资源 | 配置 |
| --- | --- | --- |
| MCU | STM32F103C8T6 | 64 KiB Flash / 20 KiB SRAM |
| HSE | PD0 / PD1 | 8 MHz，PLL ×9，SYSCLK 72 MHz |
| LSE / RTC | PC14 / PC15 | 32.768 kHz；RTC 计数器保存 2000–2099 日期时间 |
| OLED SPI SCLK / SDIN | PA5 / PA7 | `oled-spi`：SPI1 Mode 0，9 MHz，TX DMA1 CH3 |
| OLED SPI CS# / D/C# / RES# | PA4 / PA6 / PA8 | `oled-spi`：GPIO 推挽，低有效 CS 与硬件复位 |
| OLED I²C SCL / SDA | PB8 / PB9 | `oled-i2c`：I²C1 重映射，400 kHz，地址 `0x3c`，TX/RX DMA1 CH6/7 |
| Up / Down / Left / Right | PB7 / PB5 / PB6 / PB4 | 内部上拉，低电平按下 |
| Jump / Func / Enter / Back | PB12 / PB13 / PB14 / PB15 | 内部上拉，低电平按下 |
| USART1 TX | PA9 | 115200 8N1，DMA1 CH4 |
| 蜂鸣器 | PA12 | 异步任务产生方波，普通反馈为 1350 Hz / 12 ms |
| 随机熵 | PA1 / ADC1_IN1 | 启动时 16 次采样 |
| LED 1..4 | PB0 / PB1 / PB10 / PB11 | 推挽输出，正常运行保持低电平 |
| SWD | PA13 / PA14 | 保留 SWD，仅关闭 JTAG 以释放 PB4 |
| 设置日志 A / B | 0x0800_F800 / 0x0800_FC00 | 各 1 KiB，程序链接区不可见 |

同一 SSD1306 支持两种模块，编译时二选一，不自动探测，也不同时驱动两块屏幕：

- SPI 版：模块的 `SCL/D0/CLK` 接 PA5，`SDA/D1/DIN` 接 PA7；另接 CS#、D/C#、RES#。
  不需要 MISO 或 I²C 上拉，PB8/PB9 不被显示驱动占用。
- I²C 版：SCL 接 PB8、SDA 接 PB9，需外部上拉（模块可能已自带）；采用原板的 `0x3c`
  地址。四针模块没有 MCU 控制的 RES#，不占用 PA4～PA8，也不会为了复位而操作 PA8。

两个版本均不改变按键、UART、蜂鸣器或 SWD 分配。旧板没有经过确认的电池分压参数，
因此本固件不显示虚构的电池电压。

与 C++ 实现相比，DMA 的具体分配不是兼容性接口：仅选中的 OLED 后端占用对应 DMA，
USART TX 继续独占 DMA1 CH4；ADC 启动采样及蜂鸣器由异步驱动完成。用户可见功能、
按键映射和页面信息架构保持一致。

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
- firmware/src/platform/oled：统一显示接口，以及编译期选择的 SPI / I²C 板级后端；
- crates/oled-driver：保留并纳入工作区的 SSD1306 缓冲驱动；
- tools/xtask：跨平台构建、产物导出、检查及可选的硬件回归入口；
- tools/font-subset：旧中文字模提取工具，保留供后续本地化使用。

上层 UI / 游戏只使用统一的 `Oled` 接口，不关心总线、DMA、复位引脚或超时值。后端采用
静态绑定，不使用动态分发或堆分配；未选中的后端不编入固件。

UI 调度节拍为 5 ms（目标 200 FPS，并非实测可见帧率）；动画按实际经过时间推进。
两版都跳过完全相同的静态帧，但传输策略按带宽选择：

| 策略 | SPI（默认） | I²C |
| --- | --- | --- |
| 变化帧 | 单个连续 1,024 字节 DMA 数据事务 | 只发脏区，减少总线字节数 |
| 总线 / 全屏纯传输时间 | 9 MHz / 约 0.91 ms | 400 kHz / 约 23 ms，另有命令开销 |
| 控制器扫描时钟 | `D5 F0`，最高振荡器设置 | `D5 80`，保留旧 I²C 模块默认设置 |
| 单次总线操作截止时间 | 20 ms | 80 ms，避免误取消正常全屏传输 |
| 恢复 | 复位 SPI1 + PA8 硬复位面板 | 复位 I²C1 + 重发控制器初始化 |

实际帧率受渲染耗时、总线带宽及面板内部扫描共同限制；I²C 全屏传输不能达到 200 FPS。
失败后取消 DMA、指数退避重连并重发前帧；SPI 同时确保释放 CS#。面板硬复位等待独立于
总线操作截止时间。

UI 每轮都显式让出执行器，画面相同、跳过 DMA 时也保证按键任务有机会运行。UI 和按键
均丢弃已经错过的周期，从当前时刻重新安排下一次 tick，避免渲染慢于 5 ms 时无限补赶。
矩形和 6×8 字体按 OLED 原生页字节绘制，减少同步渲染占用。按键回归方法见
[实机验收清单](docs/hardware-validation.md)。

SPI 首次传输及故障恢复必须从 `SPE=0` 开始，并在 CS# 为高时完成这项准备。本板在
SPI 禁用时 SCLK 会升高；若等到 CS# 拉低后才首次禁用 SPI，会破坏初始化命令的位对齐。

设置采用两个 Flash 页 copy-on-write：新页完成擦除、写入、CRC 解码和回读比对后才成为
活动页。750 ms 静默窗口会合并连续修改。schema v2 可迁移本 Rust 分支原有的 schema v1
记录。

详细设计见 [架构说明](docs/architecture.md)。

## 构建与检查

Windows、Linux、macOS 使用相同的 Cargo 命令。安装 Rustup 及当前系统的 Rust 主机链接器
后，在仓库根目录运行即可；`rust-toolchain.toml` 会让 Rustup 安装固定的 Rust 1.96.0、
Cortex-M3 目标、`llvm-tools-preview`、Clippy 和 rustfmt。Windows 主机工具使用 MSVC
Build Tools，Linux 使用系统 C 编译器/链接器，macOS 使用 Xcode Command Line Tools。

构建和导出由工作区内的 Rust 工具 `tools/xtask` 完成，直接使用当前 Rust 工具链附带的
`rust-lld` 和 LLVM 工具，不需要安装 PowerShell、Arm GNU binutils、Make 或 cargo-make。
首次使用会编译 xtask，后续复用 Cargo 缓存。

### 选择屏幕接口

以下命令一次完成编译与 ELF、BIN、HEX、SHA-256 清单导出：

~~~sh
cargo build-spi                         # SPI 发布版，当前设备使用这一项
cargo build-i2c                         # I2C 发布版
cargo build-all                         # 两种接口的发布版
cargo xtask build                       # SPI 调试版
cargo xtask build --display i2c         # I2C 调试版
cargo xtask build --display all         # 两种接口的调试版
cargo xtask --help                      # 查看全部命令
~~~

三个 `build-*` 别名分别对应 `cargo xtask build --display spi|i2c|all --release`。
SPI / I²C 默认使用独立缓存目录 `target/oled-spi` / `target/oled-i2c`，也可以设置
`--target-dir` 或 `CARGO_TARGET_DIR`。例如路径包含空格时：

~~~sh
cargo xtask build --display spi --release --target-dir "target/custom cache" --output-dir "artifacts/custom firmware"
~~~

相对目录按仓库根目录解析，也接受绝对路径。每次成功构建都会读取 Cargo 报告的本次 ELF
路径并重新导出，因此缓存命中时也能补回被删除的发布文件。

烧录产物按接口与 profile 区分，debug 不覆盖 release：

~~~text
artifacts/firmware/gamebox-f103-spi.{elf,bin,hex,json}        # release
artifacts/firmware/gamebox-f103-i2c.{elf,bin,hex,json}        # release
artifacts/firmware/debug/gamebox-f103-spi.{elf,bin,hex,json}  # debug
artifacts/firmware/debug/gamebox-f103-i2c.{elf,bin,hex,json}   # debug
~~~

`.elf` 可供调试器烧录；`.hex` 自带 Flash 地址；`.bin` 的基地址为 `0x08000000`。
`.json` 记录接口、profile、编译 feature、Flash/静态 SRAM 占用、文件大小和 SHA-256。
导出检查 ELF 加载地址、BIN/HEX 内容一致性及内存边界，确保不侵入最后两个设置页。
转换和校验先在临时目录完成，再发布文件，清单最后更新；失败时命令返回非零状态。
这些命令不连接探针。可在烧录前独立校验现有产物：

~~~sh
cargo xtask verify --display spi --release
cargo xtask verify --display all --all-profiles
~~~

普通 `cargo build` 保留 Cargo 原生行为，**只生成 ELF，不导出 BIN/HEX**：

~~~sh
cargo build --locked
cargo build --locked --release
cargo build --locked --no-default-features --features oled-i2c
~~~

普通构建的 ELF 位于 `target/thumbv7m-none-eabi/debug/gamebox-f103` 或对应的 `release/`
目录。默认启用 `oled-spi`；选择 I²C 必须同时使用 `--no-default-features`。两种接口同时
启用或均未启用都会明确报错，固件不支持 `--all-features`。

### 质量门禁

~~~sh
cargo xtask test     # core、OLED、字体工具和 xtask 的主机测试
cargo xtask check    # 格式检查、主机及两种固件的 Clippy，警告视为错误
cargo xtask ci       # 以上检查 + 四种固件构建/导出 + 产物回归测试
~~~

`ci` 还检查缓存产物重建、路径中的空格、转换失败保留旧文件，以及损坏文件检测；不需要
连接硬件。[GitHub Actions 配置](.github/workflows/ci.yml) 在 Windows、Linux、macOS
矩阵中执行同一命令。

工作区默认目标是 `thumbv7m-none-eabi`。xtask 使用 Cargo 的
[`--target host-tuple`](https://doc.rust-lang.org/cargo/commands/cargo-run.html#compilation-options)
自动选择当前主机，不硬编码 Windows target。单独运行主机工具时也要覆盖目标，例如：

~~~sh
cargo test --locked -p gamebox-core --target host-tuple
cargo check --locked -p font-subset --target host-tuple
~~~

当前双后端 Release（Flash 按 `.bin` 长度计，含对齐填充）：

| 资源 | SPI | I²C | 边界 |
| --- | ---: | ---: | ---: |
| 程序 Flash | 56,568 B | 58,056 B | 63,488 B 链接区 |
| 程序 Flash 余量 | 6,920 B | 5,432 B | 设置页不计入可用区 |
| 静态 SRAM | 6,108 B | 6,296 B | 20,480 B 总量 |
| SRAM 余量（含栈空间） | 14,372 B | 14,184 B | 非运行期栈峰值测量 |

设置独占最后 2,048 B Flash。新版 SPI 已完成烧录、按键、连续复位及故障恢复检查，
I²C 尚未做本次实机回归。实际构建占用以对应 JSON 清单为准。

普通 build 和 release 均使用 `opt-level=z`、fat LTO、单 codegen unit，并关闭全局整数
溢出检查及 debug assertions，以满足 Cortex-M3 体积预算；普通 build 保留完整调试符号。
所有依赖回绕语义的时间、代数和计数路径均显式使用 wrapping、saturating 或 checked 运算。
主机 `test` 和 `xtask` profile 显式保留溢出检查和 debug assertions；xtask 使用独立的
快速编译 profile，不受固件体积优化约束。

## 烧录与日志

以下命令以 SPI 发布文件为例；使用 I²C 模块时将文件名改为 `gamebox-f103-i2c`。
命令锁定已确认的 GameBox ST-Link，避免后续插入第二只探针时误烧其他设备：

~~~powershell
probe-rs download --probe 0483:374b:01380173524300183638414B --chip STM32F103C8Tx --protocol swd --speed 100 --verify --restore-unwritten --disable-progressbars artifacts/firmware/gamebox-f103-spi.elf
~~~

也可以烧录上面生成的裸二进制；此时必须显式指定格式和基地址：

~~~powershell
probe-rs download --probe 0483:374b:01380173524300183638414B --chip STM32F103C8Tx --protocol swd --speed 100 --verify --restore-unwritten --disable-progressbars --binary-format bin --base-address 0x08000000 artifacts/firmware/gamebox-f103-spi.bin
~~~

本板的 NRST 连接不适合 probe-rs 的 connect-under-reset 下载；普通 SWD 附着已完成写后
校验。`probe-rs 0.31.0` 没有“忽略目标电压”的命令行选项，但上面两条命令本来就不会
因 `Target voltage (VAPP)` 警告而退出；该警告只是一条 `WARN`，工具会继续连接、写入和
verify。只有在芯片识别、写入和 verify 均成功且实际供电已确认时才可忽略它。

当前指定 GameBox ST-Link 序列号为 `01380173524300183638414B`。不要删除以上命令的
`--probe` 参数而自动选择另一只探针；切换目标设备前须重新确认序列号。
多探针环境不要直接使用未指定探针的默认 `cargo run`，请使用以上显式选择命令。
正常独立启动时 BOOT0 必须置 0；写入及校验成功不代表 MCU 已从用户 Flash 启动。

`No connected probes were found` 或 `JtagGetIdcodeError` 不是电压警告：前者表示电脑没有
枚举到 ST-Link，后者表示 SWD 没有读到芯片 ID。这两种错误都发生在写入前，不能通过隐藏
电压警告来跳过。

烧录/运行并查看 RTT（此命令不是只读附着）：

~~~powershell
probe-rs run --probe 0483:374b:01380173524300183638414B --chip STM32F103C8Tx --protocol swd --speed 100 --preverify --verify --restore-unwritten artifacts/firmware/gamebox-f103-spi.elf
~~~

RTT 启动日志会打印选中的 OLED 接口及总线频率，可用于核对实际烧录版本。

USART1 启动字符串为：

~~~text
GAMEBOX FW2 UART-TX-DMA READY
~~~

随后每个事件格式为：

~~~text
BTN <ms> <key> <event> <held_ms>
~~~

按键自动回归入口为 `cargo xtask buttons --probe 01380173524300183638414B`，仅此命令
需要另外安装 OpenOCD。测试方式、适用条件及人工项目见 [实机验收清单](docs/hardware-validation.md)。
