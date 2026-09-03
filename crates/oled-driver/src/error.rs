use core::fmt;

/// Configuration and construction errors.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConfigError {
    /// An I²C address was outside the seven-bit address range.
    InvalidI2cAddress(u8),
    /// A full framebuffer did not match the selected panel geometry.
    InvalidFullBufferLength {
        /// Supplied buffer length.
        actual: usize,
        /// Required buffer length.
        required: usize,
    },
    /// A page framebuffer was empty, oversized or not page aligned.
    InvalidPageBufferLength {
        /// Supplied buffer length.
        actual: usize,
    },
    /// The controller geometry and framebuffer geometry do not match.
    GeometryMismatch,
    /// A fixed command buffer was too small for a controller command.
    CommandBufferOverflow,
    /// A flush policy used an invalid threshold.
    InvalidFlushPolicy,
    /// Native bitmap data did not match its declared dimensions.
    InvalidBitmapLength {
        /// Supplied byte count.
        actual: usize,
        /// Required byte count.
        expected: usize,
    },
    /// A native bitmap does not fit inside display memory.
    BitmapOutOfBounds,
    /// The selected rotation is not implemented by the first release.
    UnsupportedRotation,
}

/// Operations intentionally deferred by the first release.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UnsupportedOperation {
    /// The selected display rotation is unsupported.
    Rotation,
}

impl fmt::Display for ConfigError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidI2cAddress(address) => {
                write!(formatter, "invalid seven-bit I2C address 0x{address:02x}")
            }
            Self::InvalidFullBufferLength { actual, required } => write!(
                formatter,
                "full framebuffer has {actual} bytes; exactly {required} are required"
            ),
            Self::InvalidPageBufferLength { actual } => write!(
                formatter,
                "page framebuffer length {actual} is empty, oversized, or not page aligned"
            ),
            Self::GeometryMismatch => {
                formatter.write_str("controller and framebuffer geometry differ")
            }
            Self::CommandBufferOverflow => {
                formatter.write_str("controller command buffer capacity was exceeded")
            }
            Self::InvalidFlushPolicy => formatter.write_str("invalid flush policy"),
            Self::InvalidBitmapLength { actual, expected } => write!(
                formatter,
                "native bitmap has {actual} bytes; {expected} are required"
            ),
            Self::BitmapOutOfBounds => {
                formatter.write_str("native bitmap is outside display memory")
            }
            Self::UnsupportedRotation => {
                formatter.write_str("the selected display rotation is unsupported")
            }
        }
    }
}

impl core::error::Error for ConfigError {}

impl fmt::Display for UnsupportedOperation {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Rotation => formatter.write_str("display rotation"),
        }
    }
}

impl core::error::Error for UnsupportedOperation {}

/// Errors returned while operating a display.
#[derive(Debug, PartialEq, Eq)]
pub enum DriverError<E> {
    /// The underlying transport failed.
    Bus(E),
    /// Display construction or command encoding failed.
    Config(ConfigError),
    /// The display must be initialized before this operation.
    NotInitialized,
    /// The operation is intentionally unsupported.
    Unsupported(UnsupportedOperation),
}

impl<E> From<ConfigError> for DriverError<E> {
    fn from(value: ConfigError) -> Self {
        Self::Config(value)
    }
}

impl<E: fmt::Display> fmt::Display for DriverError<E> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Bus(error) => write!(formatter, "OLED transport error: {error}"),
            Self::Config(error) => write!(formatter, "OLED configuration error: {error}"),
            Self::NotInitialized => formatter.write_str("OLED is not initialized"),
            Self::Unsupported(operation) => {
                write!(formatter, "unsupported OLED operation: {operation}")
            }
        }
    }
}

impl<E> core::error::Error for DriverError<E>
where
    E: core::error::Error + 'static,
{
    fn source(&self) -> Option<&(dyn core::error::Error + 'static)> {
        match self {
            Self::Bus(error) => Some(error),
            Self::Config(error) => Some(error),
            Self::Unsupported(operation) => Some(operation),
            Self::NotInitialized => None,
        }
    }
}
