//! Deterministic fixed-point motion primitives.

use crate::settings::AnimationSpeed;

const FRACTION_BITS: i32 = 8;
const ONE: i32 = 1 << FRACTION_BITS;

/// A seek-safe one-dimensional damped spring in signed Q24.8 format.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Spring {
    position: i32,
    velocity: i32,
    target: i32,
}

impl Spring {
    /// Create a spring already settled at an integer position.
    #[must_use]
    pub const fn settled(value: i16) -> Self {
        let fixed = value as i32 * ONE;
        Self {
            position: fixed,
            velocity: 0,
            target: fixed,
        }
    }

    /// Set a new integer target.
    pub const fn set_target(&mut self, value: i16) {
        self.target = value as i32 * ONE;
    }

    /// Move immediately to an integer value and clear momentum.
    pub const fn snap_to(&mut self, value: i16) {
        let fixed = value as i32 * ONE;
        self.position = fixed;
        self.target = fixed;
        self.velocity = 0;
    }

    /// Advance one nominal 60 Hz simulation step.
    ///
    /// Rendering may run at 30 Hz by calling this twice per frame. Keeping a
    /// fixed step makes the visual result deterministic and host-testable.
    pub fn step(&mut self, speed: AnimationSpeed) {
        let (stiffness, damping) = match speed {
            AnimationSpeed::Off => {
                self.position = self.target;
                self.velocity = 0;
                return;
            }
            AnimationSpeed::Slow => (34_i64, 206_i64),
            AnimationSpeed::Fast => (62_i64, 184_i64),
        };

        let displacement = i64::from(self.target - self.position);
        let acceleration = displacement * stiffness / 256;
        let velocity = (i64::from(self.velocity) + acceleration) * damping / 256;
        self.velocity = velocity.clamp(i64::from(i32::MIN), i64::from(i32::MAX)) as i32;
        self.position = self.position.saturating_add(self.velocity);

        if (self.target - self.position).abs() <= 8 && self.velocity.abs() <= 8 {
            self.position = self.target;
            self.velocity = 0;
        }
    }

    /// Current rounded integer position.
    #[must_use]
    pub const fn value(self) -> i16 {
        ((self.position + ONE / 2) / ONE) as i16
    }

    /// Whether both position and velocity have settled.
    #[must_use]
    pub const fn is_settled(self) -> bool {
        self.position == self.target && self.velocity == 0
    }
}

/// Animated values used by the menu renderer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MenuMotion {
    /// Selected row top edge.
    pub cursor_y: Spring,
    /// Selected row width.
    pub cursor_width: Spring,
    /// Content scroll offset.
    pub scroll_y: Spring,
    /// Incoming-page horizontal offset.
    pub page_x: Spring,
}

impl Default for MenuMotion {
    fn default() -> Self {
        Self {
            cursor_y: Spring::settled(14),
            cursor_width: Spring::settled(52),
            scroll_y: Spring::settled(0),
            page_x: Spring::settled(0),
        }
    }
}

impl MenuMotion {
    /// Advance every spring by one fixed step.
    pub fn step(&mut self, speed: AnimationSpeed) {
        self.cursor_y.step(speed);
        self.cursor_width.step(speed);
        self.scroll_y.step(speed);
        self.page_x.step(speed);
    }

    /// Whether any menu motion remains active.
    #[must_use]
    pub const fn is_active(self) -> bool {
        !self.cursor_y.is_settled()
            || !self.cursor_width.is_settled()
            || !self.scroll_y.is_settled()
            || !self.page_x.is_settled()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn spring_converges_without_float_math() {
        let mut spring = Spring::settled(0);
        spring.set_target(100);
        for _ in 0..180 {
            spring.step(AnimationSpeed::Fast);
        }
        assert_eq!(spring.value(), 100);
        assert!(spring.is_settled());
    }

    #[test]
    fn reduced_motion_snaps() {
        let mut spring = Spring::settled(-12);
        spring.set_target(87);
        spring.step(AnimationSpeed::Off);
        assert_eq!(spring.value(), 87);
        assert!(spring.is_settled());
    }
}
