# STM32 GameBox Firmware 2

**中文** | [English](README_en.md)

这是 STM32F103C8T6 掌上游戏机旧固件的重构版。旧工程仅作为功能与硬件参考；新工程以可维护、可测试、非阻塞和可继续扩展为目标，而不是逐行复刻。平台配置由 STM32CubeMX `.ioc` 管理，构建使用 CMake，产品层使用 C++20 和静态分配的 FreeRTOS Kernel 11.3.1。

当前版本已经包含旧固件的六类游戏：Dino、Snake、Air Raid、Tetris、双人 Pong 和八键电子琴。所有游戏逻辑都不调用 `HAL_Delay`，不会独占主循环，并且核心模型可以在 PC 上执行单元测试。

## 已完成的重构

- 128×64 SSD1306 OLED：卡片式主页、可配置的时间/日期/宠物/品牌顶栏、二级游戏菜单、列表滚动、非阻塞动画和 Toast；SPI 在画面变化时发送完整帧，I2C 逐页差分刷新；
- 8 键统一输入：20 ms 消抖以及 Pressed、Released、Click、DoubleClick、LongPress、Repeat 语义事件；
- 四路 DMA：OLED SPI/I2C 发送、USART1 诊断发送、ADC 熵采样、TIM2 驱动蜂鸣器 GPIO 波形；
- 串口观察者/发布订阅：`InputService` 发布事件，`UartDmaService` 订阅后通过固定队列解耦，再由 DMA 输出；
- RTC 时钟与校时、秒表、倒计时、Input Lab、System 运行状态页；
- 声音、动画等级、OLED 亮度、主页顶栏模式和 STM32F1 软件日期通过 RTC Backup Register 持久化；
- 所有任务、队列、信号量、画布、游戏状态和字符串均为静态容量，运行期不使用堆；
- 默认 SPI1 9 MHz，可切换 I2C；SPI UI 每 5 ms 调度，I2C 每 33 ms 调度；持续按住方向键不会饿死渲染；
- Debug/Release 都以严格告警构建，自动检查堆符号、Flash/SRAM 边界、复位向量和固件包一致性。

## DMA 与实时性

STM32F103 的 DMA 请求映射是固定的。当前分配没有通道冲突：

| 用途 | 外设请求 | DMA | 模式 | CPU/RTOS 行为 |
|---|---|---|---|---|
| 启动随机种子 | ADC1 | DMA1 Channel 1 | Normal，16×16-bit | 启动前一次性采样，5 ms 有界超时 |
| 无源蜂鸣器 | TIM2_UP | DMA1 Channel 2 | Circular，2×32-bit | 交替写 GPIOA BSRR，不再产生音频频率中断 |
| 串口诊断 | USART1_TX | DMA1 Channel 4 | Normal，8-bit | 低优先级任务等待二值信号量，不忙等 |
| OLED SPI（默认） | SPI1_TX | DMA1 Channel 3 | Normal，8-bit | 变化时一次发送完整 1024 B，静态画面跳过；UI 等待时休眠 |
| OLED I2C（可选） | I2C1_TX | DMA1 Channel 6 | Normal，8-bit | UI 任务休眠等待完成；只发送变化页 |

SPI1 和 DMA1 Channel 3 的中断优先级为 5；DMA1 Channel 1/4/6、I2C1 EV/ER 和 USART1 的优先级为 6，均满足 FreeRTOS `FromISR` API 的优先级约束。蜂鸣器改用 TIM2 / DMA1 Channel 2，避免与 SPI1 TX 冲突；循环 DMA 不需要完成中断。

并非所有外设都适合 DMA：按键没有 GPIO→内存 DMA 请求，而且一次读取 `GPIOB->IDR` 已能得到八键一致快照；RTC 和 LED 数据量太小；USART1 RX 目前没有命令协议，因此没有保留一个长期空转的 RX DMA 缓冲区。若以后加入串口控制台，建议使用 RX circular DMA + IDLE line 分帧。

## 游戏与操作

主页选择 `GAMES` 后进入六游戏列表。游戏中短按 Back 返回；电子琴需要长按 Back 返回，因为短按 Back 是第八个琴键。

| 游戏 | 操作 |
|---|---|
| Dino | Jump、Up 或 Enter 开始/跳跃；速度随得分提高 |
| Snake | 方向键转向；Enter 或 Jump 开始/重开 |
| Air Raid | Up/Down 连续移动，Jump 发射，Enter 开始/重开；三条生命、最多三发在途子弹 |
| Tetris | Left/Right 移动，Down 软降，Up 硬降，Jump/Func 分别向左/向右旋转，Enter 开始/重开 |
| Pong 2P | 左玩家 Up/Down，右玩家 Jump/Func，Enter 开始/重开；漏球会缩短球拍，耗尽后判负 |
| Piano | Up、Left、Right、Down、Jump、Func、Enter、Back 对应 C4 到 C5；长按 Back 退出 |

通用界面操作：

- 主页：方向键切换卡片，Enter 或 Jump 确认；
- 列表：Up/Down 选择，Enter 或 Jump 确认，Back 返回；
- Settings → Home Header：用 Left/Right、Enter 或 Jump 在 Time、Date、Pet、Title 间切换，断电保留；
- 菜单页单击 Func：显示当前菜单项说明；
- 长按 Func：在非游戏页面打开 Input Lab；
- 双击 Func：在非游戏页面快速打开时钟；
- 时钟：Enter/Jump 进入校时，Left/Right 选字段，Up/Down 调整，Enter/Jump 保存，Back/Func 取消；
- 秒表：Enter/Jump 启停，Func 清零；
- 倒计时：Up/Down 调分钟，Left/Right 调秒，Enter/Jump 启停，Func 恢复 5 分钟。

所有菜单和功能页的 Enter/Jump 确认都在 20 ms 消抖后的 Pressed 边沿执行；进行中的卡片或页面动画不会锁住输入。同一次物理按键的尾随事件会被拦截，但新的消抖 Pressed 仍可立即执行。仍需区分单击/双击的 Func 等操作继续等待 280 ms 窗口；方向导航和游戏操作使用 Pressed/Repeat。

## 引脚与片上资源

下表是当前固件的分配。`.ioc` 描述默认 SPI 硬件；I2C 是 CMake 选择的兼容配置。按键均为内部上拉、低电平按下。

| 功能 | 引脚/资源 | 配置与说明 |
|---|---|---|
| HSE | PD0 / PD1 | 8 MHz 晶振，PLL ×9 得到 72 MHz |
| LSE / RTC | PC14 / PC15 | 32.768 kHz 晶振与 backup domain |
| ADC 熵源 | PA1 / ADC1_IN1 | 55.5 cycles，DMA1 Channel 1 |
| USART1 TX/RX | PA9 / PA10 | 115200 8N1；TX 使用 DMA1 Channel 4 |
| 无源蜂鸣器 | PA12 + TIM2_UP | GPIO push-pull；DMA1 Channel 2 写 BSRR |
| SWD | PA13 / PA14 | SWDIO / SWCLK；保留调试，JTAG 关闭 |
| LED 1..4 | PB0 / PB1 / PB10 / PB11 | push-pull，启动低电平，故障诊断 |
| Right / Down / Left / Up | PB4 / PB5 / PB6 / PB7 | input pull-up；PB4 因关闭 JTAG 而释放 |
| OLED SCK / MOSI | PA5 / PA7 | 默认 SPI1，mode 0，MSB first，9 MHz，TX DMA1 Channel 3 |
| OLED DC / CS / RESET | PA6 / PA4 / PA8 | SPI 控制引脚；PA6 为 DC，不接 MISO |
| OLED SCL / SDA（I2C 构建） | PB8 / PB9 | I2C1 remap，400 kHz，TX DMA1 Channel 6，地址 0x3C |
| Jump / Func / Enter / Back | PB12 / PB13 / PB14 / PB15 | input pull-up |
| HAL 时基 | TIM4 | 1 ms；不占用外部引脚 |
| RTOS 时基 | SysTick | FreeRTOS 1 kHz tick |

PA0、PA2–PA3、PA11、PA15、PB3、PC13 当前未由固件分配；默认 SPI 构建还空出 PB8/PB9。PB2 通常参与 BOOT1 配置。SPI 与 I2C 构建都保留 PB6/PB7 的 Left/Up 按键，不改变按键接线。

更完整的时钟、DMA、中断和总线说明见 [硬件与引脚](docs/hardware.md)。

## 构建与验证

需要 CMake 3.22+、Ninja、原生 C++20 编译器，以及可从 `PATH` 找到的 `arm-none-eabi-gcc/g++/objcopy/gcc-nm`。当前已用 Arm GNU Toolchain 15.2.1 验证。完整流程使用 CMake，不依赖 Rust、Python 或 PowerShell，可在 Windows、Linux、macOS 上执行。

执行完整验证：

```sh
cmake -P scripts/verify.cmake
```

或分别构建：

```sh
cmake --preset Debug-SPI
cmake --build --preset Debug-SPI

cmake --preset Release-I2C
cmake --build --preset Release-I2C

cmake -DMODE=TEST -P scripts/verify.cmake
cmake -DMODE=CHECK -P scripts/verify.cmake
cmake -DMODE=VERIFY -P scripts/verify.cmake
```

`Debug-SPI`、`Debug-I2C`、`Release-SPI`、`Release-I2C` 分别使用独立构建目录；原有 `Debug` / `Release` / `Analyze` 名称仍默认 SPI。也可在配置时传入 `-DGAMEBOX_OLED_INTERFACE=SPI` 或 `I2C`。验证脚本支持 `-DMODE=BUILD|TEST|CHECK|VERIFY|CI`、`-DDISPLAY=SPI|I2C|ALL`、`-DPROFILE=Debug|Release|ALL`；默认 CI 检查两种接口、两种构建配置、主机测试与静态分析。`VERIFY` 只检查已有固件包，不构建、不连接硬件。

构建目录保留原有 `.elf`、`.hex`、`.bin`、`.map` 文件，并自动导出 `artifacts/firmware/debug/` 与 `artifacts/firmware/release/` 下的 `gamebox-f103-spi.*` / `gamebox-f103-i2c.*`。每个包包含 ELF、BIN、HEX、SHA256 校验文件和 JSON 清单。导出前检查 ELF 32-bit ARM/小端格式、Flash/SRAM 载入范围、栈/复位向量、HEX 校验和以及 ELF/BIN/HEX 内容一致性；转换或校验失败不会覆盖已有包。即使编译命中缓存，下次构建也会补齐缺失的导出文件。`cmake --build build/Release-SPI --target verify_artifacts` 可单独审计该配置，`ctest --test-dir build/Release-SPI --output-on-failure` 执行相同审计。

OLED 桌面预览由 CTest 生成到 `build/HostTests/oled_menu_preview.bmp`。`Analyze-SPI/I2C` 启用 GCC `-fanalyzer` 并将结果导出到 `artifacts/analyze/`；已知的 ETL `string_view` 和故障停机死循环误报类别继续排除。GitHub Actions 在三个桌面平台运行主机测试，并在 Linux 构建与验证四种固件、执行两种接口静态分析及导出损坏回归测试。

### 烧录与调试（历史 I2C 验证记录）

下面的 STM32CubeProgrammer / OpenOCD 连接经验来自此前 I2C 固件的硬件验证，不能据此认定本次 SPI 移植已在板上验证。本次工作没有烧录设备；烧录前需按实际 OLED 接线选择对应 SPI/I2C 固件，并确认 ST-LINK 对应的目标板。

历史调试配置使用 SWD 140 kHz，并在启动调试时复位目标。若克隆版 ST-LINK 只误报目标电压，但仍能识别 STM32F103、写入校验通过且 GDB 能停住，可按实际连接结果判断；若连接或校验失败，则必须先检查 3.3 V、GND、SWDIO、SWCLK 和 NRST，不能直接忽略电压提示。

`.settings/` 是 STM32Cube 扩展识别器件、工具链和 bundle 的共享项目元数据，不应整体忽略。`.vscode/` 也不应整体忽略：`settings.json`、`extensions.json`、`launch.json`、`tasks.json` 等可复现配置应提交，用户本地状态继续由当前 `.gitignore` 的白名单规则排除。

此前 I2C 固件验证过 `STM32_Programmer_CLI` 的低速、复位下连接模式；以下示例路径为默认 SPI Debug 构建，需匹配接线：

```powershell
STM32_Programmer_CLI.exe `
  -c port=SWD freq=100 mode=UR reset=HWrst `
  -w build/Debug/stm32103c8t6_game-box.elf -v -rst
```

目标电压读数正常时，也可以使用 OpenOCD 烧录 Release ELF；部分克隆探针会被 OpenOCD 的低电压保护直接拒绝，此时应使用上面的 STM32CubeProgrammer/ST-LINK GDB Server 链路：

```powershell
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg `
  -c "adapter speed 1000" `
  -c "program build/Release/stm32103c8t6_game-box.elf verify reset exit"
```

若目标程序改变了调试相关状态，可尝试 OpenOCD 的复位下连接：

```powershell
openocd -f interface/stlink.cfg `
  -f target/stm32f1x.cfg `
  -c "transport select swd" `
  -c "adapter speed 100" `
  -c "reset_config srst_only srst_nogate connect_assert_srst" `
  -c "program build/Release/stm32103c8t6_game-box.elf verify reset exit"
```

`target/stm32f1x.cfg` 必须先于 `adapter speed 100` 加载，否则目标脚本会把低速值覆盖回默认值。若探针没有连接 NRST，物理 reset 可能让 CPU 留在 RAM 烧录算法中；此时改用 Cortex-M 软件复位：

```powershell
openocd -f interface/stlink.cfg `
  -f target/stm32f1x.cfg `
  -c "transport select swd" `
  -c "adapter speed 100" `
  -c "reset_config none" `
  -c "cortex_m reset_config sysresetreq" `
  -c "program build/Release/stm32103c8t6_game-box.elf verify reset exit"
```

串口启动后应输出 `GAMEBOX FW2 UART-TX-DMA READY`，随后以 `BTN <ms> <key> <event> <held_ms>` 格式输出按键事件。System 页可检查四个任务的最小栈余量、输入/UART 丢弃数、OLED/UART 错误数和 OLED DMA 发送次数。

## 架构边界

```text
Core/          CubeMX 管理的 C/HAL、DMA、IRQ 边界
App/           手写 C++20 产品代码
  Config/      FreeRTOSConfig.h
  Inc/Src/     app、audio、diagnostics、display、games、input、platform、storage、ui
ThirdParty/    锁定版本的 FreeRTOS Kernel 与 ETL
tests/         不依赖 MCU 的模型测试与 OLED 预览器
docs/          架构、硬件、UI、输入、代码审查和 ADR
```

手写 C++ 使用 `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wundef -Werror`。CubeMX 输出和 `ThirdParty` 保持各自格式。重新生成 CubeMX 代码时不要启用其 FreeRTOS middleware；本工程独立维护内核，并在根 CMake 中处理 Cortex-M3 的 SVC/PendSV/SysTick 向量所有权。重新生成后还需检查并保留 Core 中的双接口条件、STM32F1 的 SPI SCK 启动处理和 FreeRTOS 中断优先级。

进一步阅读：

- [架构说明](docs/architecture.md)
- [硬件、DMA 与引脚](docs/hardware.md)
- [按键事件与订阅](docs/input-events.md)
- [OLED 菜单、游戏与动画](docs/ui-design.md)
- [代码审查记录](docs/code-review.md)
- [依赖版本与来源](ThirdParty/UPSTREAM.md)
- [第三方声明](THIRD_PARTY_NOTICES.md)

## 当前资源预算

程序 Flash 上限为 62 KiB（`0x08000000..0x0800F800`），末尾 2 KiB 保留供持久化扩展；当前设置仍存于 RTC Backup Register。SRAM 上限为 20 KiB，清单数值包含链接器保留的 1 KiB 栈。Debug 使用 `-Os -g3 -flto`，Release 使用 `-Os -g0 -flto -DNDEBUG`，两者保持相同优化策略，Debug 保留调试信息。实际占用以每次链接报告和对应固件包 JSON 清单为准。
