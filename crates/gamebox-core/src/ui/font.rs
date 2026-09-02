//! Compact 16×16 Chinese glyph renderer.

use embedded_graphics::{
    Pixel,
    pixelcolor::BinaryColor,
    prelude::{DrawTarget, Point},
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct Glyph {
    pub(super) character: char,
    pub(super) bitmap: [u8; 32],
}

include!("generated.rs");

/// Width in pixels for the fixed Chinese font.
pub const HANZI_WIDTH: i32 = 16;

/// Return whether every non-ASCII character is in the packed subset.
#[must_use]
#[cfg(test)]
pub fn contains_all(text: &str) -> bool {
    text.chars()
        .filter(|character| !character.is_ascii())
        .all(|character| lookup(character).is_some())
}

/// Draw a Chinese string with one foreground color.
pub fn draw<D>(
    target: &mut D,
    text: &str,
    origin: Point,
    color: BinaryColor,
) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    draw_split(target, text, origin, i32::MIN, color, color)
}

/// Draw the same packed glyphs at 8×8 for compact Chinese page headers.
/// Each destination pixel is the logical OR of one 2×2 source block, so this
/// costs no second font table and preserves thin strokes when downsampling.
pub fn draw_small<D>(
    target: &mut D,
    text: &str,
    origin: Point,
    color: BinaryColor,
) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    let mut cursor_x = origin.x;
    for character in text.chars() {
        let bitmap = lookup(character)
            .map(|glyph| glyph.bitmap)
            .unwrap_or_else(fallback_bitmap);
        let pixels = (0..8_i32).flat_map(|x| {
            (0..8_i32).filter_map(move |y| {
                let source_x = x * 2;
                let source_y = y * 2;
                let is_set = (0..2_i32).any(|dx| {
                    (0..2_i32).any(|dy| {
                        let sample_x = source_x + dx;
                        let sample_y = source_y + dy;
                        let byte = bitmap[(sample_y as usize / 8) * 16 + sample_x as usize];
                        byte & (1 << (sample_y & 7)) != 0
                    })
                });
                is_set.then_some(Pixel(Point::new(cursor_x + x, origin.y + y), color))
            })
        });
        target.draw_iter(pixels)?;
        cursor_x += 8;
    }
    Ok(())
}

/// Draw glyph pixels with separate colors to the left and right of `split_x`.
///
/// This makes text stay legible while an inverted menu capsule changes width.
pub fn draw_split<D>(
    target: &mut D,
    text: &str,
    origin: Point,
    split_x: i32,
    left_color: BinaryColor,
    right_color: BinaryColor,
) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    let mut cursor_x = origin.x;
    for character in text.chars() {
        let bitmap = lookup(character)
            .map(|glyph| glyph.bitmap)
            .unwrap_or_else(fallback_bitmap);
        let pixels = (0..16_i32).flat_map(|x| {
            (0..16_i32).filter_map(move |y| {
                let byte = bitmap[(y as usize / 8) * 16 + x as usize];
                if byte & (1 << (y & 7)) == 0 {
                    None
                } else {
                    let point = Point::new(cursor_x + x, origin.y + y);
                    let color = if point.x < split_x {
                        left_color
                    } else {
                        right_color
                    };
                    Some(Pixel(point, color))
                }
            })
        });
        target.draw_iter(pixels)?;
        cursor_x += HANZI_WIDTH;
    }
    Ok(())
}

fn lookup(character: char) -> Option<&'static Glyph> {
    GLYPHS
        .binary_search_by_key(&character, |glyph| glyph.character)
        .ok()
        .map(|index| &GLYPHS[index])
}

const fn fallback_bitmap() -> [u8; 32] {
    [
        0xff, 0x01, 0x01, 0x01, 0x31, 0x09, 0x09, 0x09, 0x09, 0x89, 0x71, 0x01, 0x01, 0x01, 0x01,
        0xff, 0xff, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x96, 0x81, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0xff,
    ]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn all_product_copy_is_available() {
        let copy = [
            "游戏机",
            "游戏",
            "工具",
            "设置",
            "关机",
            "返回",
            "贪吃蛇",
            "秒表",
            "倒计时",
            "静音",
            "动画速度",
            "光标风格",
            "刷新时间",
            "开关快慢反相矩形爱心",
            "得分最高游戏结束请按键退出",
        ];
        for text in copy {
            assert!(contains_all(text), "missing glyph in {text}");
        }
    }
}
