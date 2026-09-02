#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_code)]
#![deny(missing_docs)]

//! Hardware-independent application core for the STM32 GameBox.
//!
//! The crate owns all deterministic state machines and rendering decisions.
//! It has no allocator, no executor dependency and can be tested on a host.

pub mod app;
pub mod button;
pub mod menu;
pub mod motion;
pub mod settings;
pub mod snake;
pub mod storage;
pub mod ui;

pub use app::{App, AppEffect, AppMode, RenderSchedule};
pub use button::{ButtonBank, ButtonConfig, Gesture, Key, KeyEvent, key_mask};
pub use settings::{AnimationSpeed, CursorStyle, Settings};
pub use storage::PersistentData;
