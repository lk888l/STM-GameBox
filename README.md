# STM32 GameBox Firmware 2

**中文** | [English](README_en.md)

这是 STM32F103C8T6 掌上游戏机旧固件的重构版。旧工程仅作为功能与硬件参考；新工程以可维护、可测试、非阻塞和可继续扩展为目标，而不是逐行复刻。平台配置由 STM32CubeMX `.ioc` 管理，构建使用 CMake，产品层使用 C++20 和静态分配的 FreeRTOS Kernel 11.3.1。

当前版本已经包含旧固件的六类游戏：Dino、Snake、Air Raid、Tetris、双人 Pong 和八键电子琴。所有游戏逻辑都不调用 `HAL_Delay`，不会独占主循环，并且核心模型可以在 PC 上执行单元测试。

## 已完成的重构

- 128×64 SSD1306 OLED：卡片式主页、二级游戏菜单、列表滚动、非阻塞动画、Toast 和逐页差分刷新；
- 8 键统一输入：20 ms 消抖以及 Pressed、Released、Click、DoubleClick、LongPress、Repeat 语义事件；
- 四路 DMA：OLED I2C 发送、USART1 诊断发送、ADC 熵采样、TIM3 驱动蜂鸣器 GPIO 波形；
- 串口观察者/发布订阅：`InputService` 发布事件，`UartDmaService` 订阅后通过固定队列解耦，再由 DMA 输出；
- RTC 时钟与校时、秒表、倒计时、Input Lab、System 运行状态页；
- 声音、动画等级、OLED 亮度和 STM32F1 软件日期通过 RTC Backup Register 持久化；
- 所有任务、队列、信号量、画布、游戏状态和字符串均为静态容量，运行期不使用堆；
- Debug/Release 都以严格告警构建，链接后自动检查是否意外引入堆符号。

## DMA 与实时性

STM32F103 的 DMA 请求映射是固定的。当前分配没有通道冲突：

| 用途 | 外设请求 | DMA | 模式 | CPU/RTOS 行为 |
|---|---|---|---|---|
| 启动随机种子 | ADC1 | DMA1 Channel 1 | Normal，16×16-bit | 启动前一次性采样，5 ms 有界超时 |
| 无源蜂鸣器 | TIM3_UP | DMA1 Channel 3 | Circular，2×32-bit | 交替写 GPIOA BSRR，不再产生音频频率中断 |
| 串口诊断 | USART1_TX | DMA1 Channel 4 | Normal，8-bit | 低优先级任务等待二值信号量，不忙等 |
| OLED 刷新 | I2C1_TX | DMA1 Channel 6 | Normal，8-bit | UI 任务休眠等待完成；只发送变化页 |

DMA1 Channel 1/4/6、I2C1 EV/ER 和 USART1 中断优先级均为 6，满足 FreeRTOS `FromISR` API 的优先级约束。TIM3 Channel 3 使用循环 DMA 且不需要完成中断。

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
- 长按 Func：在非游戏页面打开 Input Lab；
- 双击 Func：在非游戏页面快速打开时钟；
- 时钟：Enter/Jump 进入校时，Left/Right 选字段，Up/Down 调整，Enter/Jump 保存，Back/Func 取消；
- 秒表：Enter/Jump 启停，Func 清零；
- 倒计时：Up/Down 调分钟，Left/Right 调秒，Enter/Jump 启停，Func 恢复 5 分钟。

Click 会在双击窗口结束后确认，以保证 Click 与 DoubleClick 互斥。方向导航和游戏操作使用 Pressed/Repeat，不会承受这段确认延迟。

## 引脚与片上资源

下表是当前固件和 `.ioc` 的权威分配。按键均为内部上拉、低电平按下。

| 功能 | 引脚/资源 | 配置与说明 |
|---|---|---|
| HSE | PD0 / PD1 | 8 MHz 晶振，PLL ×9 得到 72 MHz |
| LSE / RTC | PC14 / PC15 | 32.768 kHz 晶振与 backup domain |
| ADC 熵源 | PA1 / ADC1_IN1 | 55.5 cycles，DMA1 Channel 1 |
| USART1 TX/RX | PA9 / PA10 | 115200 8N1；TX 使用 DMA1 Channel 4 |
| 无源蜂鸣器 | PA12 + TIM3_UP | GPIO push-pull；DMA1 Channel 3 写 BSRR |
| SWD | PA13 / PA14 | SWDIO / SWCLK；保留调试，JTAG 关闭 |
| LED 1..4 | PB0 / PB1 / PB10 / PB11 | push-pull，启动低电平，故障诊断 |
| Right / Down / Left / Up | PB4 / PB5 / PB6 / PB7 | input pull-up；PB4 因关闭 JTAG 而释放 |
| OLED SCL/SDA | PB8 / PB9 | I2C1 remap，400 kHz，TX DMA1 Channel 6，地址 0x3C |
| Jump / Func / Enter / Back | PB12 / PB13 / PB14 / PB15 | input pull-up |
| HAL 时基 | TIM4 | 1 ms；不占用外部引脚 |
| RTOS 时基 | SysTick | FreeRTOS 1 kHz tick |

PA0、PA2–PA8、PA11、PA15、PB3、PC13 当前未由固件分配；PB2 通常参与 BOOT1 配置。扩展硬件前仍应以实际 PCB 原理图核对，尤其不要复用 SWD、晶振和启动配置引脚。

更完整的时钟、DMA、中断和总线说明见 [硬件与引脚](docs/hardware.md)。

## 构建与验证

需要 CMake 3.22+、Ninja，以及可从 `PATH` 找到的 `arm-none-eabi-gcc/g++`。当前已用 Arm GNU Toolchain 15.2.1 验证。

执行完整验证：

```powershell
pwsh -File scripts/verify.ps1
```

或分别构建：

```powershell
cmake --preset Debug
cmake --build --preset Debug

cmake --preset Release
cmake --build --preset Release

cmake -S tests -B build/HostTests -G Ninja
cmake --build build/HostTests
ctest --test-dir build/HostTests --output-on-failure
```

输出位于 `build/Debug` 和 `build/Release`：`.elf`、`.hex`、`.bin`、`.map`。OLED 桌面预览由验证脚本生成到 `build/HostTests/oled_menu_preview.bmp`。

本机已验证可用的是 ST-LINK Utility CLI 的低速、复位下连接模式：

```powershell
ST-LINK_CLI.exe -c SWD Freq=100 UR LPM `
  -P build/Release/stm32103c8t6_game-box.hex `
  -V after_programming -Rst
```

也可以使用 OpenOCD 烧录 Release ELF：

```powershell
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg `
  -c "adapter speed 1000" `
  -c "program build/Release/stm32103c8t6_game-box.elf verify reset exit"
```

若目标程序改变了调试相关状态，可尝试 OpenOCD 的复位下连接：

```powershell
openocd -f interface/stlink.cfg `
  -c "transport select swd" `
  -c "adapter speed 100" `
  -c "reset_config srst_only srst_nogate connect_assert_srst" `
  -f target/stm32f1x.cfg `
  -c "program build/Release/stm32103c8t6_game-box.elf verify reset exit"
```

串口启动后应输出 `GAMEBOX FW2 UART-TX-DMA READY`，随后以 `BTN <ms> <key> <event> <held_ms>` 格式输出按键事件。System 页可检查四个任务的最小栈余量、输入/UART 丢弃数、I2C/UART 错误数和 OLED DMA 发送次数。

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

手写 C++ 使用 `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wundef -Werror`。CubeMX 输出和 `ThirdParty` 保持各自格式。重新生成 CubeMX 代码时不要启用其 FreeRTOS middleware；本工程独立维护内核，并在根 CMake 中处理 Cortex-M3 的 SVC/PendSV/SysTick 向量所有权。

进一步阅读：

- [架构说明](docs/architecture.md)
- [硬件、DMA 与引脚](docs/hardware.md)
- [按键事件与订阅](docs/input-events.md)
- [OLED 菜单、游戏与动画](docs/ui-design.md)
- [代码审查记录](docs/code-review.md)
- [依赖版本与来源](ThirdParty/UPSTREAM.md)
- [第三方声明](THIRD_PARTY_NOTICES.md)

## 当前资源预算

Arm GNU 15.2.1 的最近一次完整构建结果：

| 配置 | Flash | SRAM | 说明 |
|---|---:|---:|---|
| Debug (`-Og -g3`) | 58,028 B / 64 KiB（88.54%） | 11,448 B / 20 KiB（55.90%） | 可调试、无 LTO |
| Release (`-Os -flto`) | 41,148 B / 64 KiB（62.79%） | 11,440 B / 20 KiB（55.86%） | 推荐烧录 |

RAM 数字已包含链接器保留的 1 KiB 栈。Debug 的 Flash 余量较小，后续新增大字库或图片资源前应优先检查 Release 预算，并持续保留 Debug 可链接能力。
