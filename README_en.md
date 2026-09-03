# STM32 GameBox Firmware 2

[中文](README.md) | **English**

This is a ground-up refactor of the legacy STM32F103C8T6 handheld-console firmware. The old project is used as a hardware and feature reference, not copied line by line. The new baseline is maintainable, host-testable, non-blocking, and extensible. STM32CubeMX owns the `.ioc` platform configuration, CMake drives the build, and the product layer uses C++20 with a statically allocated FreeRTOS Kernel 11.3.1.

The firmware now contains all six legacy game categories: Dino, Snake, Air Raid, Tetris, two-player Pong, and an eight-key piano. Game models never call `HAL_Delay`, never take over a blocking main loop, and run in native host tests.

## Delivered Refactor

- Redesigned 128×64 SSD1306 UI with home cards, a six-game menu, scrolling lists, non-blocking transitions, toasts, and page-differential refresh;
- Unified eight-button input with 20 ms debounce and Pressed, Released, Click, DoubleClick, LongPress, and Repeat events;
- Four DMA paths: OLED I2C TX, USART1 diagnostic TX, ADC entropy sampling, and TIM3-paced buzzer GPIO edges;
- Observer/pub-sub telemetry: `InputService` publishes events, `UartDmaService` subscribes through a fixed queue, and USART sends with DMA;
- Adjustable RTC clock, stopwatch, countdown, Input Lab, and runtime System page;
- RTC Backup Register persistence for sound, motion, brightness, and the STM32F1 software date;
- Static task stacks, queues, semaphores, canvases, strings, and game state, with no runtime heap;
- Strict Debug and Release builds plus a post-link no-heap audit.

## DMA and Real-Time Behavior

The STM32F103 DMA request map is fixed. These assignments do not conflict:

| Purpose | Peripheral request | DMA | Mode | CPU/RTOS behavior |
|---|---|---|---|---|
| Startup random seed | ADC1 | DMA1 Channel 1 | Normal, 16×16-bit | One bounded pre-scheduler sample burst |
| Passive buzzer | TIM3_UP | DMA1 Channel 3 | Circular, 2×32-bit | Alternates GPIOA BSRR writes; no audio-rate ISR |
| Telemetry | USART1_TX | DMA1 Channel 4 | Normal, 8-bit | Low-priority task sleeps on a binary semaphore |
| OLED refresh | I2C1_TX | DMA1 Channel 6 | Normal, 8-bit | UI task sleeps until completion and sends changed pages only |

DMA1 Channel 1/4/6, I2C1 EV/ER, and USART1 use interrupt priority 6, which is valid for the configured FreeRTOS `FromISR` API boundary. TIM3 Channel 3 runs circularly without a completion interrupt.

DMA is deliberately not used where it adds no value. GPIO has no suitable GPIO-to-memory DMA request for the buttons, and a single `GPIOB->IDR` read already gives a coherent eight-key snapshot. RTC and LED traffic is tiny. USART1 RX has no command protocol yet, so an idle permanent receive buffer is not allocated. A future console should use circular RX DMA with USART IDLE-line framing.

## Games and Controls

Select `GAMES` on the home screen to open the six-game list. A short Back press exits a game, except in Piano where Back is the eighth note and a long Back press exits.

| Game | Controls |
|---|---|
| Dino | Jump, Up, or Enter starts/jumps; speed increases with score |
| Snake | Direction keys steer; Enter or Jump starts/restarts |
| Air Raid | Hold Up/Down to move, Jump fires, Enter starts/restarts; three lives and three in-flight bullets |
| Tetris | Left/Right move, Down soft-drops, Up hard-drops, Jump/Func rotate left/right, Enter starts/restarts |
| Pong 2P | Left player uses Up/Down, right player uses Jump/Func, Enter starts/restarts; missed balls shrink a paddle |
| Piano | Up, Left, Right, Down, Jump, Func, Enter, Back play C4 through C5; hold Back to exit |

General UI controls:

- Home: directional keys change cards; Enter or Jump confirms;
- Lists: Up/Down selects, Enter or Jump confirms, Back returns;
- Hold Func on a non-game page to open Input Lab;
- Double-click Func on a non-game page to open the clock;
- Clock: Enter/Jump edits, Left/Right selects a field, Up/Down adjusts, Enter/Jump saves, Back/Func cancels;
- Stopwatch: Enter/Jump starts or stops and Func resets;
- Countdown: Up/Down adjusts minutes, Left/Right adjusts seconds, Enter/Jump starts or stops, and Func restores five minutes.

Click is confirmed only after the double-click window, keeping Click and DoubleClick mutually exclusive. Direction navigation and game input consume Pressed/Repeat and therefore have no confirmation delay.

## Pin and On-Chip Resource Allocation

This table matches the firmware and root `.ioc`. Every button uses an internal pull-up and is active low.

| Function | Pin/resource | Configuration |
|---|---|---|
| HSE | PD0 / PD1 | 8 MHz crystal; PLL ×9 for 72 MHz |
| LSE / RTC | PC14 / PC15 | 32.768 kHz crystal and backup domain |
| Entropy ADC | PA1 / ADC1_IN1 | 55.5-cycle sampling; DMA1 Channel 1 |
| USART1 TX/RX | PA9 / PA10 | 115200 8N1; TX on DMA1 Channel 4 |
| Passive buzzer | PA12 + TIM3_UP | Push-pull GPIO; DMA1 Channel 3 writes BSRR |
| SWD | PA13 / PA14 | SWDIO / SWCLK retained; JTAG disabled |
| LEDs 1..4 | PB0 / PB1 / PB10 / PB11 | Push-pull, initially low, fault diagnostics |
| Right / Down / Left / Up | PB4 / PB5 / PB6 / PB7 | Pull-up inputs; disabling JTAG frees PB4 |
| OLED SCL/SDA | PB8 / PB9 | Remapped I2C1, 400 kHz, TX DMA1 Channel 6, address 0x3C |
| Jump / Func / Enter / Back | PB12 / PB13 / PB14 / PB15 | Pull-up inputs |
| HAL timebase | TIM4 | 1 ms; no external pin |
| RTOS timebase | SysTick | FreeRTOS 1 kHz tick |

PA0, PA2–PA8, PA11, PA15, PB3, and PC13 are currently unallocated by firmware. PB2 commonly participates in BOOT1 configuration. Check the actual PCB schematic before extending the board, particularly around SWD, oscillator, and boot pins.

See [Hardware, DMA, and pinout](docs/hardware.md) for the complete clock and interrupt details.

## Build and Verification

The build needs CMake 3.22+, Ninja, and `arm-none-eabi-gcc/g++` on `PATH`. It has been verified with Arm GNU Toolchain 15.2.1.

Run the complete verification pipeline:

```powershell
pwsh -File scripts/verify.ps1
```

Or build each target manually:

```powershell
cmake --preset Debug
cmake --build --preset Debug

cmake --preset Release
cmake --build --preset Release

cmake -S tests -B build/HostTests -G Ninja
cmake --build build/HostTests
ctest --test-dir build/HostTests --output-on-failure
```

The `.elf`, `.hex`, `.bin`, and `.map` files are written to `build/Debug` and `build/Release`. The verification script also generates `build/HostTests/oled_menu_preview.bmp`.

### Flashing and debugging with VS Code + STM32Cube

The repository includes the STM32Cube project metadata and shared debug configuration. After installing the recommended `STM32Cube for Visual Studio Code` extension pack, open the repository root, wait for bundle installation and CMake Configure to finish, select `STM32Cube: Build, flash and debug (ST-LINK)` in Run and Debug, and press `F5`. It builds the Debug ELF, downloads and verifies it, then stops at `main`.

The launch uses SWD at 140 kHz and resets the target when starting a debug session, which is more reliable on this board. Some clone probes can misreport target voltage. If the STM32F103 is identified, programming verifies, and GDB halts correctly, judge the session by those concrete results. If connection or verification fails, check 3.3 V, GND, SWDIO, SWCLK, and NRST rather than ignoring the voltage warning.

Keep `.settings/` tracked: it is shared STM32Cube metadata for the device, toolchain, and bundles. Do not ignore all of `.vscode/` either. Shared `settings.json`, `extensions.json`, `launch.json`, and `tasks.json` files belong in Git, while the existing `.gitignore` allowlist continues to exclude local editor state.

`STM32_Programmer_CLI` has been verified locally to download, verify, and reset using low-speed connect-under-reset:

```powershell
STM32_Programmer_CLI.exe `
  -c port=SWD freq=100 mode=UR reset=HWrst `
  -w build/Debug/stm32103c8t6_game-box.elf -v -rst
```

When the target-voltage reading is valid, OpenOCD can alternatively flash the Release ELF. Some clone probes are rejected by OpenOCD's low-voltage protection; use the STM32CubeProgrammer/ST-LINK GDB Server path above in that case:

```powershell
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg `
  -c "adapter speed 1000" `
  -c "program build/Release/stm32103c8t6_game-box.elf verify reset exit"
```

If target firmware interferes with debug entry, use OpenOCD connect-under-reset:

```powershell
openocd -f interface/stlink.cfg `
  -c "transport select swd" `
  -c "adapter speed 100" `
  -c "reset_config srst_only srst_nogate connect_assert_srst" `
  -f target/stm32f1x.cfg `
  -c "program build/Release/stm32103c8t6_game-box.elf verify reset exit"
```

At boot, USART1 emits `GAMEBOX FW2 UART-TX-DMA READY`, followed by `BTN <ms> <key> <event> <held_ms>` event lines. The System page reports minimum stack headroom across all four tasks, input/UART drops, I2C/UART errors, and OLED DMA transfer count.

## Project Boundaries

```text
Core/          CubeMX-managed C/HAL, DMA, and IRQ boundary
App/           Handwritten C++20 product code
  Config/      FreeRTOSConfig.h
  Inc/Src/     app, audio, diagnostics, display, games, input, platform, storage, UI
ThirdParty/    Pinned FreeRTOS Kernel and ETL
tests/         MCU-independent model tests and OLED preview
docs/          Architecture, hardware, UI, input, review, and ADRs
```

Handwritten C++ builds with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wundef -Werror`. CubeMX output and `ThirdParty` keep their upstream style. Do not enable CubeMX's FreeRTOS middleware when regenerating code; this repository owns the kernel and resolves SVC/PendSV/SysTick vector ownership in the root CMake file.

Further reading:

- [Architecture](docs/architecture.md)
- [Hardware, DMA, and pinout](docs/hardware.md)
- [Button events and subscriptions](docs/input-events.md)
- [OLED menus, games, and motion](docs/ui-design.md)
- [Code review record](docs/code-review.md)
- [Dependency versions and sources](ThirdParty/UPSTREAM.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

## Current Resource Budget

Latest full builds with Arm GNU 15.2.1:

| Configuration | Flash | SRAM | Notes |
|---|---:|---:|---|
| Debug (`-Og -g3`) | 58,772 B / 64 KiB (89.68%) | 11,464 B / 20 KiB (55.98%) | Debuggable, no LTO |
| Release (`-Os -flto`) | 41,916 B / 64 KiB (63.96%) | 11,448 B / 20 KiB (55.90%) | Recommended image |

The SRAM figure includes the linker's 1 KiB stack reservation. Debug Flash headroom is now tighter, so large glyph or bitmap additions should be checked against Release first while preserving the ability to link a Debug image.
