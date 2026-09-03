#![no_std]
#![forbid(unsafe_code)]
#![deny(missing_docs)]
#![allow(async_fn_in_trait)]

//! A layered, allocation-free OLED driver.
//!
//! The crate separates controller commands, transports, framebuffers and
//! display orchestration. The SSD1306 controller supports six common panel
//! geometries.
//!
//! ```
//! use oled_driver::{ConfigError, DISPLAY_BYTES, FullBuffer};
//!
//! let mut memory = [0_u8; DISPLAY_BYTES];
//! let mut framebuffer = FullBuffer::new(&mut memory)?;
//! framebuffer.set_pixel(7, 8, true);
//!
//! assert_eq!(framebuffer.as_bytes()[128 + 7], 0x01);
//! # Ok::<(), ConfigError>(())
//! ```

#[cfg(test)]
extern crate std;

mod command;
mod controller;
mod display;
mod error;
mod framebuffer;
mod report;
mod reset;
mod ssd1306;
mod transport;

pub use command::CommandBuffer;
pub use controller::{Controller, Rotation};
pub use display::{BufferedDisplay, DisplayParts, PagedDisplay};
pub use error::{ConfigError, DriverError, UnsupportedOperation};
pub use framebuffer::{
    DISPLAY_BYTES, DISPLAY_HEIGHT, DISPLAY_PAGES, DISPLAY_WIDTH, FlushPolicy, FullBuffer,
    PageBuffer, PageCanvas,
};
pub use report::{FlushReport, TransferReport};
pub use ssd1306::{DisplaySize, PowerMode, Ssd1306, Ssd1306Config};
pub use transport::{I2cTransport, TransportParts};

#[cfg(feature = "async")]
pub use reset::reset_async;
#[cfg(feature = "blocking")]
pub use reset::reset_blocking;
#[cfg(feature = "async")]
pub use transport::AsyncTransport;
#[cfg(feature = "blocking")]
pub use transport::BlockingTransport;
