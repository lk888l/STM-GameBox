#![no_std]
#![forbid(unsafe_code)]

//! STM32F103C8 platform adapters and asynchronous services for GameBox.

pub mod board;
pub mod platform;
pub mod services;
pub mod tasks;
