# oled-driver

[中文](README.md) | English

An allocation-free, platform-independent SSD1306 `no_std` driver with:

- blocking and async `embedded-hal` transports;
- six common SSD1306 panel geometries;
- exact per-framebuffer-byte dirty refresh;
- low-RAM page rendering;
- `embedded-graphics-core` integration;
- framebuffer-preserving fault reinitialization;
- validated refresh policies that cannot be mutated into an invalid state.

This crate is the portable core of the parent `example_oled` workspace. See:

- [Project documentation (English)](../../README_en.md)
- [Architecture and invariants](../../docs/architecture_en.md)
- [Hardware validation](../../docs/hardware-validation_en.md)


