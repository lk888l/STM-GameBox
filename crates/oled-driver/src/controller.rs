use crate::ConfigError;
use crate::command::CommandBuffer;

/// Supported logical display rotations.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub enum Rotation {
    /// Native panel orientation.
    #[default]
    Rotate0,
    /// Rotate the panel by 90 degrees.
    Rotate90,
    /// Rotate the panel by 180 degrees.
    Rotate180,
    /// Rotate the panel by 270 degrees.
    Rotate270,
}

/// Controller command encoder used by the display orchestration layer.
pub trait Controller {
    /// Pixel color accepted by the controller framebuffer.
    type Color;

    /// Logical display width.
    fn width(&self) -> usize;

    /// Logical display height.
    fn height(&self) -> usize;

    /// Physical page count.
    fn pages(&self) -> usize;

    /// Encode deterministic initialization commands, leaving the panel off.
    fn encode_init(&self, commands: &mut CommandBuffer) -> Result<(), ConfigError>;

    /// Encode a physical column/page window.
    fn encode_window(
        &self,
        column_start: u8,
        column_end: u8,
        page_start: u8,
        page_end: u8,
        commands: &mut CommandBuffer,
    ) -> Result<(), ConfigError>;

    /// Encode a display on/off command.
    fn encode_display_on(&self, on: bool, commands: &mut CommandBuffer) -> Result<(), ConfigError>;

    /// Encode a contrast command.
    fn encode_contrast(
        &self,
        contrast: u8,
        commands: &mut CommandBuffer,
    ) -> Result<(), ConfigError>;

    /// Encode normal/inverted pixel mode.
    fn encode_invert(&self, invert: bool, commands: &mut CommandBuffer) -> Result<(), ConfigError>;

    /// Check that geometry fits SSD1306-style page memory.
    fn validate_geometry(&self) -> Result<(), ConfigError> {
        if (1..=128).contains(&self.width())
            && (8..=64).contains(&self.height())
            && self.height().is_multiple_of(8)
            && self.pages() == self.height() / 8
        {
            Ok(())
        } else {
            Err(ConfigError::GeometryMismatch)
        }
    }
}
