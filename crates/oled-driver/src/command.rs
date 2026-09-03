use crate::ConfigError;

const COMMAND_CAPACITY: usize = 32;

/// Fixed-capacity command encoder used by controller implementations.
///
/// SSD1306 command sequences are deliberately encoded without allocation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CommandBuffer {
    bytes: [u8; COMMAND_CAPACITY],
    len: usize,
}

impl CommandBuffer {
    /// Creates an empty command buffer.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            bytes: [0; COMMAND_CAPACITY],
            len: 0,
        }
    }

    /// Appends one command byte.
    pub fn push(&mut self, byte: u8) -> Result<(), ConfigError> {
        if self.len == self.bytes.len() {
            return Err(ConfigError::CommandBufferOverflow);
        }
        self.bytes[self.len] = byte;
        self.len += 1;
        Ok(())
    }

    /// Appends command bytes.
    pub fn extend(&mut self, bytes: &[u8]) -> Result<(), ConfigError> {
        if bytes.len() > self.bytes.len() - self.len {
            return Err(ConfigError::CommandBufferOverflow);
        }
        let end = self.len + bytes.len();
        self.bytes[self.len..end].copy_from_slice(bytes);
        self.len = end;
        Ok(())
    }

    /// Returns the encoded command bytes.
    #[must_use]
    pub fn as_slice(&self) -> &[u8] {
        &self.bytes[..self.len]
    }
}

impl Default for CommandBuffer {
    fn default() -> Self {
        Self::new()
    }
}
