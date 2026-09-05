//! Clipped 1-bit drawing primitives in native SSD1306 page layout.

use super::font::glyph;

/// Native 128×64 framebuffer byte count.
pub const FRAME_BYTES: usize = 1_024;

#[derive(Clone, Copy)]
pub(crate) enum PixelOp {
    Clear,
    Set,
    Invert,
}

pub(crate) struct Canvas<'a> {
    pixels: &'a mut [u8; FRAME_BYTES],
}

impl<'a> Canvas<'a> {
    pub(crate) const WIDTH: i16 = 128;
    pub(crate) const HEIGHT: i16 = 64;

    pub(crate) fn new(pixels: &'a mut [u8; FRAME_BYTES]) -> Self {
        Self { pixels }
    }

    pub(crate) fn clear(&mut self) {
        self.pixels.fill(0);
    }

    pub(crate) fn pixel(&mut self, x: i16, y: i16, operation: PixelOp) {
        if !(0..Self::WIDTH).contains(&x) || !(0..Self::HEIGHT).contains(&y) {
            return;
        }
        let index = usize::from(y as u16 / 8) * 128 + usize::from(x as u16);
        let mask = 1_u8 << (y & 7);
        match operation {
            PixelOp::Clear => self.pixels[index] &= !mask,
            PixelOp::Set => self.pixels[index] |= mask,
            PixelOp::Invert => self.pixels[index] ^= mask,
        }
    }

    pub(crate) fn hline(&mut self, x: i16, y: i16, width: i16, operation: PixelOp) {
        if width <= 0 || !(0..Self::HEIGHT).contains(&y) {
            return;
        }
        let start = x.max(0);
        let end = x.saturating_add(width).min(Self::WIDTH);
        if start < end {
            let page = (y as usize / 8) * Self::WIDTH as usize;
            Self::apply_mask(
                &mut self.pixels[page + start as usize..page + end as usize],
                1 << (y & 7),
                operation,
            );
        }
    }

    pub(crate) fn vline(&mut self, x: i16, y: i16, height: i16, operation: PixelOp) {
        self.fill_rectangle(x, y, 1, height, operation);
    }

    pub(crate) fn line(&mut self, mut x0: i16, mut y0: i16, x1: i16, y1: i16, operation: PixelOp) {
        let dx = (x1 - x0).abs();
        let sx = if x0 < x1 { 1 } else { -1 };
        let dy = -(y1 - y0).abs();
        let sy = if y0 < y1 { 1 } else { -1 };
        let mut error = dx + dy;
        loop {
            self.pixel(x0, y0, operation);
            if x0 == x1 && y0 == y1 {
                break;
            }
            let twice = error.saturating_mul(2);
            if twice >= dy {
                error += dy;
                x0 += sx;
            }
            if twice <= dx {
                error += dx;
                y0 += sy;
            }
        }
    }

    pub(crate) fn rectangle(
        &mut self,
        x: i16,
        y: i16,
        width: i16,
        height: i16,
        operation: PixelOp,
    ) {
        if width <= 0 || height <= 0 {
            return;
        }
        self.hline(x, y, width, operation);
        self.hline(x, y + height - 1, width, operation);
        self.vline(x, y, height, operation);
        self.vline(x + width - 1, y, height, operation);
    }

    pub(crate) fn fill_rectangle(
        &mut self,
        x: i16,
        y: i16,
        width: i16,
        height: i16,
        operation: PixelOp,
    ) {
        if width <= 0 || height <= 0 {
            return;
        }
        let left = x.max(0);
        let right = x.saturating_add(width).min(Self::WIDTH);
        let top = y.max(0);
        let bottom = y.saturating_add(height).min(Self::HEIGHT);
        if left >= right || top >= bottom {
            return;
        }
        // One masked byte updates up to eight vertical pixels. Clip once,
        // rather than doing bounds checks and address arithmetic per pixel.
        for page in top / 8..=(bottom - 1) / 8 {
            let first_bit = (top - page * 8).max(0) as u32;
            let end_bit = (bottom - page * 8).min(8) as u32;
            let mask = (u8::MAX << first_bit) & (u8::MAX >> (8 - end_bit));
            let base = page as usize * Self::WIDTH as usize;
            Self::apply_mask(
                &mut self.pixels[base + left as usize..base + right as usize],
                mask,
                operation,
            );
        }
    }

    fn apply_mask(bytes: &mut [u8], mask: u8, operation: PixelOp) {
        match operation {
            PixelOp::Clear => bytes.iter_mut().for_each(|byte| *byte &= !mask),
            PixelOp::Set => bytes.iter_mut().for_each(|byte| *byte |= mask),
            PixelOp::Invert => bytes.iter_mut().for_each(|byte| *byte ^= mask),
        }
    }

    pub(crate) fn rounded_rectangle(
        &mut self,
        x: i16,
        y: i16,
        width: i16,
        height: i16,
        radius: i16,
        operation: PixelOp,
    ) {
        if width <= 0 || height <= 0 {
            return;
        }
        let radius = radius.clamp(0, ((width.min(height) - 1) / 2).min(3));
        self.hline(x + radius, y, width - radius * 2, operation);
        self.hline(x + radius, y + height - 1, width - radius * 2, operation);
        self.vline(x, y + radius, height - radius * 2, operation);
        self.vline(x + width - 1, y + radius, height - radius * 2, operation);
        for offset in 0..radius {
            let inset = radius - offset;
            self.pixel(x + inset, y + offset, operation);
            self.pixel(x + width - 1 - inset, y + offset, operation);
            self.pixel(x + inset, y + height - 1 - offset, operation);
            self.pixel(x + width - 1 - inset, y + height - 1 - offset, operation);
        }
    }

    pub(crate) fn fill_rounded_rectangle(
        &mut self,
        x: i16,
        y: i16,
        width: i16,
        height: i16,
        radius: i16,
        operation: PixelOp,
    ) {
        if width <= 0 || height <= 0 {
            return;
        }
        let radius = radius.clamp(0, ((width.min(height) - 1) / 2).min(3));
        for row in 0..height {
            let edge = row.min(height - 1 - row);
            let inset = if edge < radius { radius - edge } else { 0 };
            self.hline(x + inset, y + row, width - inset * 2, operation);
        }
    }

    pub(crate) fn circle(&mut self, center_x: i16, center_y: i16, radius: i16, operation: PixelOp) {
        let mut x = radius;
        let mut y = 0;
        let mut error = 1 - radius;
        while x >= y {
            for (dx, dy) in [
                (x, y),
                (y, x),
                (-y, x),
                (-x, y),
                (-x, -y),
                (-y, -x),
                (y, -x),
                (x, -y),
            ] {
                self.pixel(center_x + dx, center_y + dy, operation);
            }
            y += 1;
            if error < 0 {
                error += 2 * y + 1;
            } else {
                x -= 1;
                error += 2 * (y - x) + 1;
            }
        }
    }

    pub(crate) fn text(&mut self, x: i16, y: i16, text: &str, inverted: bool) {
        self.text_bytes(x, y, text.as_bytes(), inverted);
    }

    pub(crate) fn text_bytes(&mut self, mut x: i16, y: i16, text: &[u8], inverted: bool) {
        if y <= -8 || y >= Self::HEIGHT {
            return;
        }
        let first_page = y.div_euclid(8);
        let shift = (y & 7) as u32;
        let mask = u16::from(u8::MAX) << shift;
        for &character in text {
            let glyph = glyph(character);
            for (column, bits) in glyph.iter().enumerate() {
                let column_x = x.saturating_add(column as i16);
                if !(0..Self::WIDTH).contains(&column_x) {
                    continue;
                }
                let bits = u16::from(if inverted { !bits } else { *bits }) << shift;
                // A 6x8 glyph column crosses at most two native OLED pages.
                // Preserve pixels outside the glyph, including clipped text.
                for offset in 0..2 {
                    let page = first_page + offset;
                    if (0..Self::HEIGHT / 8).contains(&page) {
                        let index = page as usize * Self::WIDTH as usize + column_x as usize;
                        let page_mask = (mask >> (offset * 8)) as u8;
                        let page_bits = (bits >> (offset * 8)) as u8;
                        self.pixels[index] = (self.pixels[index] & !page_mask) | page_bits;
                    }
                }
            }
            x = x.saturating_add(6);
        }
    }

    pub(crate) fn text_scaled(
        &mut self,
        mut x: i16,
        y: i16,
        text: &str,
        scale: i16,
        inverted: bool,
    ) {
        if scale <= 0 {
            return;
        }
        for character in text.bytes() {
            let glyph = glyph(character);
            for (column, bits) in glyph.iter().enumerate() {
                for row in 0..8 {
                    let foreground = bits & (1 << row) != 0;
                    let operation = if foreground != inverted {
                        PixelOp::Set
                    } else {
                        PixelOp::Clear
                    };
                    self.fill_rectangle(
                        x + column as i16 * scale,
                        y + row * scale,
                        scale,
                        scale,
                        operation,
                    );
                }
            }
            x += 6 * scale;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn patterned_frame() -> [u8; FRAME_BYTES] {
        core::array::from_fn(|index| (index.wrapping_mul(37) ^ (index / 8)) as u8)
    }

    fn reference_rectangle(
        frame: &mut [u8; FRAME_BYTES],
        x: i16,
        y: i16,
        width: i16,
        height: i16,
        operation: PixelOp,
    ) {
        let mut canvas = Canvas::new(frame);
        for screen_y in 0..Canvas::HEIGHT {
            for screen_x in 0..Canvas::WIDTH {
                if screen_x >= x
                    && i32::from(screen_x) < i32::from(x) + i32::from(width)
                    && screen_y >= y
                    && i32::from(screen_y) < i32::from(y) + i32::from(height)
                {
                    canvas.pixel(screen_x, screen_y, operation);
                }
            }
        }
    }

    #[test]
    fn page_spans_match_pixel_reference_with_clipping_and_all_operations() {
        let mut seed = 17_u32;
        for _ in 0..300 {
            let mut random = |range: u32, offset: i16| {
                seed = seed.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
                ((seed >> 8) % range) as i16 - offset
            };
            let x = random(170, 20);
            let y = random(100, 20);
            let width = random(180, 5);
            let height = random(100, 5);
            for operation in [PixelOp::Clear, PixelOp::Set, PixelOp::Invert] {
                for (span_width, span_height) in [(width, height), (width, 1), (1, height)] {
                    let mut expected = patterned_frame();
                    let mut actual = expected;
                    reference_rectangle(&mut expected, x, y, span_width, span_height, operation);
                    let mut canvas = Canvas::new(&mut actual);
                    if span_height == 1 {
                        canvas.hline(x, y, span_width, operation);
                    } else if span_width == 1 {
                        canvas.vline(x, y, span_height, operation);
                    } else {
                        canvas.fill_rectangle(x, y, span_width, span_height, operation);
                    }
                    assert_eq!(
                        actual, expected,
                        "rectangle {x},{y} {span_width}x{span_height}"
                    );
                }
            }
        }
    }

    #[test]
    fn glyph_page_writes_match_pixel_reference_at_every_vertical_alignment() {
        for character in 0..=u8::MAX {
            for x in [-6, -1, 0, 120, 125, 128] {
                for y in [-8, -7, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 57, 63, 64] {
                    for inverted in [false, true] {
                        let mut expected = patterned_frame();
                        let mut actual = expected;
                        Canvas::new(&mut actual).text_bytes(x, y, &[character], inverted);
                        let mut reference = Canvas::new(&mut expected);
                        for (column, bits) in glyph(character).iter().enumerate() {
                            for row in 0..8 {
                                let operation = if (bits & (1 << row) != 0) != inverted {
                                    PixelOp::Set
                                } else {
                                    PixelOp::Clear
                                };
                                reference.pixel(x + column as i16, y + row, operation);
                            }
                        }
                        assert_eq!(
                            actual, expected,
                            "glyph {character} at {x},{y}, inverted={inverted}"
                        );
                    }
                }
            }
        }
    }
}
