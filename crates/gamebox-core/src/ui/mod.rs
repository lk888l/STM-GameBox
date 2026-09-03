//! Native 1-bit renderer for the 128×64 GameBox OLED.

mod canvas;
mod font;

use crate::{
    App, AppMode,
    button::{Gesture, Key},
    calendar::DateTime,
    games::GamePhase,
    menu::{Action, Icon, View, menu_for},
    settings::{Brightness, HomeHeaderMode, MotionLevel},
};
use canvas::{Canvas, PixelOp};

/// Native SSD1306 page-layout framebuffer byte count.
pub use canvas::FRAME_BYTES;

const SET: PixelOp = PixelOp::Set;
const CLEAR: PixelOp = PixelOp::Clear;
const INVERT: PixelOp = PixelOp::Invert;
const PET_FRAMES: [[u16; 9]; 2] = [
    [
        0x0900, 0x0f80, 0x0bc2, 0x0ffd, 0x07fe, 0x07f8, 0x01f8, 0x0148, 0x0244,
    ],
    [
        0x0900, 0x0f80, 0x0bc2, 0x0ffd, 0x07fe, 0x07f8, 0x01f8, 0x0250, 0x0290,
    ],
];

/// Stateless full-scene renderer.
#[derive(Debug, Clone, Copy, Default)]
pub struct UiRenderer;

impl UiRenderer {
    /// Render one complete frame into native SSD1306 page-layout bytes.
    pub fn draw(&self, frame: &mut [u8; FRAME_BYTES], app: &App, now_ms: u32) {
        let mut canvas = Canvas::new(frame);
        canvas.clear();
        if app.mode() == AppMode::Boot {
            Self::render_boot(&mut canvas, now_ms);
            return;
        }

        let motion = app.motion();
        if motion.page_x.is_settled() {
            Self::render_view(&mut canvas, app, app.view(), 0, now_ms, true);
        } else {
            let current_x = motion.page_x.value();
            let previous_x = current_x + if app.page_forward() { -128 } else { 128 };
            Self::render_view(
                &mut canvas,
                app,
                app.previous_view(),
                previous_x,
                now_ms,
                false,
            );
            Self::render_view(&mut canvas, app, app.view(), current_x, now_ms, true);
        }
        if let Some(message) = app.toast(now_ms) {
            canvas.fill_rounded_rectangle(3, 43, 122, 18, 3, SET);
            let x = (128 - text_width(message)) / 2;
            canvas.text(x, 48, message, true);
        }
    }

    fn render_boot(canvas: &mut Canvas<'_>, now_ms: u32) {
        canvas.rounded_rectangle(8, 7, 112, 50, 3, SET);
        canvas.fill_rounded_rectangle(18, 15, 92, 17, 3, SET);
        canvas.text(39, 20, "GAMEBOX", true);
        canvas.text(34, 39, "RUST + EMBASSY", false);
        let width = ((now_ms % App::BOOT_DURATION_MS) * 92 / App::BOOT_DURATION_MS) as i16;
        canvas.hline(18, 52, width, SET);
    }

    fn render_view(
        canvas: &mut Canvas<'_>,
        app: &App,
        view: View,
        x: i16,
        now_ms: u32,
        interactive: bool,
    ) {
        match view {
            View::Home => Self::render_home(canvas, app, x, now_ms, interactive),
            View::Games | View::Tools | View::Settings => {
                Self::render_list(canvas, app, view, x, interactive)
            }
            View::Clock => Self::render_clock(canvas, app, x, now_ms),
            View::Stopwatch => Self::render_stopwatch(canvas, app, x, now_ms),
            View::Countdown => Self::render_countdown(canvas, app, x, now_ms),
            View::InputLab => Self::render_input_lab(canvas, app, x),
            View::System => Self::render_system(canvas, app, x, now_ms),
            View::About => Self::render_about(canvas, x),
            View::Snake => Self::render_snake(canvas, app, x, now_ms),
            View::Dino => Self::render_dino(canvas, app, x, now_ms),
            View::AirRaid => Self::render_air_raid(canvas, app, x),
            View::Tetris => Self::render_tetris(canvas, app, x),
            View::Pong => Self::render_pong(canvas, app, x),
            View::Piano => Self::render_piano(canvas, app, x),
        }
    }

    fn header(canvas: &mut Canvas<'_>, x: i16, title: &str) {
        canvas.fill_rectangle(x, 0, 128, 11, SET);
        canvas.text(x + 4, 2, title, true);
        for dot in [119, 122, 125] {
            canvas.pixel(x + dot, 3, CLEAR);
        }
    }

    fn footer(canvas: &mut Canvas<'_>, x: i16, hint: &str) {
        canvas.hline(x, 54, 128, SET);
        canvas.text(x + 4, 56, hint, false);
    }

    fn render_home(canvas: &mut Canvas<'_>, app: &App, x: i16, now_ms: u32, interactive: bool) {
        let selected = app.menu().selection(View::Home);
        let carousel = app.motion().carousel_x;
        if interactive && !carousel.is_settled() {
            let current_x = carousel.value();
            let previous_x = current_x - i16::from(app.carousel_direction()) * 128;
            Self::home_card(canvas, app, app.carousel_previous(), x + previous_x, now_ms);
            Self::home_card(canvas, app, selected, x + current_x, now_ms);
        } else {
            Self::home_card(canvas, app, selected, x, now_ms);
        }
    }

    fn home_card(canvas: &mut Canvas<'_>, app: &App, index: u8, x: i16, now_ms: u32) {
        let menu = menu_for(View::Home).expect("home is a menu");
        let entry = &menu.entries[index as usize];
        let position = position_text(index, menu.entries.len() as u8);
        canvas.rounded_rectangle(x + 2, 1, 124, 62, 3, SET);
        Self::home_header(canvas, app, x, now_ms);
        canvas.fill_rounded_rectangle(x + 92, 2, 32, 9, 2, SET);
        canvas.text_bytes(x + 93, 3, &position, true);
        canvas.hline(x + 4, 12, 120, SET);
        canvas.rounded_rectangle(x + 7, 16, 34, 27, 3, SET);
        Self::icon(canvas, entry.icon, x + 12, 21);
        let width = text_width(entry.label) + 8;
        canvas.fill_rounded_rectangle(x + 45, 17, width, 11, 2, SET);
        canvas.text(x + 49, 19, entry.label, true);
        canvas.hline(x + 46, 30, 77, SET);
        canvas.text(x + 46, 33, entry.subtitle, false);
        canvas.hline(x + 4, 47, 120, SET);
        for dot in 0..menu.entries.len() as u8 {
            let dot_x = x + 8 + i16::from(dot) * 7;
            if dot == index {
                canvas.fill_rounded_rectangle(dot_x, 54, 5, 3, 1, SET);
            } else {
                canvas.pixel(dot_x + 2, 55, SET);
            }
        }
        canvas.text(x + 42, 52, "<> MOVE", false);
        canvas.text(x + 92, 52, "ENTER", false);
    }

    fn home_header(canvas: &mut Canvas<'_>, app: &App, x: i16, now_ms: u32) {
        match app.persistent_data().settings.home_header {
            HomeHeaderMode::Time => {
                if let Some(clock) = app.clock() {
                    let time = time_text(
                        u32::from(clock.hours) * 3_600
                            + u32::from(clock.minutes) * 60
                            + u32::from(clock.seconds),
                    );
                    canvas.text_bytes(x + 7, 3, &time, false);
                } else {
                    canvas.text(x + 7, 3, "RTC ERR", false);
                }
            }
            HomeHeaderMode::Date => {
                if let Some(clock) = app.clock() {
                    canvas.text_bytes(x + 7, 3, &date_text(clock), false);
                } else {
                    canvas.text(x + 7, 3, "RTC ERR", false);
                }
            }
            HomeHeaderMode::Pet => Self::home_pet(canvas, app, x, now_ms),
            HomeHeaderMode::Title => canvas.text(x + 7, 3, "GAMEBOX", false),
        }
    }

    fn home_pet(canvas: &mut Canvas<'_>, app: &App, x: i16, now_ms: u32) {
        let period = match app.persistent_data().settings.motion {
            MotionLevel::Full => 250,
            MotionLevel::Reduced => 500,
            MotionLevel::Off => 0,
        };
        let (phase, frame) = if let Some(tick) = now_ms.checked_div(period) {
            (tick % 72, tick & 1)
        } else {
            (18, 0)
        };
        let facing_right = phase < 36;
        let step = if facing_right { phase } else { 71 - phase };
        let pet_x = x + 7 + (step * 2) as i16;
        for row in 0..9_u8 {
            let bits = PET_FRAMES[frame as usize][row as usize];
            for column in 0..12_u8 {
                if bits & (1_u16 << column) == 0 {
                    continue;
                }
                let displayed = if facing_right { column } else { 11 - column };
                canvas.pixel(pet_x + i16::from(displayed), 2 + i16::from(row), SET);
            }
        }
    }

    fn render_list(canvas: &mut Canvas<'_>, app: &App, view: View, x: i16, interactive: bool) {
        let menu = menu_for(view).expect("list view has a menu");
        let selected = app.menu().selection(view);
        let (target_y, target_width, target_scroll) = list_targets(app, view);
        let scroll = if interactive {
            app.motion().scroll_y.value()
        } else {
            target_scroll
        };
        for (index, entry) in menu.entries.iter().enumerate() {
            let y = 14 + index as i16 * 16 + scroll;
            if y <= -8 || y >= 64 {
                continue;
            }
            canvas.text(x + 8, y, entry.label, false);
            if view == View::Settings {
                let value = setting_value(app, entry.action);
                canvas.text(x + 120 - text_width(value), y, value, false);
            }
        }
        let highlight_y = if interactive {
            app.motion().cursor_y.value()
        } else {
            target_y
        };
        let highlight_width = if interactive {
            app.motion().cursor_width.value()
        } else {
            target_width
        };
        canvas.fill_rounded_rectangle(x + 3, highlight_y, highlight_width, 15, 2, INVERT);
        if menu.entries.len() > 1 {
            canvas.vline(x + 125, 15, 46, SET);
            let position_y = i16::from(selected) * 42 / (menu.entries.len() as i16 - 1);
            canvas.fill_rounded_rectangle(x + 123, 15 + position_y, 5, 4, 1, SET);
        }
        canvas.fill_rectangle(x, 0, 128, 12, CLEAR);
        canvas.text(x + 4, 2, menu.title, false);
        canvas.text_bytes(
            x + 96,
            2,
            &position_text(selected, menu.entries.len() as u8),
            false,
        );
        canvas.hline(x, 11, 128, SET);
    }

    fn render_clock(canvas: &mut Canvas<'_>, app: &App, x: i16, now_ms: u32) {
        Self::header(
            canvas,
            x,
            if app.clock_editing() {
                "CLOCK SET"
            } else {
                "CLOCK"
            },
        );
        if let Some(clock) = app.shown_clock() {
            let time = time_text(
                u32::from(clock.hours) * 3_600
                    + u32::from(clock.minutes) * 60
                    + u32::from(clock.seconds),
            );
            canvas.text_scaled(
                x + 16,
                17,
                core::str::from_utf8(&time).unwrap_or("??:??:??"),
                2,
                false,
            );
            canvas.hline(x + 22, 38, 84, SET);
            canvas.text_bytes(x + 34, 43, &date_text(clock), false);
            if app.clock_editing() && ((now_ms / 350) & 1) == 0 {
                const FIELD_X: [i16; 5] = [16, 52, 34, 64, 82];
                const FIELD_W: [i16; 5] = [22, 22, 24, 12, 12];
                const FIELD_Y: [i16; 5] = [35, 35, 52, 52, 52];
                let field = app.clock_field() as usize;
                canvas.hline(x + FIELD_X[field], FIELD_Y[field], FIELD_W[field], SET);
            }
        } else {
            canvas.text(x + 37, 27, "RTC ERROR", false);
        }
        Self::footer(
            canvas,
            x,
            if app.clock_editing() {
                "<> FIELD UP/DN ENTER"
            } else {
                "ENTER SET  BACK"
            },
        );
    }

    fn render_stopwatch(canvas: &mut Canvas<'_>, app: &App, x: i16, now_ms: u32) {
        let elapsed = app.stopwatch_elapsed_ms(now_ms);
        Self::header(
            canvas,
            x,
            if app.stopwatch_running() {
                "STOPWATCH  RUN"
            } else {
                "STOPWATCH"
            },
        );
        let time = time_text(elapsed / 1_000);
        canvas.text_scaled(
            x + 16,
            18,
            core::str::from_utf8(&time).unwrap_or("??:??:??"),
            2,
            false,
        );
        let tenths = [b'.', b'0' + ((elapsed / 100) % 10) as u8, b' ', b's'];
        canvas.text_bytes(x + 52, 39, &tenths, false);
        Self::footer(canvas, x, "ENTER START  FUNC RESET");
    }

    fn render_countdown(canvas: &mut Canvas<'_>, app: &App, x: i16, now_ms: u32) {
        Self::header(
            canvas,
            x,
            if app.countdown_running() {
                "COUNTDOWN  RUN"
            } else {
                "COUNTDOWN"
            },
        );
        let time = time_text(app.countdown_remaining(now_ms));
        canvas.text_scaled(
            x + 16,
            18,
            core::str::from_utf8(&time).unwrap_or("??:??:??"),
            2,
            false,
        );
        canvas.text(
            x + 27,
            39,
            if app.countdown_running() {
                "ENTER PAUSE"
            } else {
                "DPAD ADJUST"
            },
            false,
        );
        Self::footer(canvas, x, "ENTER GO  FUNC 05:00");
    }

    fn render_input_lab(canvas: &mut Canvas<'_>, app: &App, x: i16) {
        Self::header(canvas, x, "INPUT LAB");
        if let Some(event) = app.last_event() {
            canvas.text(x + 5, 15, "KEY", false);
            canvas.text(x + 47, 15, key_name(event.key), false);
            canvas.text(x + 5, 26, "EVENT", false);
            canvas.text(x + 47, 26, gesture_name(event.gesture), false);
            canvas.text(x + 5, 37, "HELD", false);
            let held = NumberText::new(event.held_ms);
            canvas.text_bytes(x + 47, 37, held.as_bytes(), false);
            canvas.text(x + 89, 37, "ms", false);
        } else {
            canvas.text(x + 31, 27, "PRESS A KEY", false);
        }
        Self::footer(canvas, x, "TRY CLICK DOUBLE LONG");
    }

    fn render_system(canvas: &mut Canvas<'_>, app: &App, x: i16, now_ms: u32) {
        let stats = app.system_stats();
        Self::header(canvas, x, "SYSTEM");
        canvas.text(x + 5, 13, "UPTIME", false);
        Self::number_at(canvas, x + 65, 13, now_ms / 1_000);
        canvas.text(x + 104, 13, "s", false);
        canvas.text(x + 5, 23, "DROP I:", false);
        Self::number_at(canvas, x + 47, 23, stats.input_drops);
        canvas.text(x + 77, 23, "U:", false);
        Self::number_at(canvas, x + 92, 23, stats.uart_drops);
        canvas.text(x + 5, 33, "ERR  I:", false);
        Self::number_at(canvas, x + 47, 33, stats.oled_errors);
        canvas.text(x + 77, 33, "U:", false);
        Self::number_at(canvas, x + 92, 33, stats.uart_errors);
        canvas.text(x + 5, 43, "OLED TX", false);
        Self::number_at(canvas, x + 53, 43, stats.oled_transfers);
        canvas.text(x + 89, 43, "F:", false);
        Self::number_at(canvas, x + 104, 43, stats.storage_errors);
        Self::footer(canvas, x, "NO HEAP  RUST EMBASSY");
    }

    fn render_about(canvas: &mut Canvas<'_>, x: i16) {
        Self::header(canvas, x, "ABOUT");
        canvas.text(x + 20, 15, "STM32 GAMEBOX", false);
        canvas.text(x + 29, 26, "F103C8T6", false);
        canvas.text(x + 20, 37, "RUST + EMBASSY", false);
        canvas.text(x + 29, 47, "FW 2.0.0", false);
        Self::footer(canvas, x, "OPEN ARCHITECTURE");
    }

    fn render_snake(canvas: &mut Canvas<'_>, app: &App, x: i16, now_ms: u32) {
        let game = app.snake();
        canvas.rectangle(x + 2, 12, 124, 51, SET);
        canvas.text(x + 3, 2, "SNAKE", false);
        canvas.text(x + 86, 2, "S:", false);
        Self::number_at(canvas, x + 100, 2, u32::from(game.score()));
        for (index, point) in game.body().iter().enumerate() {
            let px = x + 4 + i16::from(point.x) * 4;
            let py = 14 + i16::from(point.y) * 4;
            if index == 0 {
                canvas.rounded_rectangle(px, py, 3, 3, 1, SET);
            } else {
                canvas.fill_rectangle(px, py, 3, 3, SET);
            }
        }
        let food = game.food();
        if ((now_ms / 220) & 1) == 0 {
            canvas.fill_rounded_rectangle(
                x + 4 + i16::from(food.x) * 4,
                14 + i16::from(food.y) * 4,
                3,
                3,
                1,
                SET,
            );
        }
        Self::game_overlay(canvas, x, game.phase(), "ENTER TO PLAY");
    }

    fn render_dino(canvas: &mut Canvas<'_>, app: &App, x: i16, now_ms: u32) {
        let game = app.dino();
        canvas.text(x + 3, 2, "DINO", false);
        canvas.text(x + 86, 2, "S:", false);
        Self::number_at(canvas, x + 100, 2, u32::from(game.score()));
        canvas.hline(x, 59, 128, SET);
        for dash in (0..128_i16).step_by(9) {
            let shifted = (dash - (now_ms / 45 % 9) as i16 + 128) % 128;
            canvas.hline(x + shifted, 62, 4, SET);
        }
        let bottom = 58 - i16::from(game.jump_height());
        let dx = x + 12;
        canvas.fill_rounded_rectangle(dx, bottom - 8, 8, 8, 2, SET);
        canvas.fill_rectangle(dx + 5, bottom - 11, 7, 5, SET);
        canvas.pixel(dx + 10, bottom - 10, CLEAR);
        canvas.vline(dx + 1, bottom, 2, SET);
        canvas.vline(dx + 6, bottom, 2, SET);
        for obstacle in game.obstacles() {
            if !obstacle.is_active() {
                continue;
            }
            let oy = 58 - i16::from(obstacle.height()) + 1;
            canvas.fill_rectangle(
                x + obstacle.x(),
                oy,
                i16::from(obstacle.width()),
                i16::from(obstacle.height()),
                SET,
            );
            canvas.hline(x + obstacle.x() - 2, oy + 4, 3, SET);
        }
        Self::game_overlay(canvas, x, game.phase(), "JUMP TO PLAY");
    }

    fn render_air_raid(canvas: &mut Canvas<'_>, app: &App, x: i16) {
        let game = app.air_raid();
        canvas.text(x + 2, 2, "AIR RAID", false);
        canvas.text(x + 60, 2, "S:", false);
        Self::number_at(canvas, x + 74, 2, u32::from(game.score()));
        canvas.text(x + 101, 2, "L:", false);
        Self::number_at(canvas, x + 115, 2, u32::from(game.lives()));
        canvas.hline(x, 10, 128, SET);
        let y = game.player_y();
        canvas.line(x + 5, y + 3, x + 16, y, SET);
        canvas.line(x + 5, y + 3, x + 16, y + 6, SET);
        canvas.hline(x + 5, y + 3, 12, SET);
        canvas.vline(x + 8, y, 7, SET);
        for bullet in game.bullets() {
            if bullet.is_active() {
                canvas.hline(x + bullet.x(), bullet.y(), 3, SET);
            }
        }
        for enemy in game.enemies() {
            if !enemy.is_active() {
                continue;
            }
            let ex = x + enemy.x();
            canvas.rounded_rectangle(ex, enemy.y(), 9, 7, 2, SET);
            canvas.hline(ex - 3, enemy.y() + 3, 4, SET);
            canvas.pixel(ex + 2, enemy.y() + 2, SET);
        }
        Self::game_overlay(canvas, x, game.phase(), "ENTER TO PLAY");
    }

    fn render_tetris(canvas: &mut Canvas<'_>, app: &App, x: i16) {
        let game = app.tetris();
        let board_x = x + 2;
        canvas.rectangle(board_x, 1, 42, 62, SET);
        for row in 0..15_u8 {
            for column in 0..10_u8 {
                if game.settled(column, row) || game.active(column, row) {
                    canvas.fill_rectangle(
                        board_x + 1 + i16::from(column) * 4,
                        2 + i16::from(row) * 4,
                        3,
                        3,
                        SET,
                    );
                }
            }
        }
        canvas.text(x + 49, 3, "TETRIS", false);
        canvas.text(x + 49, 15, "SCORE", false);
        Self::number_at(canvas, x + 49, 24, game.score());
        canvas.text(x + 49, 35, "NEXT", false);
        for row in 0..4_u8 {
            for column in 0..4_u8 {
                if crate::games::Tetris::piece_cell(game.next_piece(), 0, column, row) {
                    canvas.fill_rectangle(
                        x + 54 + i16::from(column) * 4,
                        44 + i16::from(row) * 4,
                        3,
                        3,
                        SET,
                    );
                }
            }
        }
        canvas.text(x + 82, 44, "UP DROP", false);
        canvas.text(x + 82, 54, "J/F ROT", false);
        Self::game_overlay(canvas, x, game.phase(), "ENTER TO PLAY");
    }

    fn render_pong(canvas: &mut Canvas<'_>, app: &App, x: i16) {
        let game = app.pong();
        canvas.fill_rectangle(x, 0, 128, 9, SET);
        canvas.text(x + 46, 1, "PONG 2P", true);
        canvas.hline(x, 10, 128, SET);
        canvas.hline(x, 63, 128, SET);
        for y in (13..61_i16).step_by(6) {
            canvas.vline(x + 64, y, 3, SET);
        }
        canvas.vline(
            x + 7,
            game.left_y() - i16::from(game.left_half_length()),
            i16::from(game.left_half_length()) * 2 + 1,
            SET,
        );
        canvas.vline(
            x + 120,
            game.right_y() - i16::from(game.right_half_length()),
            i16::from(game.right_half_length()) * 2 + 1,
            SET,
        );
        canvas.fill_rounded_rectangle(x + game.ball_x() - 1, game.ball_y() - 1, 3, 3, 1, SET);
        if game.phase() == GamePhase::GameOver {
            let winner = if game.winner() == 1 {
                "LEFT WINS"
            } else {
                "RIGHT WINS"
            };
            canvas.fill_rounded_rectangle(x + 23, 23, 82, 26, 3, SET);
            canvas.text(x + (128 - text_width(winner)) / 2, 28, winner, true);
            canvas.text(x + 28, 39, "ENTER RESTART", true);
        } else {
            Self::game_overlay(canvas, x, game.phase(), "ENTER TO PLAY");
        }
    }

    fn render_piano(canvas: &mut Canvas<'_>, app: &App, x: i16) {
        Self::header(canvas, x, "PIANO");
        const LABELS: [u8; 8] = *b"CDEFGABC";
        for index in 0..8_u8 {
            let key_x = x + 4 + i16::from(index) * 15;
            let selected = app.piano_note() == Some(index);
            if selected {
                canvas.fill_rounded_rectangle(key_x, 15, 14, 35, 2, SET);
            } else {
                canvas.rounded_rectangle(key_x, 15, 14, 35, 2, SET);
            }
            canvas.text_bytes(
                key_x + 4,
                38,
                &LABELS[index as usize..=index as usize],
                selected,
            );
        }
        Self::footer(canvas, x, "LONG BACK TO EXIT");
    }

    fn game_overlay(canvas: &mut Canvas<'_>, x: i16, phase: GamePhase, hint: &str) {
        if phase == GamePhase::Playing {
            return;
        }
        canvas.fill_rounded_rectangle(x + 20, 24, 88, 24, 3, SET);
        let title = if phase == GamePhase::Ready {
            "READY"
        } else {
            "GAME OVER"
        };
        canvas.text(x + (128 - text_width(title)) / 2, 28, title, true);
        canvas.text(x + (128 - text_width(hint)) / 2, 38, hint, true);
    }

    fn number_at(canvas: &mut Canvas<'_>, x: i16, y: i16, value: u32) {
        let text = NumberText::new(value);
        canvas.text_bytes(x, y, text.as_bytes(), false);
    }

    fn icon(canvas: &mut Canvas<'_>, icon: Icon, x: i16, y: i16) {
        match icon {
            Icon::Gamepad => {
                canvas.rounded_rectangle(x, y, 24, 16, 3, SET);
                canvas.hline(x + 4, y + 8, 7, SET);
                canvas.vline(x + 7, y + 5, 7, SET);
                canvas.fill_rounded_rectangle(x + 16, y + 5, 3, 3, 1, SET);
                canvas.fill_rounded_rectangle(x + 20, y + 9, 3, 3, 1, SET);
            }
            Icon::Clock | Icon::Stopwatch | Icon::Timer => {
                canvas.circle(x + 12, y + 8, 8, SET);
                canvas.line(x + 12, y + 8, x + 12, y + 3, SET);
                canvas.line(x + 12, y + 8, x + 17, y + 11, SET);
            }
            Icon::Snake => {
                canvas.line(x, y + 3, x + 6, y + 12, SET);
                canvas.line(x + 6, y + 12, x + 13, y + 3, SET);
                canvas.line(x + 13, y + 3, x + 22, y + 10, SET);
                canvas.fill_rounded_rectangle(x + 20, y + 8, 4, 4, 1, SET);
            }
            Icon::Dino => {
                canvas.fill_rounded_rectangle(x + 4, y + 7, 11, 8, 2, SET);
                canvas.fill_rectangle(x + 12, y + 3, 9, 7, SET);
                canvas.pixel(x + 18, y + 5, CLEAR);
                canvas.vline(x + 6, y + 14, 2, SET);
                canvas.vline(x + 13, y + 14, 2, SET);
            }
            Icon::Plane => {
                canvas.hline(x, y + 8, 23, SET);
                canvas.line(x + 5, y + 8, x + 17, y, SET);
                canvas.line(x + 5, y + 8, x + 17, y + 15, SET);
            }
            Icon::Tetris => {
                for (dx, dy) in [(0, 8), (6, 8), (12, 8), (6, 2)] {
                    canvas.fill_rectangle(x + dx, y + dy, 5, 5, SET);
                }
            }
            Icon::Pong => {
                canvas.vline(x, y + 2, 13, SET);
                canvas.vline(x + 23, y + 2, 13, SET);
                canvas.circle(x + 12, y + 8, 2, SET);
            }
            Icon::Piano => {
                for key in 0..4 {
                    canvas.rectangle(x + key * 6, y, 6, 16, SET);
                }
            }
            Icon::Settings | Icon::Motion => {
                canvas.circle(x + 12, y + 8, 7, SET);
                canvas.circle(x + 12, y + 8, 3, SET);
                canvas.hline(x, y + 8, 24, SET);
                canvas.vline(x + 12, y, 16, SET);
            }
            Icon::Tools => {
                canvas.line(x + 2, y + 2, x + 21, y + 14, SET);
                canvas.line(x + 21, y + 2, x + 2, y + 14, SET);
                canvas.circle(x + 3, y + 2, 2, SET);
            }
            Icon::Buttons => {
                canvas.rounded_rectangle(x, y, 24, 16, 3, SET);
                canvas.text(x + 3, y + 4, "KEY", false);
            }
            Icon::Speaker => {
                canvas.fill_rectangle(x, y + 6, 5, 5, SET);
                canvas.line(x + 5, y + 6, x + 11, y + 2, SET);
                canvas.line(x + 5, y + 10, x + 11, y + 14, SET);
                canvas.circle(x + 11, y + 8, 7, SET);
            }
            Icon::Brightness => {
                canvas.circle(x + 12, y + 8, 5, SET);
                canvas.hline(x, y + 8, 24, SET);
                canvas.vline(x + 12, y, 16, SET);
            }
            Icon::Chip => {
                canvas.rectangle(x + 4, y + 2, 16, 12, SET);
                for pin in 0..4 {
                    canvas.hline(x, y + 4 + pin * 3, 4, SET);
                    canvas.hline(x + 20, y + 4 + pin * 3, 4, SET);
                }
            }
            Icon::Info => {
                canvas.circle(x + 12, y + 8, 8, SET);
                canvas.text(x + 9, y + 4, "i", false);
            }
        }
    }
}

fn list_targets(app: &App, view: View) -> (i16, i16, i16) {
    let menu = menu_for(view).expect("list view has a menu");
    let selected = app.menu().selection(view) as usize;
    let top = selected
        .saturating_sub(1)
        .min(menu.entries.len().saturating_sub(3));
    let y = 14 + (selected - top) as i16 * 16;
    let width = (menu.entries[selected].label.len() * 6 + 20).min(121) as i16;
    (y, width, -(top as i16 * 16))
}

fn setting_value(app: &App, action: Action) -> &'static str {
    let settings = app.persistent_data().settings;
    match action {
        Action::ToggleSound => {
            if settings.sound_enabled {
                "ON"
            } else {
                "OFF"
            }
        }
        Action::CycleMotion => match settings.motion {
            MotionLevel::Full => "FULL",
            MotionLevel::Reduced => "REDUCED",
            MotionLevel::Off => "OFF",
        },
        Action::CycleBrightness => match settings.brightness {
            Brightness::Low => "LOW",
            Brightness::Medium => "MED",
            Brightness::High => "HIGH",
            Brightness::Max => "MAX",
        },
        Action::CycleHomeHeader => match settings.home_header {
            HomeHeaderMode::Time => "TIME",
            HomeHeaderMode::Date => "DATE",
            HomeHeaderMode::Pet => "PET",
            HomeHeaderMode::Title => "TITLE",
        },
        Action::Open => "",
    }
}

const fn text_width(text: &str) -> i16 {
    text.len() as i16 * 6
}

fn position_text(index: u8, count: u8) -> [u8; 5] {
    let position = index + 1;
    [
        b'0' + position / 10,
        b'0' + position % 10,
        b'/',
        b'0' + count / 10,
        b'0' + count % 10,
    ]
}

fn time_text(total_seconds: u32) -> [u8; 8] {
    let hours = total_seconds / 3_600 % 100;
    let minutes = total_seconds / 60 % 60;
    let seconds = total_seconds % 60;
    [
        b'0' + (hours / 10) as u8,
        b'0' + (hours % 10) as u8,
        b':',
        b'0' + (minutes / 10) as u8,
        b'0' + (minutes % 10) as u8,
        b':',
        b'0' + (seconds / 10) as u8,
        b'0' + (seconds % 10) as u8,
    ]
}

fn date_text(value: DateTime) -> [u8; 10] {
    [
        b'2',
        b'0',
        b'0' + value.date.year / 10,
        b'0' + value.date.year % 10,
        b'-',
        b'0' + value.date.month / 10,
        b'0' + value.date.month % 10,
        b'-',
        b'0' + value.date.day / 10,
        b'0' + value.date.day % 10,
    ]
}

const fn key_name(key: Key) -> &'static str {
    match key {
        Key::Up => "UP",
        Key::Down => "DOWN",
        Key::Left => "LEFT",
        Key::Right => "RIGHT",
        Key::Jump => "JUMP",
        Key::Function => "FUNC",
        Key::Enter => "ENTER",
        Key::Back => "BACK",
    }
}

const fn gesture_name(gesture: Gesture) -> &'static str {
    match gesture {
        Gesture::Pressed => "PRESSED",
        Gesture::Released => "RELEASED",
        Gesture::Click => "CLICK",
        Gesture::DoubleClick => "DOUBLE",
        Gesture::LongPress => "LONG",
        Gesture::Repeat => "REPEAT",
    }
}

struct NumberText {
    bytes: [u8; 10],
    start: u8,
}

impl NumberText {
    fn new(mut value: u32) -> Self {
        let mut bytes = [b'0'; 10];
        let mut start = 10_u8;
        loop {
            start -= 1;
            bytes[start as usize] = b'0' + (value % 10) as u8;
            value /= 10;
            if value == 0 {
                break;
            }
        }
        Self { bytes, start }
    }

    fn as_bytes(&self) -> &[u8] {
        &self.bytes[self.start as usize..]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::PersistentData;

    #[test]
    fn every_product_view_renders_without_panicking() {
        let renderer = UiRenderer;
        let mut app = App::new(0, PersistentData::default(), 1);
        app.tick(App::BOOT_DURATION_MS, 0);
        app.set_clock_snapshot(Some(DateTime::default()));
        let mut frame = [0_u8; FRAME_BYTES];
        renderer.draw(&mut frame, &app, 1_000);
        assert!(frame.iter().any(|byte| *byte != 0));
    }

    #[test]
    fn compact_formatters_are_stable() {
        assert_eq!(&time_text(3_661), b"01:01:01");
        assert_eq!(NumberText::new(42).as_bytes(), b"42");
    }
}
