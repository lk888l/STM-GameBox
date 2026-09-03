//! Compact, validated user-facing settings.

/// Menu and decorative animation policy.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum MotionLevel {
    /// Full spring motion and the fastest pet animation.
    #[default]
    Full = 0,
    /// Gentler springs and a slower pet animation.
    Reduced = 1,
    /// Presentation state snaps directly to its target.
    Off = 2,
}

impl MotionLevel {
    /// Decode a persisted byte, falling back to full motion.
    #[must_use]
    pub const fn from_byte(value: u8) -> Self {
        match value {
            1 => Self::Reduced,
            2 => Self::Off,
            _ => Self::Full,
        }
    }

    /// Cycle through all motion levels.
    #[must_use]
    pub const fn next(self) -> Self {
        match self {
            Self::Full => Self::Reduced,
            Self::Reduced => Self::Off,
            Self::Off => Self::Full,
        }
    }
}

/// OLED contrast presets exposed in Settings.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum Brightness {
    /// Low contrast for dark rooms.
    Low = 0,
    /// Medium-low contrast.
    Medium = 1,
    /// Bright everyday preset.
    High = 2,
    /// Maximum product preset.
    #[default]
    Max = 3,
}

impl Brightness {
    /// Decode a persisted byte, falling back to maximum brightness.
    #[must_use]
    pub const fn from_byte(value: u8) -> Self {
        match value {
            0 => Self::Low,
            1 => Self::Medium,
            2 => Self::High,
            _ => Self::Max,
        }
    }

    /// Cycle through all brightness presets.
    #[must_use]
    pub const fn next(self) -> Self {
        match self {
            Self::Low => Self::Medium,
            Self::Medium => Self::High,
            Self::High => Self::Max,
            Self::Max => Self::Low,
        }
    }

    /// SSD1306 contrast command associated with this preset.
    #[must_use]
    pub const fn contrast(self) -> u8 {
        [0x28, 0x60, 0xa0, 0xcf][self as usize]
    }
}

/// Content shown at the top-left of the home card.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum HomeHeaderMode {
    /// Current RTC time.
    #[default]
    Time = 0,
    /// Current RTC date.
    Date = 1,
    /// Walking pixel pet.
    Pet = 2,
    /// Static GAMEBOX product title.
    Title = 3,
}

impl HomeHeaderMode {
    /// Decode a persisted byte, falling back to the time header.
    #[must_use]
    pub const fn from_byte(value: u8) -> Self {
        match value {
            1 => Self::Date,
            2 => Self::Pet,
            3 => Self::Title,
            _ => Self::Time,
        }
    }

    /// Cycle through all home-header modes.
    #[must_use]
    pub const fn next(self) -> Self {
        match self {
            Self::Time => Self::Date,
            Self::Date => Self::Pet,
            Self::Pet => Self::Title,
            Self::Title => Self::Time,
        }
    }
}

/// User settings retained across power cycles.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Settings {
    /// Whether button and game feedback sounds are enabled.
    pub sound_enabled: bool,
    /// Presentation animation level.
    pub motion: MotionLevel,
    /// OLED brightness preset.
    pub brightness: Brightness,
    /// Home-card header content.
    pub home_header: HomeHeaderMode,
}

impl Settings {
    /// Typed fields are valid by construction; retained for storage boundaries.
    #[must_use]
    pub const fn normalized(self) -> Self {
        self
    }
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            sound_enabled: true,
            motion: MotionLevel::Full,
            brightness: Brightness::Max,
            home_header: HomeHeaderMode::Time,
        }
    }
}
