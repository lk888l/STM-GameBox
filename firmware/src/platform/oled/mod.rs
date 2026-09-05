//! Compile-time selected OLED adapter; no bus-specific policy escapes to UI code.

use core::future::Future;

use embassy_time::{Duration, with_timeout};
use oled_driver::{
    AsyncTransport, BufferedDisplay, DISPLAY_BYTES, DISPLAY_PAGES, DISPLAY_WIDTH, DriverError,
    FlushReport, FullBuffer, Ssd1306, Ssd1306Config,
};

// lib.rs rejects both/neither feature. The fallback keeps invalid selections
// well-typed so the caller sees that explicit error, not missing-type errors.
#[cfg_attr(feature = "oled-i2c", path = "i2c.rs")]
#[cfg_attr(not(feature = "oled-i2c"), path = "spi.rs")]
mod backend;

pub use backend::Resources;

/// Transport name included in the startup log to identify the flashed variant.
pub const INTERFACE: &str = backend::INTERFACE;
/// Selected wire frequency, independent of the SWD debugging frequency.
pub const BUS_FREQUENCY_HZ: u32 = backend::BUS_FREQUENCY_HZ;
/// Producer cadence; actual throughput is limited by rendering and the bus.
pub const FRAME_INTERVAL: Duration = Duration::from_millis(5);

type BusError = <backend::Transport<'static> as AsyncTransport>::Error;

/// Failures from a bounded display operation.
#[derive(Debug)]
pub enum Error {
    /// The operation was cancelled after its backend-specific deadline.
    Timeout,
    /// The transport or controller rejected the operation.
    Driver(DriverError<BusError>),
}

/// One owner for the selected bus, panel reset, and front framebuffer.
///
/// Both interfaces use the same controller and framebuffer. Only transport,
/// wiring, interrupt bindings, reset/recovery, and flush policy are selected.
pub struct Oled<'d> {
    display: BufferedDisplay<'d, Ssd1306, backend::Transport<'d>>,
    reset: backend::Reset<'d>,
}

impl<'d> Oled<'d> {
    /// Consume only the selected display's pins and DMA channels.
    pub fn new(resources: Resources<'d>, memory: &'d mut [u8; DISPLAY_BYTES]) -> Self {
        let (transport, reset) = resources.into_parts();
        let front = FullBuffer::new(memory).expect("front framebuffer geometry is 128x64");
        let controller = Ssd1306::new(Ssd1306Config {
            display_clock: backend::DISPLAY_CLOCK,
            ..Ssd1306Config::default()
        })
        .expect("the fixed SSD1306 configuration is valid");
        let display = BufferedDisplay::new(controller, transport, front)
            .expect("SSD1306 geometry matches the framebuffer");
        Self { display, reset }
    }

    /// Initialize and clear the panel, including its backend's reset sequence.
    pub async fn init(&mut self) -> Result<FlushReport, Error> {
        self.reset.reset_panel().await;
        bounded(self.display.init_async()).await
    }

    /// Restore the bus only after any outstanding operation has been dropped.
    /// Taking &mut self prevents recovery from racing a borrowed DMA future.
    pub fn recover_bus(&mut self) {
        backend::recover_bus();
    }

    /// Reconnect and resend the complete current scene, without clearing it.
    pub async fn reinitialize(&mut self) -> Result<FlushReport, Error> {
        self.recover_bus();
        self.reset.reset_panel().await;
        bounded(self.display.reinitialize_async()).await
    }

    /// Update contrast without exposing the selected transport's API.
    pub fn set_contrast(
        &mut self,
        contrast: u8,
    ) -> impl Future<Output = Result<FlushReport, Error>> {
        bounded(self.display.set_contrast_async(contrast))
    }

    /// Stage the renderer's native page-layout scene and track changed bytes.
    pub fn stage_frame(&mut self, scene: &[u8; DISPLAY_BYTES]) {
        self.display
            .buffer_mut()
            .blit_native(0, 0, DISPLAY_WIDTH, DISPLAY_PAGES, scene)
            .expect("full-scene blit dimensions are compile-time constants");
    }

    /// Whether a staged scene still needs to reach the panel.
    #[must_use]
    pub fn is_dirty(&self) -> bool {
        self.display.buffer().is_dirty()
    }

    /// SPI submits one full frame; I²C saves wire time by sending dirty regions.
    pub fn flush(&mut self) -> impl Future<Output = Result<FlushReport, Error>> {
        #[cfg(feature = "oled-i2c")]
        let operation = self.display.flush_async();
        #[cfg(not(feature = "oled-i2c"))]
        let operation = self.display.flush_full_async();
        bounded(operation)
    }
}

async fn bounded(
    operation: impl Future<Output = Result<FlushReport, DriverError<BusError>>>,
) -> Result<FlushReport, Error> {
    with_timeout(backend::OPERATION_TIMEOUT, operation)
        .await
        .map_err(|_| Error::Timeout)?
        .map_err(Error::Driver)
}
