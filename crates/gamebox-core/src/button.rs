//! Debounced, non-blocking button gesture recognition.

/// Physical controls in their stable bit order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Key {
    /// Direction pad up.
    Up = 0,
    /// Direction pad down.
    Down = 1,
    /// Direction pad left.
    Left = 2,
    /// Direction pad right.
    Right = 3,
    /// Primary game action.
    Jump = 4,
    /// Secondary game action.
    Function = 5,
    /// Confirm/enter.
    Enter = 6,
    /// Back/cancel.
    Back = 7,
}

impl Key {
    /// Number of physical controls.
    pub const COUNT: usize = 8;

    /// Keys ordered like the input sample and stable-state bitmask.
    pub const ALL: [Self; Self::COUNT] = [
        Self::Up,
        Self::Down,
        Self::Left,
        Self::Right,
        Self::Jump,
        Self::Function,
        Self::Enter,
        Self::Back,
    ];

    /// Array index and bit number for this key.
    #[must_use]
    pub const fn index(self) -> usize {
        self as usize
    }
}

/// Return the stable-state bit associated with a key.
#[must_use]
pub const fn key_mask(key: Key) -> u8 {
    1_u8 << key.index()
}

/// High-level and raw-transition events emitted by a button state machine.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Gesture {
    /// Debounced press transition. Games use this for immediate response.
    Pressed,
    /// Debounced release transition.
    Released,
    /// One short press after the double-click window expires.
    Click,
    /// Two short presses within the configured window.
    DoubleClick,
    /// One press held through the long-press threshold.
    LongPress,
    /// Periodic event after a long press, useful for menu scrolling.
    Repeat,
}

/// Timestamped input event sent from the input adapter to the application.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct KeyEvent {
    /// Physical key.
    pub key: Key,
    /// Recognized gesture or transition.
    pub gesture: Gesture,
    /// Millisecond timestamp, allowed to wrap naturally.
    pub at_ms: u32,
    /// Stable duration of the associated press, where meaningful.
    pub held_ms: u32,
}

impl KeyEvent {
    /// Construct an event.
    #[must_use]
    pub const fn new(key: Key, gesture: Gesture, at_ms: u32) -> Self {
        Self {
            key,
            gesture,
            at_ms,
            held_ms: 0,
        }
    }

    /// Construct an event carrying a saturated stable hold duration.
    #[must_use]
    pub const fn with_held(key: Key, gesture: Gesture, at_ms: u32, held_ms: u32) -> Self {
        Self {
            key,
            gesture,
            at_ms,
            held_ms,
        }
    }
}

/// Timing policy shared by every physical key.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ButtonConfig {
    /// Time a raw level must remain unchanged before becoming stable.
    pub debounce_ms: u16,
    /// Maximum release-to-release interval for a double click.
    pub double_click_ms: u16,
    /// Hold duration that emits [`Gesture::LongPress`].
    pub long_press_ms: u16,
    /// Delay from long press to the first repeat.
    pub repeat_delay_ms: u16,
    /// Interval between repeat events.
    pub repeat_interval_ms: u16,
}

impl ButtonConfig {
    /// Validate timing relationships that the recognizer relies on.
    pub const fn validate(self) -> Result<Self, ButtonConfigError> {
        if self.debounce_ms == 0 {
            return Err(ButtonConfigError::ZeroDebounce);
        }
        if self.double_click_ms <= self.debounce_ms.saturating_mul(2) {
            return Err(ButtonConfigError::DoubleClickTooShort);
        }
        if self.long_press_ms <= self.double_click_ms {
            return Err(ButtonConfigError::LongPressTooShort);
        }
        if self.repeat_delay_ms == 0 || self.repeat_interval_ms == 0 {
            return Err(ButtonConfigError::ZeroRepeatTiming);
        }
        Ok(self)
    }
}

impl Default for ButtonConfig {
    fn default() -> Self {
        Self {
            debounce_ms: 20,
            double_click_ms: 280,
            long_press_ms: 650,
            repeat_delay_ms: 110,
            repeat_interval_ms: 90,
        }
    }
}

/// Invalid button timing policy.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ButtonConfigError {
    /// A zero debounce duration would pass contact bounce directly through.
    ZeroDebounce,
    /// The double-click window cannot contain two debounced transitions.
    DoubleClickTooShort,
    /// Long press must happen after the double-click decision window.
    LongPressTooShort,
    /// Repeat delay and interval must both be non-zero.
    ZeroRepeatTiming,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct ButtonState {
    raw_pressed: bool,
    stable_pressed: bool,
    raw_changed_at: u32,
    pressed_at: u32,
    pending_release_at: u32,
    pending_held_ms: u32,
    repeat_at: u32,
    pending_click: bool,
    long_sent: bool,
}

impl ButtonState {
    const fn new() -> Self {
        Self {
            raw_pressed: false,
            stable_pressed: false,
            raw_changed_at: 0,
            pressed_at: 0,
            pending_release_at: 0,
            pending_held_ms: 0,
            repeat_at: 0,
            pending_click: false,
            long_sent: false,
        }
    }

    fn update(
        &mut self,
        key: Key,
        now_ms: u32,
        raw_pressed: bool,
        config: ButtonConfig,
        emit: &mut impl FnMut(KeyEvent),
    ) {
        if raw_pressed != self.raw_pressed {
            self.raw_pressed = raw_pressed;
            self.raw_changed_at = now_ms;
        }

        if self.stable_pressed != self.raw_pressed
            && elapsed(now_ms, self.raw_changed_at) >= u32::from(config.debounce_ms)
        {
            self.stable_pressed = self.raw_pressed;
            if self.stable_pressed {
                self.pressed_at = now_ms;
                self.long_sent = false;
                emit(KeyEvent::new(key, Gesture::Pressed, now_ms));
            } else {
                let held_ms = elapsed(now_ms, self.pressed_at);
                emit(KeyEvent::with_held(key, Gesture::Released, now_ms, held_ms));
                if self.long_sent {
                    self.pending_click = false;
                } else if self.pending_click {
                    if elapsed(now_ms, self.pending_release_at) < u32::from(config.double_click_ms)
                    {
                        self.pending_click = false;
                        emit(KeyEvent::with_held(
                            key,
                            Gesture::DoubleClick,
                            now_ms,
                            held_ms,
                        ));
                    } else {
                        emit(KeyEvent::with_held(
                            key,
                            Gesture::Click,
                            self.pending_release_at,
                            self.pending_held_ms,
                        ));
                        self.pending_release_at = now_ms;
                        self.pending_held_ms = held_ms;
                    }
                } else {
                    self.pending_click = true;
                    self.pending_release_at = now_ms;
                    self.pending_held_ms = held_ms;
                }
            }
        }

        if self.stable_pressed {
            if !self.long_sent
                && elapsed(now_ms, self.pressed_at) >= u32::from(config.long_press_ms)
            {
                self.long_sent = true;
                self.repeat_at = now_ms.wrapping_add(u32::from(config.repeat_delay_ms));
                emit(KeyEvent::with_held(
                    key,
                    Gesture::LongPress,
                    now_ms,
                    elapsed(now_ms, self.pressed_at),
                ));
            } else if self.long_sent && deadline_reached(now_ms, self.repeat_at) {
                while deadline_reached(now_ms, self.repeat_at) {
                    self.repeat_at = self
                        .repeat_at
                        .wrapping_add(u32::from(config.repeat_interval_ms));
                }
                let held_ms = elapsed(now_ms, self.pressed_at);
                emit(KeyEvent::with_held(key, Gesture::Repeat, now_ms, held_ms));
            }
        }

        // Independent of the stable level: a following long press must not
        // swallow the preceding short click when the double window expires.
        if self.pending_click
            && elapsed(now_ms, self.pending_release_at) >= u32::from(config.double_click_ms)
        {
            self.pending_click = false;
            emit(KeyEvent::with_held(
                key,
                Gesture::Click,
                self.pending_release_at,
                self.pending_held_ms,
            ));
        }
    }
}

const fn elapsed(now: u32, since: u32) -> u32 {
    now.wrapping_sub(since)
}

const fn deadline_reached(now: u32, deadline: u32) -> bool {
    now.wrapping_sub(deadline) < 0x8000_0000
}

/// Eight independent button recognizers driven by one periodic sample.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ButtonBank {
    config: ButtonConfig,
    states: [ButtonState; Key::COUNT],
}

impl ButtonBank {
    /// Create a bank after validating its timing policy.
    pub const fn new(config: ButtonConfig) -> Result<Self, ButtonConfigError> {
        match config.validate() {
            Ok(config) => Ok(Self {
                config,
                states: [ButtonState::new(); Key::COUNT],
            }),
            Err(error) => Err(error),
        }
    }

    /// Sample all active-low inputs after converting them to `pressed = true`.
    ///
    /// The callback is invoked synchronously for every event produced by this
    /// sample. It must not block; embedded adapters normally use `try_send`.
    pub fn update(
        &mut self,
        now_ms: u32,
        pressed: [bool; Key::COUNT],
        mut emit: impl FnMut(KeyEvent),
    ) {
        for key in Key::ALL {
            self.states[key.index()].update(
                key,
                now_ms,
                pressed[key.index()],
                self.config,
                &mut emit,
            );
        }
    }

    /// Bitmask of debounced keys currently held down.
    #[must_use]
    pub fn stable_mask(&self) -> u8 {
        let mut mask = 0;
        for key in Key::ALL {
            if self.states[key.index()].stable_pressed {
                mask |= key_mask(key);
            }
        }
        mask
    }
}

impl Default for ButtonBank {
    fn default() -> Self {
        Self::new(ButtonConfig::default()).expect("default button timing is valid")
    }
}

#[cfg(test)]
mod tests {
    use std::vec::Vec;

    use super::*;

    fn run(bank: &mut ButtonBank, from: u32, to: u32, pressed: [bool; 8]) -> Vec<KeyEvent> {
        let mut events = Vec::new();
        for now in from..=to {
            bank.update(now, pressed, |event| events.push(event));
        }
        events
    }

    #[test]
    fn single_click_waits_for_double_click_window() {
        let mut bank = ButtonBank::default();
        let mut events = run(
            &mut bank,
            0,
            20,
            [true, false, false, false, false, false, false, false],
        );
        events.extend(run(&mut bank, 21, 50, [false; 8]));
        assert!(events.iter().any(|event| event.gesture == Gesture::Pressed));
        assert!(
            events
                .iter()
                .any(|event| event.gesture == Gesture::Released)
        );
        assert!(!events.iter().any(|event| event.gesture == Gesture::Click));
        events.extend(run(&mut bank, 51, 330, [false; 8]));
        assert_eq!(
            events
                .iter()
                .filter(|event| event.gesture == Gesture::Click)
                .count(),
            1
        );
    }

    #[test]
    fn two_short_presses_emit_double_click_not_clicks() {
        let mut bank = ButtonBank::default();
        let mut all = Vec::new();
        all.extend(run(
            &mut bank,
            0,
            20,
            [false, false, false, false, false, false, true, false],
        ));
        all.extend(run(&mut bank, 21, 70, [false; 8]));
        all.extend(run(
            &mut bank,
            71,
            110,
            [false, false, false, false, false, false, true, false],
        ));
        all.extend(run(&mut bank, 111, 150, [false; 8]));
        all.extend(run(&mut bank, 151, 500, [false; 8]));
        assert_eq!(
            all.iter()
                .filter(|event| event.gesture == Gesture::DoubleClick)
                .count(),
            1
        );
        assert!(!all.iter().any(|event| event.gesture == Gesture::Click));
    }

    #[test]
    fn long_press_is_single_then_repeats() {
        let mut bank = ButtonBank::default();
        let events = run(
            &mut bank,
            0,
            1_200,
            [false, true, false, false, false, false, false, false],
        );
        assert_eq!(
            events
                .iter()
                .filter(|event| event.gesture == Gesture::LongPress)
                .count(),
            1
        );
        assert!(events.iter().any(|event| event.gesture == Gesture::Repeat));
        assert!(!events.iter().any(|event| event.gesture == Gesture::Click));
    }

    #[test]
    fn contact_bounce_never_becomes_a_press() {
        let mut bank = ButtonBank::default();
        let mut events = Vec::new();
        for now in 0..100 {
            bank.update(
                now,
                [now % 4 < 2, false, false, false, false, false, false, false],
                |event| {
                    events.push(event);
                },
            );
        }
        assert!(events.is_empty());
        assert_eq!(bank.stable_mask(), 0);
    }

    #[test]
    fn each_key_has_independent_state() {
        let mut bank = ButtonBank::default();
        let events = run(
            &mut bank,
            0,
            30,
            [true, false, false, true, false, false, false, false],
        );
        assert!(events.contains(&KeyEvent::new(Key::Up, Gesture::Pressed, 20)));
        assert!(events.contains(&KeyEvent::new(Key::Right, Gesture::Pressed, 20)));
        assert_eq!(bank.stable_mask(), key_mask(Key::Up) | key_mask(Key::Right));
    }

    #[test]
    fn elapsed_math_survives_timestamp_wrap() {
        let mut bank = ButtonBank::default();
        let mut events = Vec::new();
        for offset in 0..40 {
            let now = u32::MAX.wrapping_sub(10).wrapping_add(offset);
            bank.update(
                now,
                [false, false, true, false, false, false, false, false],
                |event| {
                    events.push(event);
                },
            );
        }
        assert!(
            events
                .iter()
                .any(|event| { event.key == Key::Left && event.gesture == Gesture::Pressed })
        );
    }

    #[test]
    fn click_is_not_lost_when_the_following_press_becomes_long() {
        let mut bank = ButtonBank::default();
        let mut events = run(
            &mut bank,
            0,
            20,
            [true, false, false, false, false, false, false, false],
        );
        events.extend(run(&mut bank, 21, 50, [false; 8]));
        events.extend(run(
            &mut bank,
            51,
            800,
            [true, false, false, false, false, false, false, false],
        ));
        events.extend(run(&mut bank, 801, 830, [false; 8]));
        assert_eq!(
            events
                .iter()
                .filter(|event| event.gesture == Gesture::Click)
                .count(),
            1
        );
        assert_eq!(
            events
                .iter()
                .filter(|event| event.gesture == Gesture::LongPress)
                .count(),
            1
        );
    }

    #[test]
    fn double_click_window_is_exclusive_at_280_ms() {
        let mut bank = ButtonBank::default();
        let mut events = run(
            &mut bank,
            0,
            20,
            [true, false, false, false, false, false, false, false],
        );
        events.extend(run(&mut bank, 21, 50, [false; 8]));
        events.extend(run(
            &mut bank,
            51,
            300,
            [true, false, false, false, false, false, false, false],
        ));
        events.extend(run(&mut bank, 301, 601, [false; 8]));
        assert_eq!(
            events
                .iter()
                .filter(|event| event.gesture == Gesture::DoubleClick)
                .count(),
            0
        );
        assert_eq!(
            events
                .iter()
                .filter(|event| event.gesture == Gesture::Click)
                .count(),
            2
        );
    }
}
