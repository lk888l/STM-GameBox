# STM32 GameBox Firmware 2

[中文](README.md) | **English**

This is a ground-up refactor of the legacy STM32F103C8T6 handheld-console firmware. The old project is used as a hardware and feature reference, not copied line by line. The new baseline is maintainable, host-testable, non-blocking, and extensible. STM32CubeMX owns the `.ioc` platform configuration, CMake drives the build, and the product layer uses C++20 with a statically allocated FreeRTOS Kernel 11.3.1.

The firmware now contains all six legacy game categories: Dino, Snake, Air Raid, Tetris, two-player Pong, and an eight-key piano. Game models never call `HAL_Delay`, never take over a blocking main loop, and run in native host tests.

## Delivered Refactor

- Redesigned 128×64 SSD1306 UI with home cards, a configurable time/date/pet/brand header, a six-game menu, scrolling lists, non-blocking transitions, and toasts; SPI sends complete changed frames and I2C sends changed pages;
- Unified eight-button input with 20 ms debounce and Pressed, Released, Click, DoubleClick, LongPress, and Repeat events;
- Four DMA paths: OLED SPI/I2C TX, USART1 diagnostic TX, ADC entropy sampling, and TIM2-paced buzzer GPIO edges;
- Observer/pub-sub telemetry: `InputService` publishes events, `UartDmaService` subscribes through a fixed queue, and USART sends with DMA;
- Adjustable RTC clock, stopwatch, countdown, Input Lab, and runtime System page;
- RTC Backup Register persistence for sound, motion, brightness, the home-header mode, and the STM32F1 software date;
- Static task stacks, queues, semaphores, canvases, strings, and game state, with no runtime heap;
- SPI1 at 9 MHz by default, with selectable I2C; the UI runs every 5 ms on SPI or 33 ms on I2C, and held direction keys cannot starve rendering;
- Strict Debug and Release builds with heap, Flash/SRAM, reset-vector, and firmware package audits.

## DMA and Real-Time Behavior

The STM32F103 DMA request map is fixed. These assignments do not conflict:

| Purpose | Peripheral request | DMA | Mode | CPU/RTOS behavior |
|---|---|---|---|---|
| Startup random seed | ADC1 | DMA1 Channel 1 | Normal, 16×16-bit | One bounded pre-scheduler sample burst |
| Passive buzzer | TIM2_UP | DMA1 Channel 2 | Circular, 2×32-bit | Alternates GPIOA BSRR writes; no audio-rate ISR |
| Telemetry | USART1_TX | DMA1 Channel 4 | Normal, 8-bit | Low-priority task sleeps on a binary semaphore |
| OLED SPI (default) | SPI1_TX | DMA1 Channel 3 | Normal, 8-bit | One complete 1024-byte transfer on changes; static frames skipped; UI sleeps while waiting |
| OLED I2C (optional) | I2C1_TX | DMA1 Channel 6 | Normal, 8-bit | UI task sleeps until completion and sends changed pages only |

SPI1 and DMA1 Channel 3 use interrupt priority 5. DMA1 Channel 1/4/6, I2C1 EV/ER, and USART1 use priority 6. All satisfy the FreeRTOS `FromISR` API boundary. The buzzer uses TIM2 / DMA1 Channel 2 to avoid a collision with SPI1 TX, and runs circularly without a completion interrupt.

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
- Settings → Home Header: Left/Right, Enter, or Jump cycles Time, Date, Pet, and Title; the choice survives power loss;
- Click Func on a menu page to show the selected item's description;
- Hold Func on a non-game page to open Input Lab;
- Double-click Func on a non-game page to open the clock;
- Clock: Enter/Jump edits, Left/Right selects a field, Up/Down adjusts, Enter/Jump saves, Back/Func cancels;
- Stopwatch: Enter/Jump starts or stops and Func resets;
- Countdown: Up/Down adjusts minutes, Left/Right adjusts seconds, Enter/Jump starts or stops, and Func restores five minutes.

Every Enter/Jump confirmation in menus and utility pages runs on the Pressed edge after the 20 ms debounce; an in-flight card or page transition never locks input. Tail events from that physical press are filtered, while a new debounced Pressed event remains immediately actionable. Operations that still distinguish a Func click from a double-click keep the 280 ms decision window; direction navigation and game input use Pressed/Repeat.

## Pin and On-Chip Resource Allocation

This table describes the firmware. The root `.ioc` describes the default SPI hardware; CMake also selects the compatible I2C configuration. Every button uses an internal pull-up and is active low.

| Function | Pin/resource | Configuration |
|---|---|---|
| HSE | PD0 / PD1 | 8 MHz crystal; PLL ×9 for 72 MHz |
| LSE / RTC | PC14 / PC15 | 32.768 kHz crystal and backup domain |
| Entropy ADC | PA1 / ADC1_IN1 | 55.5-cycle sampling; DMA1 Channel 1 |
| USART1 TX/RX | PA9 / PA10 | 115200 8N1; TX on DMA1 Channel 4 |
| Passive buzzer | PA12 + TIM2_UP | Push-pull GPIO; DMA1 Channel 2 writes BSRR |
| SWD | PA13 / PA14 | SWDIO / SWCLK retained; JTAG disabled |
| LEDs 1..4 | PB0 / PB1 / PB10 / PB11 | Push-pull, initially low, fault diagnostics |
| Right / Down / Left / Up | PB4 / PB5 / PB6 / PB7 | Pull-up inputs; disabling JTAG frees PB4 |
| OLED SCK / MOSI | PA5 / PA7 | Default SPI1, mode 0, MSB first, 9 MHz, TX DMA1 Channel 3 |
| OLED DC / CS / RESET | PA6 / PA4 / PA8 | SPI control pins; PA6 is DC, not MISO |
| OLED SCL / SDA (I2C build) | PB8 / PB9 | Remapped I2C1, 400 kHz, TX DMA1 Channel 6, address 0x3C |
| Jump / Func / Enter / Back | PB12 / PB13 / PB14 / PB15 | Pull-up inputs |
| HAL timebase | TIM4 | 1 ms; no external pin |
| RTOS timebase | SysTick | FreeRTOS 1 kHz tick |

PA0, PA2–PA3, PA11, PA15, PB3, and PC13 are currently unallocated; the default SPI build also frees PB8/PB9. PB2 commonly participates in BOOT1 configuration. Both display variants retain the Left/Up buttons on PB6/PB7 with unchanged button wiring.

See [Hardware, DMA, and pinout](docs/hardware.md) for the complete clock and interrupt details.

## Build and Verification

The build needs CMake 3.22+, Ninja, a native C++20 compiler, and `arm-none-eabi-gcc/g++/objcopy/gcc-nm` on `PATH`. It has been verified with Arm GNU Toolchain 15.2.1. The CMake workflow runs on Windows, Linux, and macOS without Rust, Python, or PowerShell.

Run the complete verification pipeline:

```sh
cmake -P scripts/verify.cmake
```

Or build each target manually:

```sh
cmake --preset Debug-SPI
cmake --build --preset Debug-SPI

cmake --preset Release-I2C
cmake --build --preset Release-I2C

cmake -DMODE=TEST -P scripts/verify.cmake
cmake -DMODE=CHECK -P scripts/verify.cmake
cmake -DMODE=VERIFY -P scripts/verify.cmake
```

`Debug-SPI`, `Debug-I2C`, `Release-SPI`, and `Release-I2C` use separate build directories. The original `Debug`, `Release`, and `Analyze` aliases still default to SPI. A custom configure can use `-DGAMEBOX_OLED_INTERFACE=SPI` or `I2C`. The verification script accepts `-DMODE=BUILD|TEST|CHECK|VERIFY|CI`, `-DDISPLAY=SPI|I2C|ALL`, and `-DPROFILE=Debug|Release|ALL`. Its default CI mode runs both displays and profiles, native tests, and static analysis. `VERIFY` only audits existing packages, without building or connecting to hardware.

Build directories retain the original `.elf`, `.hex`, `.bin`, and `.map` filenames for existing tools. Every build also exports `gamebox-f103-spi.*` or `gamebox-f103-i2c.*` under `artifacts/firmware/debug/` or `artifacts/firmware/release/`. Each package contains ELF, BIN, HEX, SHA256 checksums, and a JSON manifest. Export validates the 32-bit ARM/little-endian ELF, Flash/SRAM ranges, stack/reset vectors, HEX checksums, and ELF/BIN/HEX agreement before replacing an existing package. Cached builds restore missing exported files. Run `cmake --build build/Release-SPI --target verify_artifacts` or `ctest --test-dir build/Release-SPI --output-on-failure` to audit that configuration separately.

CTest generates `build/HostTests/oled_menu_preview.bmp`. `Analyze-SPI/I2C` enable GCC `-fanalyzer` and export into `artifacts/analyze/`; the existing ETL `string_view` and deliberate halt-loop noise exclusions remain. GitHub Actions runs native tests on all three desktop platforms and uses Linux to build and verify the four firmware variants, analyze both interfaces, and test damaged packages and cached exports.

### Flashing and debugging (historical I2C validation)

The connection guidance below comes from earlier I2C firmware validation; it does not establish on-device validation of this SPI port. No device was flashed during this migration. Select the SPI/I2C image that matches the OLED wiring and identify the intended ST-LINK target before programming.

The historical debug setup used SWD at 140 kHz and reset the target when starting a session. Some clone probes can misreport target voltage. If the STM32F103 is identified, programming verifies, and GDB halts correctly, judge the session by those concrete results. If connection or verification fails, check 3.3 V, GND, SWDIO, SWCLK, and NRST rather than ignoring the voltage warning.

Keep `.settings/` tracked: it is shared STM32Cube metadata for the device, toolchain, and bundles. Do not ignore all of `.vscode/` either. Shared `settings.json`, `extensions.json`, `launch.json`, and `tasks.json` files belong in Git, while the existing `.gitignore` allowlist continues to exclude local editor state.

Earlier I2C firmware was tested with `STM32_Programmer_CLI` using low-speed connect-under-reset. The example below selects the default SPI Debug build; it must match the wiring:

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
  -f target/stm32f1x.cfg `
  -c "transport select swd" `
  -c "adapter speed 100" `
  -c "reset_config srst_only srst_nogate connect_assert_srst" `
  -c "program build/Release/stm32103c8t6_game-box.elf verify reset exit"
```

Load `target/stm32f1x.cfg` before setting `adapter speed 100`; otherwise the target script overwrites the low-speed setting with its default. If the probe does not carry NRST, a physical reset can leave the CPU in the RAM flash algorithm. Use a Cortex-M software reset instead:

```powershell
openocd -f interface/stlink.cfg `
  -f target/stm32f1x.cfg `
  -c "transport select swd" `
  -c "adapter speed 100" `
  -c "reset_config none" `
  -c "cortex_m reset_config sysresetreq" `
  -c "program build/Release/stm32103c8t6_game-box.elf verify reset exit"
```

At boot, USART1 emits `GAMEBOX FW2 UART-TX-DMA READY`, followed by `BTN <ms> <key> <event> <held_ms>` event lines. The System page reports minimum stack headroom across all four tasks, input/UART drops, OLED/UART errors, and OLED DMA transfer count.

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

Handwritten C++ builds with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wundef -Werror`. CubeMX output and `ThirdParty` keep their upstream style. Do not enable CubeMX's FreeRTOS middleware when regenerating code; this repository owns the kernel and resolves SVC/PendSV/SysTick vector ownership in the root CMake file. After regeneration, preserve the Core display-selection conditions, STM32F1 SCK startup handling, and FreeRTOS interrupt priorities.

Further reading:

- [Architecture](docs/architecture.md)
- [Hardware, DMA, and pinout](docs/hardware.md)
- [Button events and subscriptions](docs/input-events.md)
- [OLED menus, games, and motion](docs/ui-design.md)
- [Code review record](docs/code-review.md)
- [Dependency versions and sources](ThirdParty/UPSTREAM.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

## Current Resource Budget

Program Flash is limited to 62 KiB (`0x08000000..0x0800F800`), reserving the final 2 KiB for future persistence; settings currently remain in RTC Backup Registers. SRAM is limited to 20 KiB and the manifest includes the linker's 1 KiB stack reservation. Debug uses `-Os -g3 -flto` and Release uses `-Os -g0 -flto -DNDEBUG`, retaining the same optimization policy while Debug keeps symbols. Read the linker report and corresponding JSON package manifest for current measured usage.
