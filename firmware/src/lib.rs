#![no_std]
#![forbid(unsafe_code)]

//! STM32F103C8 platform adapters and asynchronous services for GameBox.

#[cfg(all(feature = "oled-spi", feature = "oled-i2c"))]
compile_error!(
    "Select exactly one OLED interface: use --no-default-features --features oled-spi or oled-i2c"
);
#[cfg(not(any(feature = "oled-spi", feature = "oled-i2c")))]
compile_error!("Select an OLED interface with --features oled-spi or --features oled-i2c");

pub mod board;
pub mod platform;
pub mod services;
pub mod tasks;
