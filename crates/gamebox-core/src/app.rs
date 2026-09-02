//! Product-level application state machine.

use crate::{
    button::{Gesture, Key, KeyEvent},
    menu::{ApplicationId, MenuEffect, MenuModel, TransitionDirection},
    motion::MenuMotion,
    snake::{Direction, Snake, StepResult},
    storage::PersistentData,
};

/// Top-level full-screen mode.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AppMode {
    /// Short, skippable startup reveal.
    Boot,
    /// Hierarchical animated menu.
    Menu,
    /// Snake game.
    Snake,
    /// Stopwatch utility.
    Stopwatch,
    /// Countdown utility.
    Countdown,
    /// Low-update display mode, woken by any key.
    Standby,
}

/// Shell work requested by an application transition.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AppEffect {
    /// No platform side effect.
    None,
    /// Save this complete snapshot through the Flash journal.
    Persist(PersistentData),
}

/// Whether the current scheduler tick changed visible state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RenderSchedule {
    /// No display transaction is needed.
    Idle,
    /// Render and submit a new scene.
    Render,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
struct Stopwatch {
    accumulated_ms: u32,
    started_at_ms: u32,
    running: bool,
}

impl Stopwatch {
    const fn elapsed_ms(self, now_ms: u32) -> u32 {
        if self.running {
            self.accumulated_ms
                .saturating_add(now_ms.wrapping_sub(self.started_at_ms))
        } else {
            self.accumulated_ms
        }
    }

    fn toggle(&mut self, now_ms: u32) {
        if self.running {
            self.accumulated_ms = self.elapsed_ms(now_ms);
            self.running = false;
        } else {
            self.started_at_ms = now_ms;
            self.running = true;
        }
    }

    fn reset(&mut self) {
        if !self.running {
            self.accumulated_ms = 0;
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Countdown {
    configured_ms: u32,
    remaining_at_start_ms: u32,
    started_at_ms: u32,
    running: bool,
    completed: bool,
}

impl Default for Countdown {
    fn default() -> Self {
        Self {
            configured_ms: 60_000,
            remaining_at_start_ms: 60_000,
            started_at_ms: 0,
            running: false,
            completed: false,
        }
    }
}

impl Countdown {
    const fn remaining_ms(self, now_ms: u32) -> u32 {
        if self.running {
            self.remaining_at_start_ms
                .saturating_sub(now_ms.wrapping_sub(self.started_at_ms))
        } else {
            self.remaining_at_start_ms
        }
    }

    fn toggle(&mut self, now_ms: u32) {
        if self.running {
            self.remaining_at_start_ms = self.remaining_ms(now_ms);
            self.running = false;
        } else {
            if self.remaining_at_start_ms == 0 {
                self.remaining_at_start_ms = self.configured_ms;
            }
            self.completed = false;
            self.started_at_ms = now_ms;
            self.running = true;
        }
    }

    fn adjust(&mut self, delta_seconds: i32) {
        if self.running {
            return;
        }
        let seconds = (self.configured_ms / 1_000)
            .saturating_add_signed(delta_seconds)
            .clamp(10, 5_999);
        self.configured_ms = seconds * 1_000;
        self.remaining_at_start_ms = self.configured_ms;
        self.completed = false;
    }

    fn update(&mut self, now_ms: u32) -> bool {
        if self.running && self.remaining_ms(now_ms) == 0 {
            self.running = false;
            self.remaining_at_start_ms = 0;
            self.completed = true;
            true
        } else {
            false
        }
    }
}

/// All hardware-independent product state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct App {
    mode: AppMode,
    mode_started_ms: u32,
    menu: MenuModel,
    menu_motion: MenuMotion,
    persistent: PersistentData,
    snake: Snake,
    snake_paused: bool,
    snake_last_step_ms: u32,
    stopwatch: Stopwatch,
    countdown: Countdown,
    last_dynamic_render_ms: u32,
    entropy: u32,
    pending_effect: AppEffect,
    input_guard_key: Option<Key>,
    input_guard_was_long: bool,
}

impl App {
    /// Startup reveal duration.
    pub const BOOT_DURATION_MS: u32 = 1_150;

    /// Create the product state from validated persistent data and entropy.
    #[must_use]
    pub fn new(now_ms: u32, persistent: PersistentData, entropy: u32) -> Self {
        let persistent = PersistentData {
            settings: persistent.settings.normalized(),
            snake_high_score: persistent.snake_high_score,
        };
        let mut app = Self {
            mode: AppMode::Boot,
            mode_started_ms: now_ms,
            menu: MenuModel::default(),
            menu_motion: MenuMotion::default(),
            persistent,
            snake: Snake::new(entropy),
            snake_paused: false,
            snake_last_step_ms: now_ms,
            stopwatch: Stopwatch::default(),
            countdown: Countdown::default(),
            last_dynamic_render_ms: now_ms,
            entropy: entropy.max(1),
            pending_effect: AppEffect::None,
            input_guard_key: None,
            input_guard_was_long: false,
        };
        app.sync_menu_motion(false);
        app
    }

    /// Current full-screen mode.
    #[must_use]
    pub const fn mode(&self) -> AppMode {
        self.mode
    }

    /// Current menu model.
    #[must_use]
    pub const fn menu(&self) -> &MenuModel {
        &self.menu
    }

    /// Current animated menu values.
    #[must_use]
    pub const fn menu_motion(&self) -> &MenuMotion {
        &self.menu_motion
    }

    /// Current validated persistent snapshot.
    #[must_use]
    pub const fn persistent_data(&self) -> PersistentData {
        self.persistent
    }

    /// Active snake simulation.
    #[must_use]
    pub const fn snake(&self) -> &Snake {
        &self.snake
    }

    /// Whether snake movement is paused.
    #[must_use]
    pub const fn snake_is_paused(&self) -> bool {
        self.snake_paused
    }

    /// Current stopwatch value.
    #[must_use]
    pub const fn stopwatch_elapsed_ms(&self, now_ms: u32) -> u32 {
        self.stopwatch.elapsed_ms(now_ms)
    }

    /// Whether the stopwatch is running.
    #[must_use]
    pub const fn stopwatch_is_running(&self) -> bool {
        self.stopwatch.running
    }

    /// Current countdown value.
    #[must_use]
    pub const fn countdown_remaining_ms(&self, now_ms: u32) -> u32 {
        self.countdown.remaining_ms(now_ms)
    }

    /// Whether the countdown is running.
    #[must_use]
    pub const fn countdown_is_running(&self) -> bool {
        self.countdown.running
    }

    /// Whether the countdown most recently reached zero.
    #[must_use]
    pub const fn countdown_is_completed(&self) -> bool {
        self.countdown.completed
    }

    /// Milliseconds since the current mode was entered.
    #[must_use]
    pub const fn mode_elapsed_ms(&self, now_ms: u32) -> u32 {
        now_ms.wrapping_sub(self.mode_started_ms)
    }

    /// Process one recognized key event.
    pub fn handle_event(&mut self, event: KeyEvent) -> AppEffect {
        self.mix_entropy(event.at_ms ^ ((event.key as u32) << 24));
        if self.consume_input_guard(event) {
            return AppEffect::None;
        }
        match self.mode {
            AppMode::Boot => {
                if event.gesture == Gesture::Pressed {
                    self.enter_mode(AppMode::Menu, event.at_ms);
                    self.begin_input_guard(event.key);
                }
                AppEffect::None
            }
            AppMode::Menu => self.handle_menu_event(event),
            AppMode::Snake => self.handle_snake_event(event),
            AppMode::Stopwatch => self.handle_stopwatch_event(event),
            AppMode::Countdown => self.handle_countdown_event(event),
            AppMode::Standby => {
                if event.gesture == Gesture::Pressed {
                    self.enter_mode(AppMode::Menu, event.at_ms);
                    self.begin_input_guard(event.key);
                }
                AppEffect::None
            }
        }
    }

    /// Advance time-driven state and decide whether a new frame is needed.
    pub fn tick(&mut self, now_ms: u32) -> RenderSchedule {
        match self.mode {
            AppMode::Boot => {
                if self.mode_elapsed_ms(now_ms) >= Self::BOOT_DURATION_MS {
                    self.enter_mode(AppMode::Menu, now_ms);
                }
                RenderSchedule::Render
            }
            AppMode::Menu => {
                let was_active = self.menu_motion.is_active();
                self.menu_motion
                    .step(self.persistent.settings.animation_speed);
                self.menu_motion
                    .step(self.persistent.settings.animation_speed);
                if was_active || self.menu_motion.is_active() {
                    RenderSchedule::Render
                } else {
                    RenderSchedule::Idle
                }
            }
            AppMode::Snake => self.tick_snake(now_ms),
            AppMode::Stopwatch => {
                if self.stopwatch.running && self.dynamic_frame_due(now_ms, 100) {
                    RenderSchedule::Render
                } else {
                    RenderSchedule::Idle
                }
            }
            AppMode::Countdown => {
                let completed = self.countdown.update(now_ms);
                if completed || (self.countdown.running && self.dynamic_frame_due(now_ms, 100)) {
                    RenderSchedule::Render
                } else {
                    RenderSchedule::Idle
                }
            }
            AppMode::Standby => {
                let period = u32::from(self.persistent.settings.standby_refresh_seconds) * 1_000;
                if self.dynamic_frame_due(now_ms, period) {
                    RenderSchedule::Render
                } else {
                    RenderSchedule::Idle
                }
            }
        }
    }

    /// Consume a deferred effect produced by a time-driven transition.
    pub fn take_pending_effect(&mut self) -> AppEffect {
        core::mem::replace(&mut self.pending_effect, AppEffect::None)
    }

    fn handle_menu_event(&mut self, event: KeyEvent) -> AppEffect {
        let before_page = self.menu.page_id();
        let before_selection = self.menu.selected_index();
        let effect = self.menu.handle_event(event, &mut self.persistent.settings);
        if before_page != self.menu.page_id() || before_selection != self.menu.selected_index() {
            self.sync_menu_motion(true);
        }
        match effect {
            MenuEffect::None => AppEffect::None,
            MenuEffect::Launch(application) => {
                self.menu.record_launch(application);
                self.launch(application, event.at_ms);
                AppEffect::None
            }
            MenuEffect::Standby => {
                self.enter_mode(AppMode::Standby, event.at_ms);
                AppEffect::None
            }
            MenuEffect::SettingsChanged(settings) => {
                self.persistent.settings = settings.normalized();
                self.sync_menu_motion(false);
                AppEffect::Persist(self.persistent)
            }
        }
    }

    fn handle_snake_event(&mut self, event: KeyEvent) -> AppEffect {
        if event.gesture == Gesture::Pressed {
            let direction = match event.key {
                Key::Up => Some(Direction::Up),
                Key::Down => Some(Direction::Down),
                Key::Left => Some(Direction::Left),
                Key::Right => Some(Direction::Right),
                _ => None,
            };
            if let Some(direction) = direction {
                self.snake.steer(direction);
            }
            if matches!(event.key, Key::Jump | Key::Enter) && self.snake.is_game_over() {
                self.snake.reset(self.entropy ^ event.at_ms);
                self.snake_paused = false;
                self.snake_last_step_ms = event.at_ms;
            }
        }
        match (event.key, event.gesture) {
            (Key::Enter, Gesture::Click) if !self.snake.is_game_over() => {
                self.snake_paused = !self.snake_paused;
            }
            (Key::Back, Gesture::Click | Gesture::LongPress) => {
                self.enter_mode(AppMode::Menu, event.at_ms);
            }
            _ => {}
        }
        AppEffect::None
    }

    fn handle_stopwatch_event(&mut self, event: KeyEvent) -> AppEffect {
        match (event.key, event.gesture) {
            (Key::Enter | Key::Jump, Gesture::Click) => self.stopwatch.toggle(event.at_ms),
            (Key::Down, Gesture::Click | Gesture::LongPress) => self.stopwatch.reset(),
            (Key::Back, Gesture::Click | Gesture::LongPress) => {
                self.enter_mode(AppMode::Menu, event.at_ms);
            }
            _ => {}
        }
        AppEffect::None
    }

    fn handle_countdown_event(&mut self, event: KeyEvent) -> AppEffect {
        match (event.key, event.gesture) {
            (Key::Up, Gesture::Pressed | Gesture::Repeat) => self.countdown.adjust(60),
            (Key::Down, Gesture::Pressed | Gesture::Repeat) => self.countdown.adjust(-60),
            (Key::Left, Gesture::Pressed | Gesture::Repeat) => self.countdown.adjust(-10),
            (Key::Right, Gesture::Pressed | Gesture::Repeat) => self.countdown.adjust(10),
            (Key::Enter | Key::Jump, Gesture::Click) => self.countdown.toggle(event.at_ms),
            (Key::Back, Gesture::Click | Gesture::LongPress) => {
                self.enter_mode(AppMode::Menu, event.at_ms);
            }
            _ => {}
        }
        AppEffect::None
    }

    fn tick_snake(&mut self, now_ms: u32) -> RenderSchedule {
        if self.snake_paused || self.snake.is_game_over() {
            return RenderSchedule::Idle;
        }
        if now_ms.wrapping_sub(self.snake_last_step_ms) < u32::from(self.snake.step_interval_ms()) {
            return RenderSchedule::Idle;
        }
        self.snake_last_step_ms = now_ms;
        if self.snake.step() == StepResult::GameOver
            && self.snake.score() > self.persistent.snake_high_score
        {
            self.persistent.snake_high_score = self.snake.score();
            self.pending_effect = AppEffect::Persist(self.persistent);
        }
        RenderSchedule::Render
    }

    fn launch(&mut self, application: ApplicationId, now_ms: u32) {
        let mode = match application {
            ApplicationId::Snake => {
                self.snake.reset(self.entropy ^ now_ms);
                self.snake_paused = false;
                self.snake_last_step_ms = now_ms;
                AppMode::Snake
            }
            ApplicationId::Stopwatch => AppMode::Stopwatch,
            ApplicationId::Countdown => AppMode::Countdown,
        };
        self.enter_mode(mode, now_ms);
    }

    fn enter_mode(&mut self, mode: AppMode, now_ms: u32) {
        self.mode = mode;
        self.mode_started_ms = now_ms;
        self.last_dynamic_render_ms = now_ms;
        if mode == AppMode::Menu {
            self.sync_menu_motion(false);
        }
    }

    fn sync_menu_motion(&mut self, animate_page: bool) {
        let page = self.menu.current_page();
        let selected = self.menu.selected_index();
        let visible_rows = 3_usize;
        let maximum_top = page.entries.len().saturating_sub(visible_rows);
        let top = selected.saturating_sub(1).min(maximum_top);
        self.menu_motion
            .cursor_y
            .set_target((14 + (selected - top) * 16) as i16);
        self.menu_motion.scroll_y.set_target(-(top as i16 * 16));
        let width = text_width(page.entries[selected].label)
            .saturating_add(20)
            .min(120);
        self.menu_motion.cursor_width.set_target(width as i16);

        if let Some(direction) = self.menu.take_transition() {
            if animate_page {
                let start = match direction {
                    TransitionDirection::Forward => 128,
                    TransitionDirection::Backward => -128,
                };
                self.menu_motion.page_x.snap_to(start);
            }
            self.menu_motion.page_x.set_target(0);
        }
    }

    fn dynamic_frame_due(&mut self, now_ms: u32, period_ms: u32) -> bool {
        if now_ms.wrapping_sub(self.last_dynamic_render_ms) >= period_ms {
            self.last_dynamic_render_ms = now_ms;
            true
        } else {
            false
        }
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

    /// Consume the remainder of a physical press that already caused a mode
    /// transition. A short press ends at Click/DoubleClick (after Released),
    /// while a long press ends at Released (after LongPress and repeats).
    fn consume_input_guard(&mut self, event: KeyEvent) -> bool {
        if self.input_guard_key != Some(event.key) {
            return false;
        }
        match event.gesture {
            Gesture::LongPress => self.input_guard_was_long = true,
            Gesture::Released if self.input_guard_was_long => {
                self.input_guard_key = None;
                self.input_guard_was_long = false;
            }
            Gesture::Click | Gesture::DoubleClick => {
                self.input_guard_key = None;
                self.input_guard_was_long = false;
            }
            Gesture::Pressed | Gesture::Released | Gesture::Repeat => {}
        }
        true
    }
}

fn text_width(text: &str) -> usize {
    text.chars()
        .map(|character| if character.is_ascii() { 6 } else { 16 })
        .sum()
}

#[cfg(test)]
mod tests {
    use crate::{
        button::{Gesture, Key, KeyEvent},
        menu::PageId,
    };

    use super::*;

    fn event(key: Key, gesture: Gesture, at_ms: u32) -> KeyEvent {
        KeyEvent::new(key, gesture, at_ms)
    }

    #[test]
    fn boot_is_skippable_and_also_times_out() {
        let mut skipped = App::new(0, PersistentData::default(), 1);
        skipped.handle_event(event(Key::Enter, Gesture::Pressed, 50));
        assert_eq!(skipped.mode(), AppMode::Menu);

        let mut timed = App::new(0, PersistentData::default(), 1);
        timed.tick(App::BOOT_DURATION_MS);
        assert_eq!(timed.mode(), AppMode::Menu);
    }

    #[test]
    fn boot_skip_does_not_replay_its_trailing_click_into_menu() {
        let mut app = App::new(0, PersistentData::default(), 1);
        app.handle_event(event(Key::Enter, Gesture::Pressed, 10));
        app.handle_event(event(Key::Enter, Gesture::Released, 30));
        app.handle_event(event(Key::Enter, Gesture::Click, 30));
        assert_eq!(app.mode(), AppMode::Menu);
        assert_eq!(app.menu().page_id(), PageId::Home);

        app.handle_event(event(Key::Enter, Gesture::Click, 400));
        assert_eq!(app.menu().page_id(), PageId::Games);
    }

    #[test]
    fn menu_launches_snake_without_nested_control_flow() {
        let mut app = App::new(0, PersistentData::default(), 1);
        app.handle_event(event(Key::Enter, Gesture::Pressed, 1));
        app.handle_event(event(Key::Enter, Gesture::Click, 100));
        app.handle_event(event(Key::Enter, Gesture::Click, 110));
        assert_eq!(app.menu().page_id(), PageId::Games);
        app.handle_event(event(Key::Down, Gesture::Pressed, 120));
        app.handle_event(event(Key::Enter, Gesture::Click, 150));
        assert_eq!(app.mode(), AppMode::Snake);
    }

    #[test]
    fn setting_change_requests_complete_snapshot_persistence() {
        let mut app = App::new(0, PersistentData::default(), 1);
        app.handle_event(event(Key::Enter, Gesture::Pressed, 1));
        app.handle_event(event(Key::Function, Gesture::Pressed, 50));
        app.handle_event(event(Key::Down, Gesture::Pressed, 60));
        let effect = app.handle_event(event(Key::Right, Gesture::Pressed, 70));
        assert!(matches!(effect, AppEffect::Persist(_)));
        assert!(!app.persistent_data().settings.sound_enabled);
    }
}
