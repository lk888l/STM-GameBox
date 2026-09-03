//! Static product information architecture and bounded menu selection state.

/// Every renderable product view.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum View {
    /// Four-card product home.
    #[default]
    Home = 0,
    /// Six-game list.
    Games = 1,
    /// Utility list.
    Tools = 2,
    /// RTC dashboard and editor.
    Clock = 3,
    /// User settings list.
    Settings = 4,
    /// Snake game.
    Snake = 5,
    /// Dinosaur runner.
    Dino = 6,
    /// Side-scrolling shooter.
    AirRaid = 7,
    /// Falling-block puzzle.
    Tetris = 8,
    /// Two-player Pong.
    Pong = 9,
    /// Eight-key piano.
    Piano = 10,
    /// Stopwatch utility.
    Stopwatch = 11,
    /// Countdown utility.
    Countdown = 12,
    /// Live button-event inspector.
    InputLab = 13,
    /// Runtime health page.
    System = 14,
    /// Product information page.
    About = 15,
}

impl View {
    /// Number of concrete views.
    pub const COUNT: usize = 16;

    /// Stable array index.
    #[must_use]
    pub const fn index(self) -> usize {
        self as usize
    }
}

/// Small procedural icon identity.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Icon {
    /// Game controller.
    Gamepad,
    /// Crossed tools.
    Tools,
    /// Clock face.
    Clock,
    /// Settings gear.
    Settings,
    /// Snake line.
    Snake,
    /// Pixel dinosaur.
    Dino,
    /// Aircraft.
    Plane,
    /// Tetromino.
    Tetris,
    /// Pong court.
    Pong,
    /// Piano keys.
    Piano,
    /// Stopwatch.
    Stopwatch,
    /// Countdown timer.
    Timer,
    /// Button tester.
    Buttons,
    /// MCU/system chip.
    Chip,
    /// Information mark.
    Info,
    /// Speaker.
    Speaker,
    /// Animation/motion.
    Motion,
    /// Display brightness.
    Brightness,
}

/// Behavior of one menu entry.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Action {
    /// Open the target view.
    Open,
    /// Toggle sound feedback.
    ToggleSound,
    /// Cycle the motion level.
    CycleMotion,
    /// Cycle OLED brightness.
    CycleBrightness,
    /// Cycle home-header content.
    CycleHomeHeader,
}

/// Immutable menu entry.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MenuEntry {
    /// Uppercase display label.
    pub label: &'static str,
    /// Short explanatory subtitle.
    pub subtitle: &'static str,
    /// Procedural icon identity.
    pub icon: Icon,
    /// Activation behavior.
    pub action: Action,
    /// Destination for [`Action::Open`].
    pub target: View,
}

/// Immutable menu-page descriptor.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MenuDefinition {
    /// Compact title rail text.
    pub title: &'static str,
    /// Ordered menu entries.
    pub entries: &'static [MenuEntry],
}

const HOME_ENTRIES: [MenuEntry; 4] = [
    MenuEntry {
        label: "GAMES",
        subtitle: "6 classics",
        icon: Icon::Gamepad,
        action: Action::Open,
        target: View::Games,
    },
    MenuEntry {
        label: "TOOLS",
        subtitle: "Time & test",
        icon: Icon::Tools,
        action: Action::Open,
        target: View::Tools,
    },
    MenuEntry {
        label: "CLOCK",
        subtitle: "RTC dashboard",
        icon: Icon::Clock,
        action: Action::Open,
        target: View::Clock,
    },
    MenuEntry {
        label: "SETTINGS",
        subtitle: "Tune the box",
        icon: Icon::Settings,
        action: Action::Open,
        target: View::Settings,
    },
];

const GAME_ENTRIES: [MenuEntry; 6] = [
    MenuEntry {
        label: "DINO",
        subtitle: "Jump & survive",
        icon: Icon::Dino,
        action: Action::Open,
        target: View::Dino,
    },
    MenuEntry {
        label: "SNAKE",
        subtitle: "Eat and grow",
        icon: Icon::Snake,
        action: Action::Open,
        target: View::Snake,
    },
    MenuEntry {
        label: "AIR RAID",
        subtitle: "Dodge and fire",
        icon: Icon::Plane,
        action: Action::Open,
        target: View::AirRaid,
    },
    MenuEntry {
        label: "TETRIS",
        subtitle: "Clear the lines",
        icon: Icon::Tetris,
        action: Action::Open,
        target: View::Tetris,
    },
    MenuEntry {
        label: "PONG 2P",
        subtitle: "Two players",
        icon: Icon::Pong,
        action: Action::Open,
        target: View::Pong,
    },
    MenuEntry {
        label: "PIANO",
        subtitle: "Eight-key synth",
        icon: Icon::Piano,
        action: Action::Open,
        target: View::Piano,
    },
];

const TOOL_ENTRIES: [MenuEntry; 4] = [
    MenuEntry {
        label: "STOPWATCH",
        subtitle: "Monotonic timer",
        icon: Icon::Stopwatch,
        action: Action::Open,
        target: View::Stopwatch,
    },
    MenuEntry {
        label: "COUNTDOWN",
        subtitle: "Adjustable timer",
        icon: Icon::Timer,
        action: Action::Open,
        target: View::Countdown,
    },
    MenuEntry {
        label: "INPUT LAB",
        subtitle: "Button events",
        icon: Icon::Buttons,
        action: Action::Open,
        target: View::InputLab,
    },
    MenuEntry {
        label: "SYSTEM",
        subtitle: "Runtime health",
        icon: Icon::Chip,
        action: Action::Open,
        target: View::System,
    },
];

const SETTING_ENTRIES: [MenuEntry; 5] = [
    MenuEntry {
        label: "SOUND",
        subtitle: "Buzzer feedback",
        icon: Icon::Speaker,
        action: Action::ToggleSound,
        target: View::Settings,
    },
    MenuEntry {
        label: "MOTION",
        subtitle: "Animation level",
        icon: Icon::Motion,
        action: Action::CycleMotion,
        target: View::Settings,
    },
    MenuEntry {
        label: "BRIGHTNESS",
        subtitle: "OLED contrast",
        icon: Icon::Brightness,
        action: Action::CycleBrightness,
        target: View::Settings,
    },
    MenuEntry {
        label: "HOME HEADER",
        subtitle: "Time date pet title",
        icon: Icon::Clock,
        action: Action::CycleHomeHeader,
        target: View::Settings,
    },
    MenuEntry {
        label: "ABOUT",
        subtitle: "Firmware details",
        icon: Icon::Info,
        action: Action::Open,
        target: View::About,
    },
];

const HOME: MenuDefinition = MenuDefinition {
    title: "GAMEBOX",
    entries: &HOME_ENTRIES,
};
const GAMES: MenuDefinition = MenuDefinition {
    title: "GAMES",
    entries: &GAME_ENTRIES,
};
const TOOLS: MenuDefinition = MenuDefinition {
    title: "TOOLS",
    entries: &TOOL_ENTRIES,
};
const SETTINGS: MenuDefinition = MenuDefinition {
    title: "SETTINGS",
    entries: &SETTING_ENTRIES,
};

/// Look up a menu descriptor; non-menu views return `None`.
#[must_use]
pub const fn menu_for(view: View) -> Option<&'static MenuDefinition> {
    match view {
        View::Home => Some(&HOME),
        View::Games => Some(&GAMES),
        View::Tools => Some(&TOOLS),
        View::Settings => Some(&SETTINGS),
        _ => None,
    }
}

/// Whether a view is backed by a menu table.
#[must_use]
pub const fn is_menu(view: View) -> bool {
    matches!(
        view,
        View::Home | View::Games | View::Tools | View::Settings
    )
}

/// Whether a view is one of the six games.
#[must_use]
pub const fn is_game(view: View) -> bool {
    matches!(
        view,
        View::Snake | View::Dino | View::AirRaid | View::Tetris | View::Pong | View::Piano
    )
}

/// Per-page menu selection memory.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MenuModel {
    selections: [u8; View::COUNT],
}

impl Default for MenuModel {
    fn default() -> Self {
        Self {
            selections: [0; View::COUNT],
        }
    }
}

impl MenuModel {
    /// Selected row for a menu view, or zero for a non-menu view.
    #[must_use]
    pub const fn selection(&self, view: View) -> u8 {
        self.selections[view.index()]
    }

    /// Selected entry for a menu view.
    #[must_use]
    pub fn selected_entry(&self, view: View) -> Option<&'static MenuEntry> {
        let menu = menu_for(view)?;
        Some(&menu.entries[self.selection(view) as usize])
    }

    /// Move a selection with wraparound and return its previous value.
    pub fn move_selection(&mut self, view: View, delta: i8) -> Option<u8> {
        let menu = menu_for(view)?;
        let index = view.index();
        let old = self.selections[index];
        let count = menu.entries.len() as u8;
        self.selections[index] = if delta < 0 {
            if old == 0 { count - 1 } else { old - 1 }
        } else if old + 1 >= count {
            0
        } else {
            old + 1
        };
        Some(old)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn all_six_games_are_real_targets() {
        let games = menu_for(View::Games).unwrap();
        assert_eq!(games.entries.len(), 6);
        assert!(games.entries.iter().all(|entry| is_game(entry.target)));
    }

    #[test]
    fn selection_wraps_and_is_remembered_per_page() {
        let mut menu = MenuModel::default();
        menu.move_selection(View::Home, -1);
        menu.move_selection(View::Games, 1);
        assert_eq!(menu.selection(View::Home), 3);
        assert_eq!(menu.selection(View::Games), 1);
    }
}
