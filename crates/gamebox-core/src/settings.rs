//! User settings and their validated compact representation.

/// Menu animation speed.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum AnimationSpeed {
    /// Reduced motion. Transitions snap directly to their target.
    Off = 0,
    /// Gentle, slower transitions.
    Slow = 1,
    /// Responsive transitions.
    #[default]
    Fast = 2,
}

impl AnimationSpeed {
    /// Convert a persisted byte to a validated value.
    #[must_use]
    pub const fn from_byte(value: u8) -> Self {
        match value {
            0 => Self::Off,
            1 => Self::Slow,
            _ => Self::Fast,
        }
    }

    /// Select the next value, wrapping at the end.
    #[must_use]
    pub const fn next(self) -> Self {
        match self {
            Self::Off => Self::Slow,
            Self::Slow => Self::Fast,
            Self::Fast => Self::Off,
        }
    }

    /// Select the previous value, wrapping at the beginning.
    #[must_use]
    pub const fn previous(self) -> Self {
        match self {
            Self::Off => Self::Fast,
            Self::Slow => Self::Off,
            Self::Fast => Self::Slow,
        }
    }
}

/// Visual treatment of the selected menu row.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum CursorStyle {
    /// Filled, inverted capsule.
    #[default]
    Inverse = 0,
    /// One-pixel outline.
    Frame = 1,
    /// A small heart marker.
    Heart = 2,
}

impl CursorStyle {
    /// Convert a persisted byte to a validated value.
    #[must_use]
    pub const fn from_byte(value: u8) -> Self {
        match value {
            1 => Self::Frame,
            2 => Self::Heart,
            _ => Self::Inverse,
        }
    }

    /// Select the next value, wrapping at the end.
    #[must_use]
    pub const fn next(self) -> Self {
        match self {
            Self::Inverse => Self::Frame,
            Self::Frame => Self::Heart,
            Self::Heart => Self::Inverse,
        }
    }

    /// Select the previous value, wrapping at the beginning.
    #[must_use]
    pub const fn previous(self) -> Self {
        match self {
            Self::Inverse => Self::Heart,
            Self::Frame => Self::Inverse,
            Self::Heart => Self::Frame,
        }
    }
}

/// Validated user settings used by the application.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Settings {
    /// Whether UI and game sounds are enabled.
    pub sound_enabled: bool,
    /// Menu motion preset.
    pub animation_speed: AnimationSpeed,
    /// Selected-row style.
    pub cursor_style: CursorStyle,
    /// OLED contrast command value.
    pub contrast: u8,
    /// Seconds between standby redraws.
    pub standby_refresh_seconds: u16,
}

impl Settings {
    /// Smallest accepted standby refresh period.
    pub const MIN_STANDBY_REFRESH_SECONDS: u16 = 2;
    /// Largest accepted standby refresh period.
    pub const MAX_STANDBY_REFRESH_SECONDS: u16 = 60;

    /// Clamp fields that can originate from persistent storage.
    #[must_use]
    pub const fn normalized(mut self) -> Self {
        if self.contrast < 16 {
            self.contrast = 16;
        }
        if self.standby_refresh_seconds < Self::MIN_STANDBY_REFRESH_SECONDS {
            self.standby_refresh_seconds = Self::MIN_STANDBY_REFRESH_SECONDS;
        }
        if self.standby_refresh_seconds > Self::MAX_STANDBY_REFRESH_SECONDS {
            self.standby_refresh_seconds = Self::MAX_STANDBY_REFRESH_SECONDS;
        }
        self
    }
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            sound_enabled: true,
            animation_speed: AnimationSpeed::Fast,
            cursor_style: CursorStyle::Inverse,
            contrast: 0x9f,
            standby_refresh_seconds: 10,
        }
    }
}
