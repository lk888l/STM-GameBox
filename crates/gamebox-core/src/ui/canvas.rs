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
        for column in start..end {
            self.pixel(column, y, operation);
        }
    }

    pub(crate) fn vline(&mut self, x: i16, y: i16, height: i16, operation: PixelOp) {
        if height <= 0 || !(0..Self::WIDTH).contains(&x) {
            return;
        }
        let start = y.max(0);
        let end = y.saturating_add(height).min(Self::HEIGHT);
        for row in start..end {
            self.pixel(x, row, operation);
        }
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
        if height <= 0 {
            return;
        }
        for row in 0..height {
            self.hline(x, y + row, width, operation);
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
        for &character in text {
            let glyph = glyph(character);
            for (column, bits) in glyph.iter().enumerate() {
                for row in 0..8 {
                    let foreground = bits & (1 << row) != 0;
                    let operation = if foreground != inverted {
                        PixelOp::Set
                    } else {
                        PixelOp::Clear
                    };
                    self.pixel(x + column as i16, y + row, operation);
                }
            }
            x += 6;
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
