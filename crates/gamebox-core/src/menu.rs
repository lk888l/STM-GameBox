//! Table-driven hierarchical menu state machine.

use crate::{
    button::{Gesture, Key, KeyEvent},
    settings::Settings,
};

/// Menu pages with stable indices for per-page selection memory.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum PageId {
    /// Top-level product navigation.
    Home = 0,
    /// Playable games.
    Games = 1,
    /// Time-based utility applications.
    Tools = 2,
    /// Persistent user settings.
    Settings = 3,
}

impl PageId {
    const COUNT: usize = 4;

    const fn index(self) -> usize {
        self as usize
    }
}

/// Applications that can take ownership of the main content area.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ApplicationId {
    /// Grid-based snake game.
    Snake,
    /// Stopwatch utility.
    Stopwatch,
    /// Countdown utility.
    Countdown,
}

/// A menu entry's behavior, separate from its presentation label.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MenuAction {
    /// Navigate into a child page.
    Open(PageId),
    /// Navigate to the parent page.
    Back,
    /// Launch an application.
    Launch(ApplicationId),
    /// Enter low-update standby mode.
    Standby,
    /// Modify one persistent setting.
    Adjust(SettingId),
}

/// Persistent settings addressable from the menu.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SettingId {
    /// Sound on/off.
    Sound,
    /// Motion preset.
    Animation,
    /// Selected-row visual style.
    Cursor,
    /// Standby refresh period.
    StandbyRefresh,
}

/// Immutable presentation and command descriptor.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MenuEntry {
    /// Chinese display label.
    pub label: &'static str,
    /// Command produced by activation.
    pub action: MenuAction,
}

const HOME_ENTRIES: [MenuEntry; 4] = [
    MenuEntry {
        label: "游戏",
        action: MenuAction::Open(PageId::Games),
    },
    MenuEntry {
        label: "工具",
        action: MenuAction::Open(PageId::Tools),
    },
    MenuEntry {
        label: "设置",
        action: MenuAction::Open(PageId::Settings),
    },
    MenuEntry {
        label: "关机",
        action: MenuAction::Standby,
    },
];

const GAME_ENTRIES: [MenuEntry; 2] = [
    MenuEntry {
        label: "返回",
        action: MenuAction::Back,
    },
    MenuEntry {
        label: "贪吃蛇",
        action: MenuAction::Launch(ApplicationId::Snake),
    },
];

const TOOL_ENTRIES: [MenuEntry; 3] = [
    MenuEntry {
        label: "返回",
        action: MenuAction::Back,
    },
    MenuEntry {
        label: "秒表",
        action: MenuAction::Launch(ApplicationId::Stopwatch),
    },
    MenuEntry {
        label: "倒计时",
        action: MenuAction::Launch(ApplicationId::Countdown),
    },
];

const SETTING_ENTRIES: [MenuEntry; 5] = [
    MenuEntry {
        label: "返回",
        action: MenuAction::Back,
    },
    MenuEntry {
        label: "静音",
        action: MenuAction::Adjust(SettingId::Sound),
    },
    MenuEntry {
        label: "动画速度",
        action: MenuAction::Adjust(SettingId::Animation),
    },
    MenuEntry {
        label: "光标风格",
        action: MenuAction::Adjust(SettingId::Cursor),
    },
    MenuEntry {
        label: "刷新时间",
        action: MenuAction::Adjust(SettingId::StandbyRefresh),
    },
];

/// View of a page's immutable content.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MenuPage {
    /// Short Chinese page title.
    pub title: &'static str,
    /// Ordered entries.
    pub entries: &'static [MenuEntry],
}

/// Look up an immutable page descriptor.
#[must_use]
pub const fn page(id: PageId) -> MenuPage {
    match id {
        PageId::Home => MenuPage {
            title: "游戏机",
            entries: &HOME_ENTRIES,
        },
        PageId::Games => MenuPage {
            title: "游戏",
            entries: &GAME_ENTRIES,
        },
        PageId::Tools => MenuPage {
            title: "工具",
            entries: &TOOL_ENTRIES,
        },
        PageId::Settings => MenuPage {
            title: "设置",
            entries: &SETTING_ENTRIES,
        },
    }
}

/// Direction used to seed an incoming-page transition.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransitionDirection {
    /// Child page enters from the right.
    Forward,
    /// Parent page enters from the left.
    Backward,
}

/// Side effect returned to the application shell.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MenuEffect {
    /// No shell-level work is needed.
    None,
    /// Launch a full-screen application.
    Launch(ApplicationId),
    /// Enter standby.
    Standby,
    /// Persist and apply a changed setting snapshot.
    SettingsChanged(Settings),
}

/// Hierarchical menu model with bounded history and no allocation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MenuModel {
    page: PageId,
    selected: [u8; PageId::COUNT],
    history: [PageId; PageId::COUNT],
    history_len: u8,
    transition: Option<TransitionDirection>,
    last_application: Option<ApplicationId>,
}

impl Default for MenuModel {
    fn default() -> Self {
        Self {
            page: PageId::Home,
            selected: [0; PageId::COUNT],
            history: [PageId::Home; PageId::COUNT],
            history_len: 0,
            transition: None,
            last_application: None,
        }
    }
}

impl MenuModel {
    /// Current page identifier.
    #[must_use]
    pub const fn page_id(&self) -> PageId {
        self.page
    }

    /// Current immutable page descriptor.
    #[must_use]
    pub const fn current_page(&self) -> MenuPage {
        page(self.page)
    }

    /// Selected entry index.
    #[must_use]
    pub const fn selected_index(&self) -> usize {
        self.selected[self.page.index()] as usize
    }

    /// Selected entry descriptor.
    #[must_use]
    pub fn selected_entry(&self) -> &'static MenuEntry {
        &self.current_page().entries[self.selected_index()]
    }

    /// Most recently launched application, used by the quick-launch gesture.
    #[must_use]
    pub const fn last_application(&self) -> Option<ApplicationId> {
        self.last_application
    }

    /// Record a successfully launched application.
    pub const fn record_launch(&mut self, application: ApplicationId) {
        self.last_application = Some(application);
    }

    /// Consume a pending page transition exactly once.
    pub const fn take_transition(&mut self) -> Option<TransitionDirection> {
        self.transition.take()
    }

    /// Return directly to the root page and clear navigation history.
    pub fn go_home(&mut self) {
        if self.page != PageId::Home {
            self.page = PageId::Home;
            self.history_len = 0;
            self.transition = Some(TransitionDirection::Backward);
        }
    }

    /// Process one input event.
    ///
    /// Directional keys respond on the debounced press transition for a crisp
    /// feel. Confirm/back use exclusive click, double-click and long-press
    /// gestures, so their commands cannot fire twice.
    pub fn handle_event(&mut self, event: KeyEvent, settings: &mut Settings) -> MenuEffect {
        match (event.key, event.gesture) {
            (Key::Up, Gesture::Pressed | Gesture::Repeat) => {
                self.move_selection(-1);
                MenuEffect::None
            }
            (Key::Down, Gesture::Pressed | Gesture::Repeat) => {
                self.move_selection(1);
                MenuEffect::None
            }
            (Key::Left, Gesture::Pressed | Gesture::Repeat) => {
                if let MenuAction::Adjust(setting) = self.selected_entry().action {
                    adjust_setting(settings, setting, -1);
                    MenuEffect::SettingsChanged(*settings)
                } else {
                    self.navigate_back();
                    MenuEffect::None
                }
            }
            (Key::Right, Gesture::Pressed | Gesture::Repeat) => {
                if let MenuAction::Adjust(setting) = self.selected_entry().action {
                    adjust_setting(settings, setting, 1);
                    MenuEffect::SettingsChanged(*settings)
                } else {
                    self.activate(settings)
                }
            }
            (Key::Enter, Gesture::Click) => self.activate(settings),
            (Key::Enter | Key::Jump, Gesture::DoubleClick) => self
                .last_application
                .map_or(MenuEffect::None, MenuEffect::Launch),
            (Key::Function, Gesture::Pressed) => {
                self.open(PageId::Settings);
                MenuEffect::None
            }
            (Key::Back, Gesture::Click) => {
                if self.page == PageId::Home {
                    MenuEffect::Standby
                } else {
                    self.navigate_back();
                    MenuEffect::None
                }
            }
            (Key::Back, Gesture::DoubleClick) => {
                self.go_home();
                MenuEffect::None
            }
            (Key::Back, Gesture::LongPress) => MenuEffect::Standby,
            _ => MenuEffect::None,
        }
    }

    fn move_selection(&mut self, direction: i8) {
        let count = self.current_page().entries.len();
        let current = self.selected_index();
        let next = if direction < 0 {
            current.checked_sub(1).unwrap_or(count - 1)
        } else {
            (current + 1) % count
        };
        self.selected[self.page.index()] = next as u8;
    }

    fn activate(&mut self, settings: &mut Settings) -> MenuEffect {
        match self.selected_entry().action {
            MenuAction::Open(page) => {
                self.open(page);
                MenuEffect::None
            }
            MenuAction::Back => {
                self.navigate_back();
                MenuEffect::None
            }
            MenuAction::Launch(application) => MenuEffect::Launch(application),
            MenuAction::Standby => MenuEffect::Standby,
            MenuAction::Adjust(setting) => {
                adjust_setting(settings, setting, 1);
                MenuEffect::SettingsChanged(*settings)
            }
        }
    }

    fn open(&mut self, target: PageId) {
        if target == self.page {
            return;
        }
        let index = usize::from(self.history_len);
        if index < self.history.len() {
            self.history[index] = self.page;
            self.history_len += 1;
        }
        self.page = target;
        self.transition = Some(TransitionDirection::Forward);
    }

    fn navigate_back(&mut self) {
        if self.history_len == 0 {
            self.go_home();
            return;
        }
        self.history_len -= 1;
        self.page = self.history[usize::from(self.history_len)];
        self.transition = Some(TransitionDirection::Backward);
    }
}

fn adjust_setting(settings: &mut Settings, setting: SettingId, direction: i8) {
    match setting {
        SettingId::Sound => settings.sound_enabled = !settings.sound_enabled,
        SettingId::Animation => {
            settings.animation_speed = if direction < 0 {
                settings.animation_speed.previous()
            } else {
                settings.animation_speed.next()
            };
        }
        SettingId::Cursor => {
            settings.cursor_style = if direction < 0 {
                settings.cursor_style.previous()
            } else {
                settings.cursor_style.next()
            };
        }
        SettingId::StandbyRefresh => {
            let delta = if direction < 0 { -2_i16 } else { 2_i16 };
            settings.standby_refresh_seconds = settings
                .standby_refresh_seconds
                .saturating_add_signed(delta)
                .clamp(
                    Settings::MIN_STANDBY_REFRESH_SECONDS,
                    Settings::MAX_STANDBY_REFRESH_SECONDS,
                );
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn event(key: Key, gesture: Gesture) -> KeyEvent {
        KeyEvent::new(key, gesture, 100)
    }

    #[test]
    fn directional_press_wraps_without_waiting_for_click() {
        let mut menu = MenuModel::default();
        let mut settings = Settings::default();
        menu.handle_event(event(Key::Up, Gesture::Pressed), &mut settings);
        assert_eq!(menu.selected_index(), HOME_ENTRIES.len() - 1);
        menu.handle_event(event(Key::Down, Gesture::Pressed), &mut settings);
        assert_eq!(menu.selected_index(), 0);
    }

    #[test]
    fn navigation_preserves_each_pages_selection() {
        let mut menu = MenuModel::default();
        let mut settings = Settings::default();
        menu.handle_event(event(Key::Enter, Gesture::Click), &mut settings);
        assert_eq!(menu.page_id(), PageId::Games);
        menu.handle_event(event(Key::Down, Gesture::Pressed), &mut settings);
        assert_eq!(menu.selected_index(), 1);
        menu.handle_event(event(Key::Back, Gesture::Click), &mut settings);
        assert_eq!(menu.page_id(), PageId::Home);
        menu.handle_event(event(Key::Enter, Gesture::Click), &mut settings);
        assert_eq!(menu.selected_index(), 1);
    }

    #[test]
    fn settings_are_changed_by_value_not_global_state() {
        let mut menu = MenuModel::default();
        let mut settings = Settings::default();
        menu.open(PageId::Settings);
        menu.move_selection(1);
        let effect = menu.handle_event(event(Key::Right, Gesture::Pressed), &mut settings);
        assert!(!settings.sound_enabled);
        assert_eq!(effect, MenuEffect::SettingsChanged(settings));
    }

    #[test]
    fn double_back_returns_home_without_activating_standby() {
        let mut menu = MenuModel::default();
        let mut settings = Settings::default();
        menu.open(PageId::Tools);
        let effect = menu.handle_event(event(Key::Back, Gesture::DoubleClick), &mut settings);
        assert_eq!(effect, MenuEffect::None);
        assert_eq!(menu.page_id(), PageId::Home);
    }
}
