use crate::command::CommandBuffer;
use crate::{ConfigError, Controller, Rotation};

/// Supported SSD1306 panel layouts.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub enum DisplaySize {
    /// Common 128×64 panel.
    #[default]
    Display128x64,
    /// Common 128×32 panel.
    Display128x32,
    /// Common 96×16 panel.
    Display96x16,
    /// SSD1306B 72×40 panel using columns 28 through 99.
    Display72x40,
    /// 64×48 panel centered in the 128-column controller memory.
    Display64x48,
    /// 64×32 panel centered in the 128-column controller memory.
    Display64x32,
}

impl DisplaySize {
    /// Visible panel width in pixels.
    pub const fn width(self) -> usize {
        match self {
            Self::Display128x64 | Self::Display128x32 => 128,
            Self::Display96x16 => 96,
            Self::Display72x40 => 72,
            Self::Display64x48 | Self::Display64x32 => 64,
        }
    }

    /// Visible panel height in pixels.
    pub const fn height(self) -> usize {
        match self {
            Self::Display128x64 => 64,
            Self::Display128x32 | Self::Display64x32 => 32,
            Self::Display96x16 => 16,
            Self::Display72x40 => 40,
            Self::Display64x48 => 48,
        }
    }

    /// Number of controller pages used by the panel.
    pub const fn pages(self) -> usize {
        self.height() / 8
    }

    /// Required native framebuffer length.
    pub const fn buffer_len(self) -> usize {
        self.width() * self.pages()
    }

    pub(crate) const fn column_offset(self) -> u8 {
        match self {
            Self::Display72x40 => 28,
            Self::Display64x48 | Self::Display64x32 => 32,
            Self::Display128x64 | Self::Display128x32 | Self::Display96x16 => 0,
        }
    }

    pub(crate) const fn com_pin_config(self) -> u8 {
        match self {
            Self::Display128x64 | Self::Display72x40 | Self::Display64x48 | Self::Display64x32 => {
                0x12
            }
            Self::Display128x32 | Self::Display96x16 => 0x02,
        }
    }

    pub(crate) const fn requires_internal_iref(self) -> bool {
        matches!(self, Self::Display72x40)
    }
}

/// OLED panel power configuration.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub enum PowerMode {
    /// Generate the panel voltage with the SSD1306 charge pump.
    #[default]
    InternalChargePump,
    /// Use an externally supplied panel voltage.
    ExternalVcc,
}

/// SSD1306 display-clock presets.
///
/// Both presets use the fastest divide ratio (one). The oscillator setting
/// controls how quickly the controller scans its panel independently of the
/// host interface speed.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub enum DisplayClock {
    /// Datasheet reset oscillator setting (`0x80`).
    #[default]
    Default,
    /// Highest programmable oscillator setting (`0xf0`).
    Maximum,
}

impl DisplayClock {
    const fn register_value(self) -> u8 {
        match self {
            Self::Default => 0x80,
            Self::Maximum => 0xf0,
        }
    }
}

/// Configuration shared by all supported SSD1306 panel profiles.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Ssd1306Config {
    /// Initial contrast.
    pub contrast: u8,
    /// Logical panel rotation.
    pub rotation: Rotation,
    /// Initial inversion state.
    pub invert: bool,
    /// Panel power source.
    pub power_mode: PowerMode,
    /// Internal panel scan-clock preset.
    pub display_clock: DisplayClock,
}

impl Default for Ssd1306Config {
    fn default() -> Self {
        Self {
            contrast: 0x7f,
            rotation: Rotation::Rotate0,
            invert: false,
            power_mode: PowerMode::InternalChargePump,
            display_clock: DisplayClock::Default,
        }
    }
}

/// SSD1306 command encoder for the selected panel profile.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Ssd1306 {
    config: Ssd1306Config,
    size: DisplaySize,
}

impl Ssd1306 {
    /// Construct a controller.
    pub fn new(config: Ssd1306Config) -> Result<Self, ConfigError> {
        Self::new_with_size(DisplaySize::Display128x64, config)
    }

    /// Construct a controller for a supported panel layout.
    pub fn new_with_size(size: DisplaySize, config: Ssd1306Config) -> Result<Self, ConfigError> {
        match config.rotation {
            Rotation::Rotate0 | Rotation::Rotate180 => Ok(Self { config, size }),
            Rotation::Rotate90 | Rotation::Rotate270 => Err(ConfigError::UnsupportedRotation),
        }
    }

    /// Return the active controller configuration.
    pub const fn config(&self) -> Ssd1306Config {
        self.config
    }

    /// Return the selected panel layout.
    pub const fn size(&self) -> DisplaySize {
        self.size
    }
}

impl Default for Ssd1306 {
    fn default() -> Self {
        Self {
            config: Ssd1306Config {
                contrast: 0x7f,
                rotation: Rotation::Rotate0,
                invert: false,
                power_mode: PowerMode::InternalChargePump,
                display_clock: DisplayClock::Default,
            },
            size: DisplaySize::Display128x64,
        }
    }
}

impl Controller for Ssd1306 {
    #[cfg(feature = "graphics")]
    type Color = embedded_graphics_core::pixelcolor::BinaryColor;
    #[cfg(not(feature = "graphics"))]
    type Color = bool;

    fn width(&self) -> usize {
        self.size.width()
    }

    fn height(&self) -> usize {
        self.size.height()
    }

    fn pages(&self) -> usize {
        self.size.pages()
    }

    fn encode_init(&self, commands: &mut CommandBuffer) -> Result<(), ConfigError> {
        commands.extend(&[
            0xae, // Display off
            0xd5,
            self.config.display_clock.register_value(),
            0xa8,
            (self.size.height() - 1) as u8,
            0xd3,
            0x00, // Display offset
            0x40, // Start line
            0x8d,
            match self.config.power_mode {
                PowerMode::InternalChargePump => 0x14,
                PowerMode::ExternalVcc => 0x10,
            },
            0x20,
            0x00, // Horizontal addressing
        ])?;
        match self.config.rotation {
            Rotation::Rotate0 => commands.extend(&[0xa1, 0xc8])?,
            Rotation::Rotate180 => commands.extend(&[0xa0, 0xc0])?,
            Rotation::Rotate90 | Rotation::Rotate270 => {
                return Err(ConfigError::UnsupportedRotation);
            }
        }
        commands.extend(&[
            0xda,
            self.size.com_pin_config(),
            0x81,
            self.config.contrast,
            0xd9,
            match self.config.power_mode {
                PowerMode::InternalChargePump => 0xf1,
                PowerMode::ExternalVcc => 0x22,
            },
            0xdb,
            0x40, // VCOMH
            0xa4, // Resume RAM display
            if self.config.invert { 0xa7 } else { 0xa6 },
            0x2e, // Disable scrolling
        ])?;
        if self.size.requires_internal_iref() {
            commands.extend(&[0xad, 0x30])?;
        }
        Ok(())
    }

    fn encode_window(
        &self,
        column_start: u8,
        column_end: u8,
        page_start: u8,
        page_end: u8,
        commands: &mut CommandBuffer,
    ) -> Result<(), ConfigError> {
        let offset = self.size.column_offset();
        let column_start = column_start
            .checked_add(offset)
            .ok_or(ConfigError::GeometryMismatch)?;
        let column_end = column_end
            .checked_add(offset)
            .ok_or(ConfigError::GeometryMismatch)?;
        commands.extend(&[0x21, column_start, column_end, 0x22, page_start, page_end])
    }

    fn encode_display_on(&self, on: bool, commands: &mut CommandBuffer) -> Result<(), ConfigError> {
        commands.push(if on { 0xaf } else { 0xae })
    }

    fn encode_contrast(
        &self,
        contrast: u8,
        commands: &mut CommandBuffer,
    ) -> Result<(), ConfigError> {
        commands.extend(&[0x81, contrast])
    }

    fn encode_invert(&self, invert: bool, commands: &mut CommandBuffer) -> Result<(), ConfigError> {
        commands.push(if invert { 0xa7 } else { 0xa6 })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn maximum_display_clock_is_encoded_in_initialization() {
        let controller = Ssd1306::new(Ssd1306Config {
            display_clock: DisplayClock::Maximum,
            ..Ssd1306Config::default()
        })
        .unwrap();
        let mut commands = CommandBuffer::new();

        controller.encode_init(&mut commands).unwrap();

        assert_eq!(&commands.as_slice()[..3], &[0xae, 0xd5, 0xf0]);
    }
}
