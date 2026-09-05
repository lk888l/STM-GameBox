# oled-driver

[中文](README.md) | English

An allocation-free, platform-independent SSD1306 `no_std` driver with:

- blocking and async `embedded-hal` transports for I²C and four-wire SPI with D/C# and CS#;
- six common SSD1306 panel geometries;
- exact per-framebuffer-byte dirty refresh;
- low-RAM page rendering;
- `embedded-graphics-core` integration;
- framebuffer-preserving fault reinitialization;
- validated refresh policies that cannot be mutated into an invalid state.

This crate is the portable display driver in the STM-GameBox workspace. Firmware
selects its I²C / SPI backend in `firmware/src/platform/oled`; pin and DMA details
stay outside this crate. See the project documentation (Chinese):

- [Project documentation](../../README.md)
- [Architecture and invariants](../../docs/architecture.md)
- [Hardware validation](../../docs/hardware-validation.md)


