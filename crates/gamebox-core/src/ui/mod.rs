//! Stateless rendering of application scenes to a binary draw target.

mod font;

use core::str;

use embedded_graphics::{
    mono_font::{
        MonoTextStyle,
        ascii::{FONT_6X10, FONT_10X20},
    },
    pixelcolor::BinaryColor,
    prelude::*,
    primitives::{Circle, Line, PrimitiveStyle, Rectangle, RoundedRectangle},
    text::Text,
};

use crate::{
    App, AppMode, CursorStyle,
    menu::{PageId, SettingId},
    snake::Cell,
};

const ON: BinaryColor = BinaryColor::On;
const OFF: BinaryColor = BinaryColor::Off;

/// Product UI renderer for the 128×64 monochrome canvas.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct UiRenderer;

impl UiRenderer {
    /// Draw the complete current scene.
    ///
    /// `battery_mv` is a filtered estimate; passing zero hides the fill level
    /// but keeps the battery outline visible during ADC startup.
    pub fn draw<D>(
        &self,
        target: &mut D,
        app: &App,
        now_ms: u32,
        battery_mv: u16,
    ) -> Result<(), D::Error>
    where
        D: DrawTarget<Color = BinaryColor>,
    {
        target.clear(OFF)?;
        match app.mode() {
            AppMode::Boot => draw_boot(target, app.mode_elapsed_ms(now_ms), battery_mv),
            AppMode::Menu => draw_menu(target, app, battery_mv),
            AppMode::Snake => draw_snake(target, app),
            AppMode::Stopwatch => draw_stopwatch(target, app, now_ms),
            AppMode::Countdown => draw_countdown(target, app, now_ms),
            AppMode::Standby => draw_standby(target, now_ms, battery_mv),
        }
    }
}

fn draw_boot<D>(target: &mut D, elapsed_ms: u32, battery_mv: u16) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    let progress = elapsed_ms.min(App::BOOT_DURATION_MS);
    let reveal = progress.min(620);
    let line_half = (reveal * 52 / 620) as i32;
    Line::new(
        Point::new(64 - line_half, 31),
        Point::new(64 + line_half, 31),
    )
    .into_styled(PrimitiveStyle::with_stroke(ON, 1))
    .draw(target)?;

    if progress > 80 {
        let radius = (progress - 80).min(420) * 12 / 420;
        if radius > 1 {
            Circle::new(
                Point::new(64 - radius as i32, 31 - radius as i32),
                radius * 2,
            )
            .into_styled(PrimitiveStyle::with_stroke(ON, 1))
            .draw(target)?;
        }
    }
    if progress > 260 {
        let rise = ((progress - 260).min(420) * 20 / 420) as i32;
        font::draw(target, "游戏机", Point::new(40, 50 - rise), ON)?;
    }
    if progress > 470 {
        draw_ascii(target, "GAMEBOX", Point::new(43, 12), &FONT_6X10)?;
    }

    let bar_width = progress * 118 / App::BOOT_DURATION_MS;
    Rectangle::new(Point::new(5, 61), Size::new(bar_width, 2))
        .into_styled(PrimitiveStyle::with_fill(ON))
        .draw(target)?;
    draw_battery(target, battery_mv)?;
    Ok(())
}

fn draw_menu<D>(target: &mut D, app: &App, battery_mv: u16) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    let menu = app.menu();
    let page = menu.current_page();
    let selected = menu.selected_index();
    let motion = app.menu_motion();
    let page_x = i32::from(motion.page_x.value());
    let cursor_y = i32::from(motion.cursor_y.value());
    let cursor_width = i32::from(motion.cursor_width.value()).clamp(12, 121);
    let scroll_y = i32::from(motion.scroll_y.value());
    let settings = app.persistent_data().settings;

    font::draw_small(target, page.title, Point::new(3, 2), ON)?;
    draw_battery(target, battery_mv)?;
    Line::new(Point::new(0, 11), Point::new(127, 11))
        .into_styled(PrimitiveStyle::with_stroke(ON, 1))
        .draw(target)?;

    let cursor_rect = Rectangle::new(
        Point::new(3 + page_x, cursor_y),
        Size::new(cursor_width as u32, 15),
    );
    match settings.cursor_style {
        CursorStyle::Inverse => RoundedRectangle::with_equal_corners(cursor_rect, Size::new(5, 5))
            .into_styled(PrimitiveStyle::with_fill(ON))
            .draw(target)?,
        CursorStyle::Frame => RoundedRectangle::with_equal_corners(cursor_rect, Size::new(5, 5))
            .into_styled(PrimitiveStyle::with_stroke(ON, 1))
            .draw(target)?,
        CursorStyle::Heart => draw_heart(target, Point::new(5 + page_x, cursor_y + 3))?,
    }

    for (index, entry) in page.entries.iter().enumerate() {
        let y = 14 + index as i32 * 16 + scroll_y;
        if !(-15..64).contains(&y) {
            continue;
        }
        let x = if settings.cursor_style == CursorStyle::Heart && index == selected {
            20 + page_x
        } else {
            8 + page_x
        };
        if index == selected && settings.cursor_style == CursorStyle::Inverse {
            font::draw_split(
                target,
                entry.label,
                Point::new(x, y),
                3 + page_x + cursor_width,
                OFF,
                ON,
            )?;
        } else {
            font::draw(target, entry.label, Point::new(x, y), ON)?;
        }

        if menu.page_id() == PageId::Settings
            && let Some(setting) = setting_for_index(index)
        {
            draw_setting_value(target, setting, app, y, page_x)?;
        }
    }

    draw_scrollbar(target, selected, page.entries.len())?;
    Ok(())
}

const fn setting_for_index(index: usize) -> Option<SettingId> {
    match index {
        1 => Some(SettingId::Sound),
        2 => Some(SettingId::Animation),
        3 => Some(SettingId::Cursor),
        4 => Some(SettingId::StandbyRefresh),
        _ => None,
    }
}

fn draw_setting_value<D>(
    target: &mut D,
    setting: SettingId,
    app: &App,
    y: i32,
    page_x: i32,
) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    let settings = app.persistent_data().settings;
    match setting {
        SettingId::Sound => {
            let value = if settings.sound_enabled { "开" } else { "关" };
            font::draw(target, value, Point::new(104 + page_x, y), ON)
        }
        SettingId::Animation => {
            let value = match settings.animation_speed {
                crate::AnimationSpeed::Off => "关",
                crate::AnimationSpeed::Slow => "慢",
                crate::AnimationSpeed::Fast => "快",
            };
            font::draw(target, value, Point::new(104 + page_x, y), ON)
        }
        SettingId::Cursor => {
            let value = match settings.cursor_style {
                CursorStyle::Inverse => "反相",
                CursorStyle::Frame => "矩形",
                CursorStyle::Heart => "爱心",
            };
            font::draw(target, value, Point::new(88 + page_x, y), ON)
        }
        SettingId::StandbyRefresh => {
            let mut buffer = NumberBuffer::new();
            let value = buffer.format(u32::from(settings.standby_refresh_seconds), 1);
            draw_ascii(target, value, Point::new(104 + page_x, y + 11), &FONT_6X10)
        }
    }
}

fn draw_scrollbar<D>(target: &mut D, selected: usize, count: usize) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    if count <= 1 {
        return Ok(());
    }
    Line::new(Point::new(125, 15), Point::new(125, 60))
        .into_styled(PrimitiveStyle::with_stroke(ON, 1))
        .draw(target)?;
    let y = 15 + (selected * 42 / (count - 1)) as i32;
    Rectangle::new(Point::new(123, y), Size::new(5, 4))
        .into_styled(PrimitiveStyle::with_fill(ON))
        .draw(target)?;
    Ok(())
}

fn draw_snake<D>(target: &mut D, app: &App) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    draw_ascii(target, "S", Point::new(1, 9), &FONT_6X10)?;
    let mut score_buffer = NumberBuffer::new();
    draw_ascii(
        target,
        score_buffer.format(u32::from(app.snake().score()), 2),
        Point::new(10, 9),
        &FONT_6X10,
    )?;
    draw_ascii(target, "HI", Point::new(79, 9), &FONT_6X10)?;
    let mut high_buffer = NumberBuffer::new();
    draw_ascii(
        target,
        high_buffer.format(u32::from(app.persistent_data().snake_high_score), 2),
        Point::new(96, 9),
        &FONT_6X10,
    )?;
    Line::new(Point::new(0, 10), Point::new(127, 10))
        .into_styled(PrimitiveStyle::with_stroke(ON, 1))
        .draw(target)?;

    for (index, cell) in app.snake().body().iter().enumerate() {
        let top_left = snake_point(*cell);
        let size = if index == 0 {
            Size::new(4, 4)
        } else {
            Size::new(3, 3)
        };
        Rectangle::new(top_left, size)
            .into_styled(PrimitiveStyle::with_fill(ON))
            .draw(target)?;
    }
    let food = snake_point(app.snake().food());
    Line::new(food + Point::new(0, 2), food + Point::new(4, 2))
        .into_styled(PrimitiveStyle::with_stroke(ON, 1))
        .draw(target)?;
    Line::new(food + Point::new(2, 0), food + Point::new(2, 4))
        .into_styled(PrimitiveStyle::with_stroke(ON, 1))
        .draw(target)?;

    if app.snake().is_game_over() {
        RoundedRectangle::with_equal_corners(
            Rectangle::new(Point::new(23, 18), Size::new(82, 20)),
            Size::new(6, 6),
        )
        .into_styled(PrimitiveStyle::with_fill(ON))
        .draw(target)?;
        font::draw(target, "游戏结束", Point::new(32, 20), OFF)?;
        font::draw(target, "请按键", Point::new(40, 44), ON)?;
    } else if app.snake_is_paused() {
        draw_ascii(target, "PAUSE", Point::new(49, 37), &FONT_6X10)?;
    }
    Ok(())
}

const fn snake_point(cell: Cell) -> Point {
    Point::new(cell.x as i32 * 4, 12 + cell.y as i32 * 4)
}

fn draw_stopwatch<D>(target: &mut D, app: &App, now_ms: u32) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    font::draw(target, "秒表", Point::new(48, 0), ON)?;
    Line::new(Point::new(0, 17), Point::new(127, 17))
        .into_styled(PrimitiveStyle::with_stroke(ON, 1))
        .draw(target)?;
    let bytes = format_stopwatch(app.stopwatch_elapsed_ms(now_ms));
    let value = str::from_utf8(&bytes).expect("time formatter only emits ASCII");
    draw_ascii(target, value, Point::new(24, 42), &FONT_10X20)?;
    let state = if app.stopwatch_is_running() {
        "RUN"
    } else {
        "STOP"
    };
    draw_ascii(target, state, Point::new(53, 57), &FONT_6X10)?;
    Ok(())
}

fn draw_countdown<D>(target: &mut D, app: &App, now_ms: u32) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    font::draw(target, "倒计时", Point::new(40, 0), ON)?;
    Line::new(Point::new(0, 17), Point::new(127, 17))
        .into_styled(PrimitiveStyle::with_stroke(ON, 1))
        .draw(target)?;
    let bytes = format_countdown(app.countdown_remaining_ms(now_ms));
    let value = str::from_utf8(&bytes).expect("time formatter only emits ASCII");
    draw_ascii(target, value, Point::new(39, 42), &FONT_10X20)?;
    let state = if app.countdown_is_completed() {
        "DONE"
    } else if app.countdown_is_running() {
        "RUN"
    } else {
        "SET"
    };
    draw_ascii(target, state, Point::new(53, 57), &FONT_6X10)?;
    Ok(())
}

fn draw_standby<D>(target: &mut D, now_ms: u32, battery_mv: u16) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    let total_seconds = now_ms / 1_000;
    let hours = (total_seconds / 3_600) % 100;
    let minutes = (total_seconds / 60) % 60;
    let bytes = [
        digit(hours / 10),
        digit(hours % 10),
        b':',
        digit(minutes / 10),
        digit(minutes % 10),
    ];
    let value = str::from_utf8(&bytes).expect("standby formatter only emits ASCII");
    draw_ascii(target, value, Point::new(39, 27), &FONT_10X20)?;
    Line::new(Point::new(15, 34), Point::new(112, 34))
        .into_styled(PrimitiveStyle::with_stroke(ON, 1))
        .draw(target)?;
    font::draw(target, "按键返回", Point::new(32, 42), ON)?;
    draw_battery(target, battery_mv)?;
    Ok(())
}

fn draw_battery<D>(target: &mut D, battery_mv: u16) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    // The legacy PCB exposes PA1 but does not document a battery divider.
    // Zero therefore means "sensor unavailable", not an empty battery.
    if battery_mv == 0 {
        return Ok(());
    }
    Rectangle::new(Point::new(109, 2), Size::new(15, 7))
        .into_styled(PrimitiveStyle::with_stroke(ON, 1))
        .draw(target)?;
    Rectangle::new(Point::new(124, 4), Size::new(2, 3))
        .into_styled(PrimitiveStyle::with_fill(ON))
        .draw(target)?;
    let width = u32::from(battery_mv.saturating_sub(3_200).min(1_000)) * 11 / 1_000;
    if width > 0 {
        Rectangle::new(Point::new(111, 4), Size::new(width, 3))
            .into_styled(PrimitiveStyle::with_fill(ON))
            .draw(target)?;
    }
    Ok(())
}

fn draw_heart<D>(target: &mut D, origin: Point) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    const POINTS: [(i32, i32); 20] = [
        (1, 0),
        (2, 0),
        (5, 0),
        (6, 0),
        (0, 1),
        (1, 1),
        (2, 1),
        (3, 1),
        (4, 1),
        (5, 1),
        (6, 1),
        (7, 1),
        (1, 2),
        (2, 2),
        (3, 2),
        (4, 2),
        (5, 2),
        (6, 2),
        (3, 3),
        (4, 3),
    ];
    target.draw_iter(
        POINTS
            .iter()
            .map(|(x, y)| Pixel(origin + Point::new(*x, *y), ON)),
    )
}

fn draw_ascii<D>(
    target: &mut D,
    text: &str,
    baseline: Point,
    font: &'static embedded_graphics::mono_font::MonoFont<'static>,
) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    Text::new(text, baseline, MonoTextStyle::new(font, ON))
        .draw(target)
        .map(|_| ())
}

const fn digit(value: u32) -> u8 {
    b'0' + value as u8
}

fn format_stopwatch(elapsed_ms: u32) -> [u8; 7] {
    let tenths = (elapsed_ms / 100) % 10;
    let total_seconds = elapsed_ms / 1_000;
    let minutes = (total_seconds / 60) % 100;
    let seconds = total_seconds % 60;
    [
        digit(minutes / 10),
        digit(minutes % 10),
        b':',
        digit(seconds / 10),
        digit(seconds % 10),
        b'.',
        digit(tenths),
    ]
}

fn format_countdown(remaining_ms: u32) -> [u8; 5] {
    let total_seconds = remaining_ms.saturating_add(999) / 1_000;
    let minutes = (total_seconds / 60).min(99);
    let seconds = total_seconds % 60;
    [
        digit(minutes / 10),
        digit(minutes % 10),
        b':',
        digit(seconds / 10),
        digit(seconds % 10),
    ]
}

struct NumberBuffer {
    bytes: [u8; 10],
}

impl NumberBuffer {
    const fn new() -> Self {
        Self { bytes: [b'0'; 10] }
    }

    fn format(&mut self, mut value: u32, minimum_digits: usize) -> &str {
        let mut cursor = self.bytes.len();
        let mut digits = 0;
        loop {
            cursor -= 1;
            self.bytes[cursor] = digit(value % 10);
            value /= 10;
            digits += 1;
            if value == 0 && digits >= minimum_digits {
                break;
            }
        }
        str::from_utf8(&self.bytes[cursor..]).expect("number formatter only emits ASCII")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn compact_time_formatters_are_stable() {
        assert_eq!(&format_stopwatch(123_456), b"02:03.4");
        assert_eq!(&format_countdown(60_001), b"01:01");
    }

    #[test]
    fn number_buffer_pads_without_allocation() {
        let mut buffer = NumberBuffer::new();
        assert_eq!(buffer.format(7, 3), "007");
    }
}
