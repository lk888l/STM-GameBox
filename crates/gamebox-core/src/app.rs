//! Top-level allocation-free product state machine.

use crate::{
    button::{Gesture, Key, KeyEvent, key_mask},
    calendar::{DateTime, days_in_month},
    games::{AirRaid, Dino, GamePhase, Pong, Tetris},
    menu::{Action, MenuModel, View, is_game, is_menu, menu_for},
    motion::MenuMotion,
    settings::Settings,
    snake::{Direction, Snake},
    storage::PersistentData,
};

const HISTORY_CAPACITY: usize = 8;
const MOTION_STEP_INTERVAL_MS: u32 = 8;
const MAX_MOTION_CATCHUP_STEPS: u32 = 8;

/// Coarse shell state; active content is identified by [`App::view`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum AppMode {
    /// Short, skippable startup card.
    Boot = 0,
    /// Normal product UI.
    Active = 1,
}

/// Hardware-facing effect produced by deterministic application logic.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AppEffect {
    /// No adapter work is required.
    None,
    /// Coalescible persistent snapshot.
    Persist(PersistentData),
    /// Atomically update the hardware RTC.
    SetClock(DateTime),
    /// Play a bounded tone through the buzzer owner.
    Tone {
        /// Tone frequency in hertz.
        frequency_hz: u16,
        /// Tone duration in milliseconds.
        duration_ms: u16,
    },
}

/// Whether the display needs a fresh frame.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RenderSchedule {
    /// Render this cycle.
    Render,
    /// No visible state changed.
    Idle,
}

/// Runtime counters shown on the System page.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct SystemStats {
    /// Main input queue overwrites.
    pub input_drops: u32,
    /// UART diagnostic queue overwrites.
    pub uart_drops: u32,
    /// OLED transfer or recovery failures.
    pub oled_errors: u32,
    /// UART DMA failures.
    pub uart_errors: u32,
    /// Completed OLED flush transfers.
    pub oled_transfers: u32,
    /// Failed Flash commits.
    pub storage_errors: u32,
}

/// Monotonic stopwatch state.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
struct Stopwatch {
    running: bool,
    started_ms: u32,
    accumulated_ms: u32,
}

impl Stopwatch {
    fn toggle(&mut self, now_ms: u32) {
        if self.running {
            self.accumulated_ms = self
                .accumulated_ms
                .wrapping_add(now_ms.wrapping_sub(self.started_ms));
        } else {
            self.started_ms = now_ms;
        }
        self.running = !self.running;
    }

    const fn reset(&mut self) {
        self.running = false;
        self.accumulated_ms = 0;
    }

    const fn elapsed_ms(self, now_ms: u32) -> u32 {
        if self.running {
            self.accumulated_ms
                .wrapping_add(now_ms.wrapping_sub(self.started_ms))
        } else {
            self.accumulated_ms
        }
    }
}

/// Complete application model shared by firmware and host tests.
pub struct App {
    mode: AppMode,
    mode_started_ms: u32,
    view: View,
    previous_view: View,
    page_forward: bool,
    history: [View; HISTORY_CAPACITY],
    history_len: u8,
    menu: MenuModel,
    motion: MenuMotion,
    motion_step_at_ms: u32,
    carousel_previous: u8,
    carousel_direction: i8,
    persistent: PersistentData,
    entropy: u32,
    event_count: u32,
    last_event: Option<KeyEvent>,
    held_keys: u8,
    dino: Dino,
    snake: Snake,
    air_raid: AirRaid,
    tetris: Tetris,
    pong: Pong,
    piano_note: u8,
    stopwatch: Stopwatch,
    countdown_running: bool,
    countdown_seconds: u32,
    countdown_deadline_ms: u32,
    rtc: DateTime,
    rtc_valid: bool,
    clock_edit: DateTime,
    clock_editing: bool,
    clock_field: u8,
    toast: Option<&'static str>,
    toast_until_ms: u32,
    stats: SystemStats,
    pending_persist: Option<PersistentData>,
    pending_tone: Option<(u16, u16)>,
    feedback_requested: bool,
    input_guard_key: Option<Key>,
    input_guard_was_long: bool,
}

impl App {
    /// Skippable boot-card duration.
    pub const BOOT_DURATION_MS: u32 = 700;

    /// Build the complete application with persisted settings and one entropy seed.
    #[must_use]
    pub fn new(now_ms: u32, persistent: PersistentData, entropy: u32) -> Self {
        let seed = entropy.max(1);
        Self {
            mode: AppMode::Boot,
            mode_started_ms: now_ms,
            view: View::Home,
            previous_view: View::Home,
            page_forward: true,
            history: [View::Home; HISTORY_CAPACITY],
            history_len: 0,
            menu: MenuModel::default(),
            motion: MenuMotion::default(),
            motion_step_at_ms: now_ms,
            carousel_previous: 0,
            carousel_direction: 1,
            persistent,
            entropy: seed,
            event_count: 0,
            last_event: None,
            held_keys: 0,
            dino: Dino::new(seed ^ 0xd170_2026),
            snake: Snake::new(seed ^ 0x51a2_e11f),
            air_raid: AirRaid::new(seed ^ 0xa17a_1d55),
            tetris: Tetris::new(seed ^ 0x7e71_5123),
            pong: Pong::new(seed ^ 0xb011_2026),
            piano_note: u8::MAX,
            stopwatch: Stopwatch::default(),
            countdown_running: false,
            countdown_seconds: 300,
            countdown_deadline_ms: 0,
            rtc: DateTime::default(),
            rtc_valid: false,
            clock_edit: DateTime::default(),
            clock_editing: false,
            clock_field: 0,
            toast: None,
            toast_until_ms: 0,
            stats: SystemStats::default(),
            pending_persist: None,
            pending_tone: None,
            feedback_requested: false,
            input_guard_key: None,
            input_guard_was_long: false,
        }
    }

    /// Current coarse shell mode.
    #[must_use]
    pub const fn mode(&self) -> AppMode {
        self.mode
    }

    /// Current product view.
    #[must_use]
    pub const fn view(&self) -> View {
        self.view
    }

    /// Outgoing view while a page spring is active.
    #[must_use]
    pub const fn previous_view(&self) -> View {
        self.previous_view
    }

    /// Whether the current page entered from the right.
    #[must_use]
    pub const fn page_forward(&self) -> bool {
        self.page_forward
    }

    /// Menu selection state.
    #[must_use]
    pub const fn menu(&self) -> &MenuModel {
        &self.menu
    }

    /// Current fixed-point presentation state.
    #[must_use]
    pub const fn motion(&self) -> &MenuMotion {
        &self.motion
    }

    /// Previous home-card index during a carousel transition.
    #[must_use]
    pub const fn carousel_previous(&self) -> u8 {
        self.carousel_previous
    }

    /// Signed home-card transition direction.
    #[must_use]
    pub const fn carousel_direction(&self) -> i8 {
        self.carousel_direction
    }

    /// Complete durable state snapshot.
    #[must_use]
    pub const fn persistent_data(&self) -> PersistentData {
        self.persistent
    }

    /// Latest RTC snapshot supplied by the platform adapter.
    #[must_use]
    pub const fn clock(&self) -> Option<DateTime> {
        if self.rtc_valid { Some(self.rtc) } else { None }
    }

    /// Clock value currently shown, including uncommitted edits.
    #[must_use]
    pub const fn shown_clock(&self) -> Option<DateTime> {
        if self.clock_editing {
            Some(self.clock_edit)
        } else {
            self.clock()
        }
    }

    /// Whether the clock editor is active.
    #[must_use]
    pub const fn clock_editing(&self) -> bool {
        self.clock_editing
    }

    /// Selected clock field: hour, minute, year, month, or day.
    #[must_use]
    pub const fn clock_field(&self) -> u8 {
        self.clock_field
    }

    /// Refresh the RTC snapshot without perturbing an in-progress edit.
    pub fn set_clock_snapshot(&mut self, value: Option<DateTime>) {
        if let Some(value) = value {
            self.rtc = value;
            self.rtc_valid = true;
        } else {
            self.rtc_valid = false;
        }
    }

    /// Replace runtime diagnostics shown by the System page.
    pub const fn set_system_stats(&mut self, stats: SystemStats) {
        self.stats = stats;
    }

    /// Runtime diagnostics snapshot.
    #[must_use]
    pub const fn system_stats(&self) -> SystemStats {
        self.stats
    }

    /// Latest semantic input event.
    #[must_use]
    pub const fn last_event(&self) -> Option<KeyEvent> {
        self.last_event
    }

    /// Number of semantic input events observed since boot.
    #[must_use]
    pub const fn event_count(&self) -> u32 {
        self.event_count
    }

    /// Debounced held-key mask supplied at the latest frame.
    #[must_use]
    pub const fn held_keys(&self) -> u8 {
        self.held_keys
    }

    /// Current toast while its deadline has not elapsed.
    #[must_use]
    pub const fn toast(&self, now_ms: u32) -> Option<&'static str> {
        if self.toast.is_some() && !deadline_reached(now_ms, self.toast_until_ms) {
            self.toast
        } else {
            None
        }
    }

    /// Dinosaur game model.
    #[must_use]
    pub const fn dino(&self) -> &Dino {
        &self.dino
    }
    /// Snake game model.
    #[must_use]
    pub const fn snake(&self) -> &Snake {
        &self.snake
    }
    /// Air Raid game model.
    #[must_use]
    pub const fn air_raid(&self) -> &AirRaid {
        &self.air_raid
    }
    /// Tetris game model.
    #[must_use]
    pub const fn tetris(&self) -> &Tetris {
        &self.tetris
    }
    /// Pong game model.
    #[must_use]
    pub const fn pong(&self) -> &Pong {
        &self.pong
    }

    /// Last piano key selected, or `None` before the first note.
    #[must_use]
    pub const fn piano_note(&self) -> Option<u8> {
        if self.piano_note < 8 {
            Some(self.piano_note)
        } else {
            None
        }
    }

    /// Whether the stopwatch is currently running.
    #[must_use]
    pub const fn stopwatch_running(&self) -> bool {
        self.stopwatch.running
    }

    /// Stopwatch elapsed milliseconds at a monotonic timestamp.
    #[must_use]
    pub const fn stopwatch_elapsed_ms(&self, now_ms: u32) -> u32 {
        self.stopwatch.elapsed_ms(now_ms)
    }

    /// Whether the countdown is running.
    #[must_use]
    pub const fn countdown_running(&self) -> bool {
        self.countdown_running
    }

    /// Rounded-up countdown seconds remaining at a timestamp.
    #[must_use]
    pub const fn countdown_remaining(&self, now_ms: u32) -> u32 {
        if !self.countdown_running {
            return self.countdown_seconds;
        }
        let remaining = self.countdown_deadline_ms.wrapping_sub(now_ms);
        if remaining < 0x8000_0000 {
            remaining.saturating_add(999) / 1_000
        } else {
            0
        }
    }

    /// Process one semantic button event.
    pub fn handle_event(&mut self, event: KeyEvent) -> AppEffect {
        self.handle_event_at(event, event.at_ms)
    }

    /// Process one semantic button event at the current UI-service time.
    ///
    /// Click events retain their physical release timestamp for diagnostics,
    /// but UI actions occur when the double-click decision window expires.
    pub fn handle_event_at(&mut self, event: KeyEvent, now_ms: u32) -> AppEffect {
        self.feedback_requested = false;
        self.last_event = Some(event);
        self.event_count = self.event_count.wrapping_add(1);
        self.mix_entropy(event.at_ms ^ (u32::from(event.key as u8) << 24));
        let event = KeyEvent {
            at_ms: now_ms,
            ..event
        };

        if self.mode == AppMode::Boot {
            if event.gesture == Gesture::Pressed {
                self.mode = AppMode::Active;
                self.mode_started_ms = event.at_ms;
                self.begin_input_guard(event.key);
            }
            return AppEffect::None;
        }
        if self.consume_input_guard(event) {
            return AppEffect::None;
        }

        if self.view == View::Piano {
            return self.handle_piano(event);
        }
        if self.view == View::Clock && self.clock_editing {
            if event.key == Key::Back && event.gesture == Gesture::Pressed {
                self.clock_editing = false;
                self.show_toast("CANCELLED", event.at_ms, 1_200);
                self.request_feedback();
                return AppEffect::None;
            }
            return self.handle_clock(event);
        }
        if !is_game(self.view) && event.key == Key::Function && event.gesture == Gesture::LongPress
        {
            self.open_view(View::InputLab, event.at_ms, true);
            return AppEffect::None;
        }
        if !is_game(self.view)
            && event.key == Key::Function
            && event.gesture == Gesture::DoubleClick
        {
            self.open_view(View::Clock, event.at_ms, true);
            return AppEffect::None;
        }
        if event.key == Key::Back && event.gesture == Gesture::Pressed {
            self.navigate_back();
            return AppEffect::None;
        }

        if is_menu(self.view) {
            self.handle_menu(event)
        } else {
            match self.view {
                View::Snake => self.handle_snake(event),
                View::Dino => self.handle_dino(event),
                View::AirRaid => self.handle_air_raid(event),
                View::Tetris => self.handle_tetris(event),
                View::Pong => self.handle_pong(event),
                View::Stopwatch => self.handle_stopwatch(event),
                View::Countdown => self.handle_countdown(event),
                View::Clock => self.handle_clock(event),
                _ => AppEffect::None,
            }
        }
    }

    /// Advance animation, games, and timers for one render tick.
    pub fn tick(&mut self, now_ms: u32, held_keys: u8) -> RenderSchedule {
        self.held_keys = held_keys;
        self.advance_motion(now_ms);
        if self.mode == AppMode::Boot {
            if now_ms.wrapping_sub(self.mode_started_ms) >= Self::BOOT_DURATION_MS {
                self.mode = AppMode::Active;
                self.mode_started_ms = now_ms;
                return RenderSchedule::Render;
            }
            return RenderSchedule::Render;
        }

        match self.view {
            View::Snake => {
                self.snake.update(now_ms);
                if self.snake.phase() == GamePhase::GameOver
                    && self.snake.score() > self.persistent.snake_high_score
                {
                    self.persistent.snake_high_score = self.snake.score();
                    self.pending_persist = Some(self.persistent);
                }
            }
            View::Dino => self.dino.update(now_ms),
            View::AirRaid => {
                let up = held_keys & key_mask(Key::Up) != 0;
                let down = held_keys & key_mask(Key::Down) != 0;
                self.air_raid.set_vertical(if up == down {
                    0
                } else if up {
                    -1
                } else {
                    1
                });
                self.air_raid.update(now_ms);
            }
            View::Tetris => self.tetris.update(now_ms),
            View::Pong => {
                let left_up = held_keys & key_mask(Key::Up) != 0;
                let left_down = held_keys & key_mask(Key::Down) != 0;
                let right_up = held_keys & key_mask(Key::Jump) != 0;
                let right_down = held_keys & key_mask(Key::Function) != 0;
                self.pong.set_left_direction(axis(left_up, left_down));
                self.pong.set_right_direction(axis(right_up, right_down));
                self.pong.update(now_ms);
            }
            _ => {}
        }

        if self.countdown_running && deadline_reached(now_ms, self.countdown_deadline_ms) {
            self.countdown_running = false;
            self.countdown_seconds = 0;
            self.show_toast("TIME UP", now_ms, 2_500);
            if self.persistent.settings.sound_enabled {
                self.pending_tone = Some((880, 600));
            }
        }
        RenderSchedule::Render
    }

    fn advance_motion(&mut self, now_ms: u32) {
        let elapsed_ms = now_ms.wrapping_sub(self.motion_step_at_ms);
        let due_steps = elapsed_ms / MOTION_STEP_INTERVAL_MS;
        let steps = due_steps.min(MAX_MOTION_CATCHUP_STEPS);
        for _ in 0..steps {
            self.motion.step(self.persistent.settings.motion);
        }

        if due_steps > MAX_MOTION_CATCHUP_STEPS {
            // Drop excessive backlog after a long blocking hardware operation;
            // animation should resume smoothly instead of jumping to its end.
            self.motion_step_at_ms = now_ms;
        } else {
            self.motion_step_at_ms = self
                .motion_step_at_ms
                .wrapping_add(steps * MOTION_STEP_INTERVAL_MS);
        }
    }

    /// Consume one effect generated by [`App::tick`].
    ///
    /// Persistence and audio have independent one-deep slots, so coincident
    /// timer completion cannot overwrite a new high-score save.
    pub const fn take_pending_effect(&mut self) -> AppEffect {
        if let Some(snapshot) = self.pending_persist.take() {
            AppEffect::Persist(snapshot)
        } else if let Some((frequency_hz, duration_ms)) = self.pending_tone.take() {
            AppEffect::Tone {
                frequency_hz,
                duration_ms,
            }
        } else {
            AppEffect::None
        }
    }

    /// Consume one ordinary 1350 Hz UI feedback-pulse request.
    #[must_use]
    pub const fn take_feedback_request(&mut self) -> bool {
        core::mem::replace(&mut self.feedback_requested, false)
    }

    fn handle_menu(&mut self, event: KeyEvent) -> AppEffect {
        let navigation = matches!(event.gesture, Gesture::Pressed | Gesture::Repeat);
        if navigation {
            let delta = if self.view == View::Home {
                match event.key {
                    Key::Left | Key::Up => Some(-1),
                    Key::Right | Key::Down => Some(1),
                    _ => None,
                }
            } else {
                match event.key {
                    Key::Up => Some(-1),
                    Key::Down => Some(1),
                    _ => None,
                }
            };
            if let Some(delta) = delta {
                self.move_selection(delta);
            } else if self.view == View::Settings && matches!(event.key, Key::Left | Key::Right) {
                return self.activate_setting(event.at_ms);
            }
        }

        let primary_action = matches!(event.key, Key::Enter | Key::Jump);
        if primary_action && event.gesture == Gesture::Pressed {
            // Confirmation is an edge-triggered action throughout the menu
            // hierarchy. Click is intentionally withheld for the 280 ms
            // double-click window and is too slow for primary navigation.
            // Existing springs are presentation only: open_view retargets a
            // page transition, while Home also commits its current card.
            if self.view == View::Home {
                self.motion.carousel_x.snap_to(0);
            }
            self.begin_input_guard(event.key);
            return self.activate_selection(event.at_ms);
        }

        if event.key == Key::Function
            && event.gesture == Gesture::Click
            && let Some(entry) = self.menu.selected_entry(self.view)
        {
            self.show_toast(entry.subtitle, event.at_ms, 1_800);
        }
        AppEffect::None
    }

    fn move_selection(&mut self, delta: i8) {
        if let Some(old) = self.menu.move_selection(self.view, delta) {
            if self.view == View::Home {
                self.carousel_previous = old;
                self.carousel_direction = if delta < 0 { -1 } else { 1 };
                self.motion
                    .carousel_x
                    .snap_to(i16::from(self.carousel_direction) * 128);
                self.motion.carousel_x.set_target(0);
            } else {
                self.sync_list_motion(self.view);
            }
            self.request_feedback();
        }
    }

    fn activate_selection(&mut self, now_ms: u32) -> AppEffect {
        let Some(entry) = self.menu.selected_entry(self.view) else {
            return AppEffect::None;
        };
        let effect = if entry.action == Action::Open {
            self.open_view(entry.target, now_ms, true);
            AppEffect::None
        } else {
            self.set_setting(entry.action, now_ms)
        };
        self.request_feedback();
        effect
    }

    fn activate_setting(&mut self, now_ms: u32) -> AppEffect {
        self.menu
            .selected_entry(self.view)
            .map_or(AppEffect::None, |entry| {
                self.set_setting(entry.action, now_ms)
            })
    }

    fn set_setting(&mut self, action: Action, now_ms: u32) -> AppEffect {
        let settings: &mut Settings = &mut self.persistent.settings;
        let message = match action {
            Action::ToggleSound => {
                settings.sound_enabled = !settings.sound_enabled;
                if settings.sound_enabled {
                    "SOUND ON"
                } else {
                    "SOUND OFF"
                }
            }
            Action::CycleMotion => {
                settings.motion = settings.motion.next();
                match settings.motion {
                    crate::settings::MotionLevel::Full => "FULL",
                    crate::settings::MotionLevel::Reduced => "REDUCED",
                    crate::settings::MotionLevel::Off => "MOTION OFF",
                }
            }
            Action::CycleBrightness => {
                settings.brightness = settings.brightness.next();
                match settings.brightness {
                    crate::settings::Brightness::Low => "LOW",
                    crate::settings::Brightness::Medium => "MED",
                    crate::settings::Brightness::High => "HIGH",
                    crate::settings::Brightness::Max => "MAX",
                }
            }
            Action::CycleHomeHeader => {
                settings.home_header = settings.home_header.next();
                match settings.home_header {
                    crate::settings::HomeHeaderMode::Time => "TIME",
                    crate::settings::HomeHeaderMode::Date => "DATE",
                    crate::settings::HomeHeaderMode::Pet => "PET",
                    crate::settings::HomeHeaderMode::Title => "TITLE",
                }
            }
            Action::Open => return AppEffect::None,
        };
        self.show_toast(message, now_ms, 1_200);
        AppEffect::Persist(self.persistent)
    }

    fn handle_snake(&mut self, event: KeyEvent) -> AppEffect {
        if matches!(event.gesture, Gesture::Pressed | Gesture::Repeat) {
            match event.key {
                Key::Up => self.snake.steer(Direction::Up),
                Key::Down => self.snake.steer(Direction::Down),
                Key::Left => self.snake.steer(Direction::Left),
                Key::Right => self.snake.steer(Direction::Right),
                _ => {}
            }
        }
        if matches!(event.key, Key::Enter | Key::Jump) && event.gesture == Gesture::Pressed {
            if self.snake.phase() == GamePhase::GameOver {
                self.snake
                    .reset(self.entropy ^ event.at_ms ^ self.event_count);
            }
            self.snake.start();
            self.request_feedback();
        }
        AppEffect::None
    }

    fn handle_dino(&mut self, event: KeyEvent) -> AppEffect {
        if event.gesture == Gesture::Pressed
            && matches!(event.key, Key::Jump | Key::Up | Key::Enter)
        {
            if self.dino.phase() == GamePhase::GameOver {
                self.dino.reset(self.entropy ^ event.at_ms ^ 0xd170);
            }
            self.dino.start();
            self.dino.jump();
            self.request_feedback();
        }
        AppEffect::None
    }

    fn handle_air_raid(&mut self, event: KeyEvent) -> AppEffect {
        if event.gesture != Gesture::Pressed {
            return AppEffect::None;
        }
        if event.key == Key::Enter {
            if self.air_raid.phase() == GamePhase::GameOver {
                self.air_raid.reset(self.entropy ^ event.at_ms ^ 0xa17a1d);
            }
            self.air_raid.start();
            self.request_feedback();
        } else if event.key == Key::Jump
            && self.air_raid.fire()
            && self.persistent.settings.sound_enabled
        {
            return AppEffect::Tone {
                frequency_hz: 1_700,
                duration_ms: 18,
            };
        }
        AppEffect::None
    }

    fn handle_tetris(&mut self, event: KeyEvent) -> AppEffect {
        if event.gesture == Gesture::Pressed && event.key == Key::Enter {
            if self.tetris.phase() == GamePhase::GameOver {
                self.tetris.reset(self.entropy ^ event.at_ms ^ 0x7e715);
            }
            self.tetris.start();
            self.request_feedback();
            return AppEffect::None;
        }
        if self.tetris.phase() != GamePhase::Playing
            || !matches!(event.gesture, Gesture::Pressed | Gesture::Repeat)
        {
            return AppEffect::None;
        }
        match event.key {
            Key::Left => {
                let _ = self.tetris.move_horizontal(-1);
            }
            Key::Right => {
                let _ = self.tetris.move_horizontal(1);
            }
            Key::Down => {
                let _ = self.tetris.soft_drop();
            }
            Key::Up if event.gesture == Gesture::Pressed => self.tetris.hard_drop(),
            Key::Jump if event.gesture == Gesture::Pressed => {
                let _ = self.tetris.rotate(-1);
            }
            Key::Function if event.gesture == Gesture::Pressed => {
                let _ = self.tetris.rotate(1);
            }
            _ => {}
        }
        AppEffect::None
    }

    fn handle_pong(&mut self, event: KeyEvent) -> AppEffect {
        if event.gesture == Gesture::Pressed && event.key == Key::Enter {
            if self.pong.phase() == GamePhase::GameOver {
                self.pong.reset(self.entropy ^ event.at_ms ^ 0xb011);
            }
            self.pong.start();
            self.request_feedback();
        }
        AppEffect::None
    }

    fn handle_piano(&mut self, event: KeyEvent) -> AppEffect {
        if event.key == Key::Back && event.gesture == Gesture::LongPress {
            self.navigate_back();
            return AppEffect::None;
        }
        let trigger = if event.key == Key::Back {
            event.gesture == Gesture::Click
        } else {
            event.gesture == Gesture::Pressed
        };
        if !trigger {
            return AppEffect::None;
        }
        let note = match event.key {
            Key::Up => 0,
            Key::Left => 1,
            Key::Right => 2,
            Key::Down => 3,
            Key::Jump => 4,
            Key::Function => 5,
            Key::Enter => 6,
            Key::Back => 7,
        };
        self.piano_note = note;
        if self.persistent.settings.sound_enabled {
            const FREQUENCIES: [u16; 8] = [262, 294, 330, 349, 392, 440, 494, 523];
            AppEffect::Tone {
                frequency_hz: FREQUENCIES[note as usize],
                duration_ms: 180,
            }
        } else {
            AppEffect::None
        }
    }

    fn handle_stopwatch(&mut self, event: KeyEvent) -> AppEffect {
        if event.gesture == Gesture::Pressed && matches!(event.key, Key::Enter | Key::Jump) {
            self.stopwatch.toggle(event.at_ms);
            self.request_feedback();
        } else if event.gesture == Gesture::Click && event.key == Key::Function {
            self.stopwatch.reset();
            self.show_toast("RESET", event.at_ms, 1_200);
        }
        AppEffect::None
    }

    fn handle_countdown(&mut self, event: KeyEvent) -> AppEffect {
        if !self.countdown_running && matches!(event.gesture, Gesture::Pressed | Gesture::Repeat) {
            match event.key {
                Key::Up => {
                    self.countdown_seconds = self.countdown_seconds.saturating_add(60).min(3_600)
                }
                Key::Down => self.countdown_seconds = self.countdown_seconds.saturating_sub(60),
                Key::Right => {
                    self.countdown_seconds = self.countdown_seconds.saturating_add(1).min(3_600)
                }
                Key::Left => self.countdown_seconds = self.countdown_seconds.saturating_sub(1),
                _ => {}
            }
        }
        if event.gesture == Gesture::Pressed && matches!(event.key, Key::Enter | Key::Jump) {
            if self.countdown_running {
                self.countdown_seconds = self.countdown_remaining(event.at_ms);
                self.countdown_running = false;
            } else if self.countdown_seconds != 0 {
                self.countdown_deadline_ms =
                    event.at_ms.wrapping_add(self.countdown_seconds * 1_000);
                self.countdown_running = true;
            }
            self.request_feedback();
        } else if event.gesture == Gesture::Click && event.key == Key::Function {
            self.countdown_running = false;
            self.countdown_seconds = 300;
            self.show_toast("SET 05:00", event.at_ms, 1_200);
        }
        AppEffect::None
    }

    fn handle_clock(&mut self, event: KeyEvent) -> AppEffect {
        if !self.clock_editing {
            if matches!(event.key, Key::Enter | Key::Jump) && event.gesture == Gesture::Pressed {
                if let Some(clock) = self.clock() {
                    self.clock_edit = clock;
                    self.clock_edit.seconds = 0;
                    self.clock_field = 0;
                    self.clock_editing = true;
                    self.request_feedback();
                } else {
                    self.show_toast("RTC ERROR", event.at_ms, 1_800);
                }
            }
            return AppEffect::None;
        }

        if matches!(event.gesture, Gesture::Pressed | Gesture::Repeat) {
            match event.key {
                Key::Left => {
                    self.clock_field = if self.clock_field == 0 {
                        4
                    } else {
                        self.clock_field - 1
                    };
                    self.request_feedback();
                }
                Key::Right => {
                    self.clock_field = (self.clock_field + 1) % 5;
                    self.request_feedback();
                }
                Key::Up => {
                    self.adjust_clock(1);
                    self.request_feedback();
                }
                Key::Down => {
                    self.adjust_clock(-1);
                    self.request_feedback();
                }
                _ => {}
            }
        }
        if matches!(event.key, Key::Enter | Key::Jump) && event.gesture == Gesture::Pressed {
            if self.clock_field < 4 {
                self.clock_field += 1;
                self.request_feedback();
            } else {
                self.clock_editing = false;
                self.rtc = self.clock_edit;
                self.rtc_valid = true;
                self.show_toast("CLOCK SAVED", event.at_ms, 1_200);
                self.request_feedback();
                return AppEffect::SetClock(self.clock_edit);
            }
        } else if event.key == Key::Function && event.gesture == Gesture::Click {
            self.clock_editing = false;
            self.show_toast("CANCELLED", event.at_ms, 1_200);
            self.request_feedback();
        }
        AppEffect::None
    }

    fn adjust_clock(&mut self, direction: i8) {
        match self.clock_field {
            0 => self.clock_edit.hours = wrap(self.clock_edit.hours, 0, 23, direction),
            1 => self.clock_edit.minutes = wrap(self.clock_edit.minutes, 0, 59, direction),
            2 => self.clock_edit.date.year = wrap(self.clock_edit.date.year, 0, 99, direction),
            3 => self.clock_edit.date.month = wrap(self.clock_edit.date.month, 1, 12, direction),
            4 => {
                let maximum = days_in_month(self.clock_edit.date.year, self.clock_edit.date.month);
                self.clock_edit.date.day = wrap(self.clock_edit.date.day, 1, maximum, direction);
            }
            _ => {}
        }
        let maximum = days_in_month(self.clock_edit.date.year, self.clock_edit.date.month);
        self.clock_edit.date.day = self.clock_edit.date.day.min(maximum);
    }

    fn open_view(&mut self, target: View, now_ms: u32, remember: bool) {
        if target == self.view {
            return;
        }
        if remember {
            if usize::from(self.history_len) == HISTORY_CAPACITY {
                self.history.copy_within(1..HISTORY_CAPACITY, 0);
                self.history_len -= 1;
            }
            self.history[usize::from(self.history_len)] = self.view;
            self.history_len += 1;
        }
        self.previous_view = self.view;
        self.view = target;
        self.page_forward = true;
        self.motion.page_x.snap_to(128);
        self.motion.page_x.set_target(0);
        self.sync_list_motion(target);

        let seed = self.entropy ^ now_ms ^ self.event_count.rotate_left(9) ^ 0x9e37_79b9;
        match target {
            View::Snake => self.snake.reset(seed ^ 0x51a2_e11f),
            View::Dino => self.dino.reset(seed ^ 0xd170_2026),
            View::AirRaid => self.air_raid.reset(seed ^ 0xa17a_1d55),
            View::Tetris => self.tetris.reset(seed ^ 0x7e71_5123),
            View::Pong => self.pong.reset(seed ^ 0xb011_2026),
            View::Piano => self.piano_note = u8::MAX,
            _ => {}
        }
    }

    fn navigate_back(&mut self) {
        if self.view == View::Home {
            return;
        }
        let target = if self.history_len == 0 {
            View::Home
        } else {
            self.history_len -= 1;
            self.history[usize::from(self.history_len)]
        };
        self.previous_view = self.view;
        self.view = target;
        self.page_forward = false;
        self.motion.page_x.snap_to(-128);
        self.motion.page_x.set_target(0);
        self.sync_list_motion(target);
        self.request_feedback();
    }

    fn sync_list_motion(&mut self, view: View) {
        let Some(menu) = menu_for(view) else { return };
        if view == View::Home || menu.entries.is_empty() {
            return;
        }
        let selected = self.menu.selection(view) as usize;
        let maximum_top = menu.entries.len().saturating_sub(3);
        let top = selected.saturating_sub(1).min(maximum_top);
        self.motion
            .cursor_y
            .set_target((14 + (selected - top) * 16) as i16);
        self.motion.scroll_y.set_target(-(top as i16 * 16));
        let width = menu.entries[selected]
            .label
            .len()
            .saturating_mul(6)
            .saturating_add(20)
            .min(121);
        self.motion.cursor_width.set_target(width as i16);
    }

    fn show_toast(&mut self, message: &'static str, now_ms: u32, duration_ms: u32) {
        self.toast = Some(message);
        self.toast_until_ms = now_ms.wrapping_add(duration_ms);
    }

    fn request_feedback(&mut self) {
        self.feedback_requested = true;
    }

    fn mix_entropy(&mut self, value: u32) {
        let mut mixed = self.entropy ^ value.rotate_left(11);
        mixed ^= mixed << 13;
        mixed ^= mixed >> 17;
        mixed ^= mixed << 5;
        self.entropy = mixed.max(1);
    }

    fn begin_input_guard(&mut self, key: Key) {
        self.input_guard_key = Some(key);
        self.input_guard_was_long = false;
    }

    fn consume_input_guard(&mut self, event: KeyEvent) -> bool {
        if self.input_guard_key != Some(event.key) {
            return false;
        }
        match event.gesture {
            Gesture::Pressed => {
                // A debounced second Pressed can only occur after the guarded
                // physical press was released. Let that new action run at
                // once, while retaining the guard for the recognizer's
                // eventual Click or DoubleClick tail.
                self.input_guard_was_long = false;
                false
            }
            Gesture::LongPress => {
                self.input_guard_was_long = true;
                true
            }
            Gesture::Released if self.input_guard_was_long => {
                self.input_guard_key = None;
                self.input_guard_was_long = false;
                true
            }
            Gesture::Click | Gesture::DoubleClick => {
                self.input_guard_key = None;
                self.input_guard_was_long = false;
                true
            }
            Gesture::Released | Gesture::Repeat => true,
        }
    }
}

const fn axis(negative: bool, positive: bool) -> i8 {
    if negative == positive {
        0
    } else if negative {
        -1
    } else {
        1
    }
}

const fn deadline_reached(now: u32, deadline: u32) -> bool {
    now.wrapping_sub(deadline) < 0x8000_0000
}

const fn wrap(value: u8, minimum: u8, maximum: u8, direction: i8) -> u8 {
    if direction > 0 {
        if value >= maximum { minimum } else { value + 1 }
    } else if value <= minimum {
        maximum
    } else {
        value - 1
    }
}

#[cfg(test)]
mod tests {
    use crate::button::ButtonBank;

    use super::*;

    fn event(key: Key, gesture: Gesture, at_ms: u32) -> KeyEvent {
        KeyEvent::new(key, gesture, at_ms)
    }

    fn active_app() -> App {
        let mut app = App::new(0, PersistentData::default(), 1);
        app.tick(App::BOOT_DURATION_MS, 0);
        app
    }

    fn sample_key(
        bank: &mut ButtonBank,
        app: &mut App,
        key: Key,
        from_ms: u32,
        through_ms: u32,
        pressed: bool,
    ) {
        for now_ms in (from_ms..=through_ms).step_by(5) {
            let mut keys = [false; Key::COUNT];
            keys[key.index()] = pressed;
            bank.update(now_ms, keys, |event| {
                let _ = app.handle_event_at(event, now_ms);
            });
        }
    }

    #[test]
    fn boot_is_skippable_and_also_times_out() {
        let mut skipped = App::new(0, PersistentData::default(), 1);
        skipped.handle_event(event(Key::Enter, Gesture::Pressed, 50));
        assert_eq!(skipped.mode(), AppMode::Active);
        let mut timed = App::new(0, PersistentData::default(), 1);
        timed.tick(App::BOOT_DURATION_MS, 0);
        assert_eq!(timed.mode(), AppMode::Active);
    }

    #[test]
    fn menu_reaches_all_six_games() {
        let mut app = active_app();
        app.handle_event(event(Key::Enter, Gesture::Pressed, 1_000));
        assert_eq!(app.view(), View::Games);
        for expected in [
            View::Dino,
            View::Snake,
            View::AirRaid,
            View::Tetris,
            View::Pong,
            View::Piano,
        ] {
            let mut candidate = active_app();
            candidate.handle_event(event(Key::Enter, Gesture::Pressed, 1_000));
            while candidate.menu().selected_entry(View::Games).unwrap().target != expected {
                candidate.handle_event(event(Key::Down, Gesture::Pressed, 1_010));
            }
            candidate.handle_event(event(Key::Enter, Gesture::Pressed, 1_020));
            assert_eq!(candidate.view(), expected);
        }
    }

    #[test]
    fn setting_change_requests_complete_snapshot_persistence() {
        let mut app = active_app();
        app.handle_event(event(Key::Right, Gesture::Pressed, 800));
        app.handle_event(event(Key::Right, Gesture::Pressed, 810));
        app.handle_event(event(Key::Right, Gesture::Pressed, 820));
        app.handle_event(event(Key::Enter, Gesture::Pressed, 900));
        assert_eq!(app.view(), View::Settings);
        let effect = app.handle_event(event(Key::Enter, Gesture::Pressed, 950));
        assert!(matches!(effect, AppEffect::Persist(_)));
        assert!(!app.persistent_data().settings.sound_enabled);
    }

    #[test]
    fn home_confirmation_is_immediate_and_does_not_leak_into_the_opened_view() {
        let mut app = active_app();
        app.handle_event(event(Key::Right, Gesture::Pressed, 800));
        assert_eq!(app.view(), View::Home);
        assert!(!app.motion().carousel_x.is_settled());

        app.handle_event(event(Key::Enter, Gesture::Pressed, 810));
        assert_eq!(app.view(), View::Tools);
        assert!(app.motion().carousel_x.is_settled());

        // Released and the delayed Click belong to the press that opened
        // Tools. Neither may cascade into the selected Stopwatch entry.
        app.handle_event(event(Key::Enter, Gesture::Released, 850));
        app.handle_event_at(event(Key::Enter, Gesture::Click, 850), 1_130);
        assert_eq!(app.view(), View::Tools);

        app.handle_event(event(Key::Enter, Gesture::Pressed, 1_200));
        assert_eq!(app.view(), View::Stopwatch);
    }

    #[test]
    fn secondary_confirmation_accepts_a_new_press_before_the_old_click_tail() {
        let mut app = active_app();
        app.handle_event(event(Key::Enter, Gesture::Pressed, 800));
        assert_eq!(app.view(), View::Games);

        // The first release has happened, but its Click decision window is
        // still open. A new debounced press must launch the selected game now.
        app.handle_event(event(Key::Enter, Gesture::Released, 840));
        app.handle_event(event(Key::Enter, Gesture::Pressed, 900));
        assert_eq!(app.view(), View::Dino);

        app.handle_event(event(Key::Enter, Gesture::Released, 940));
        app.handle_event(event(Key::Enter, Gesture::DoubleClick, 940));
        assert_eq!(app.view(), View::Dino);
    }

    #[test]
    fn physical_fast_double_press_opens_one_level_per_press_without_waiting() {
        let mut app = active_app();
        let mut bank = ButtonBank::default();

        sample_key(&mut bank, &mut app, Key::Enter, 800, 820, true);
        assert_eq!(app.view(), View::Games);
        sample_key(&mut bank, &mut app, Key::Enter, 825, 845, false);

        sample_key(&mut bank, &mut app, Key::Enter, 850, 870, true);
        assert_eq!(app.view(), View::Dino);
        sample_key(&mut bank, &mut app, Key::Enter, 875, 895, false);

        // The recognizer emits one DoubleClick after the second release. It is
        // diagnostic tail data now, not a third confirmation.
        assert_eq!(app.view(), View::Dino);
    }

    #[test]
    fn jump_also_confirms_the_home_card_on_press() {
        let mut app = active_app();
        app.handle_event(event(Key::Jump, Gesture::Pressed, 800));
        assert_eq!(app.view(), View::Games);
    }

    #[test]
    fn motion_uses_elapsed_time_instead_of_render_tick_count() {
        let mut frequent_ticks = active_app();
        let mut one_tick = active_app();
        frequent_ticks.handle_event(event(Key::Right, Gesture::Pressed, 701));
        one_tick.handle_event(event(Key::Right, Gesture::Pressed, 701));

        for now_ms in [708, 716, 724, 732] {
            frequent_ticks.tick(now_ms, 0);
        }
        one_tick.tick(732, 0);

        assert_eq!(frequent_ticks.motion(), one_tick.motion());
    }

    #[test]
    fn clock_edit_is_atomic_and_emits_hardware_effect() {
        let mut app = active_app();
        app.set_clock_snapshot(Some(DateTime::default()));
        app.open_view(View::Clock, 1_000, true);
        app.handle_event(event(Key::Enter, Gesture::Pressed, 1_100));
        assert!(app.clock_editing());
        app.handle_event(event(Key::Up, Gesture::Pressed, 1_110));
        for index in 0..5 {
            let effect = app.handle_event(event(Key::Enter, Gesture::Pressed, 1_200 + index));
            if index == 4 {
                assert!(matches!(effect, AppEffect::SetClock(_)));
            }
        }
        assert!(!app.clock_editing());
    }

    #[test]
    fn delayed_click_uses_processing_time_but_preserves_diagnostics_timestamp() {
        let mut app = active_app();
        app.open_view(View::Stopwatch, 900, true);
        let click = event(Key::Function, Gesture::Click, 1_000);
        app.handle_event_at(click, 1_280);
        assert_eq!(app.toast(2_400), Some("RESET"));
        assert_eq!(app.last_event(), Some(click));
    }

    #[test]
    fn primary_utility_actions_run_on_pressed_not_delayed_click() {
        let mut app = active_app();
        app.open_view(View::Stopwatch, 900, true);
        app.handle_event(event(Key::Enter, Gesture::Pressed, 1_000));
        assert!(app.stopwatch_running());
        app.handle_event_at(event(Key::Enter, Gesture::Click, 1_040), 1_320);
        assert!(app.stopwatch_running());

        app.open_view(View::Countdown, 1_400, false);
        app.handle_event(event(Key::Jump, Gesture::Pressed, 1_500));
        assert!(app.countdown_running());
    }

    #[test]
    fn feedback_is_requested_by_actions_not_by_arbitrary_gestures() {
        let mut app = active_app();
        app.handle_event(event(Key::Right, Gesture::Pressed, 800));
        assert!(app.take_feedback_request());

        app.handle_event(event(Key::Function, Gesture::LongPress, 900));
        assert_eq!(app.view(), View::InputLab);
        assert!(!app.take_feedback_request());

        app.open_view(View::Tetris, 1_000, false);
        app.handle_event(event(Key::Enter, Gesture::Pressed, 1_010));
        assert!(app.take_feedback_request());
        app.handle_event(event(Key::Left, Gesture::Pressed, 1_020));
        assert!(!app.take_feedback_request());
    }

    #[test]
    fn pending_save_and_timer_tone_are_both_delivered() {
        let mut app = active_app();
        let snapshot = app.persistent_data();
        app.pending_persist = Some(snapshot);
        app.pending_tone = Some((880, 600));
        assert_eq!(app.take_pending_effect(), AppEffect::Persist(snapshot));
        assert_eq!(
            app.take_pending_effect(),
            AppEffect::Tone {
                frequency_hz: 880,
                duration_ms: 600
            }
        );
        assert_eq!(app.take_pending_effect(), AppEffect::None);
    }
}
