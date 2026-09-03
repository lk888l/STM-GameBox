//! Native SSD1306 framebuffer storage and drawing surfaces.

use crate::{ConfigError, DisplaySize};

#[cfg(feature = "graphics")]
use core::convert::Infallible;
#[cfg(feature = "graphics")]
use embedded_graphics_core::{
    Pixel,
    draw_target::DrawTarget,
    geometry::{Dimensions, OriginDimensions, Point, Size},
    pixelcolor::BinaryColor,
    primitives::Rectangle,
};

/// Width of the first supported display.
pub const DISPLAY_WIDTH: usize = 128;
/// Height of the first supported display.
pub const DISPLAY_HEIGHT: usize = 64;
/// Number of eight-pixel pages in the display.
pub const DISPLAY_PAGES: usize = DISPLAY_HEIGHT / 8;
/// Number of bytes in a full native framebuffer.
pub const DISPLAY_BYTES: usize = DISPLAY_WIDTH * DISPLAY_PAGES;
/// Number of bytes required by the exact dirty bitmap.
pub const DIRTY_BYTES: usize = DISPLAY_BYTES / 8;

/// Policy used to turn exact dirty bytes into efficient I²C transfers.
///
/// This is an immutable, validated value object. Construct it with [`Self::new`]
/// so an invalid threshold cannot enter the display pipeline.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FlushPolicy {
    /// Maximum clean-byte gap merged between two dirty runs.
    merge_gap: u8,
    /// Dirty-byte threshold on a 128-column panel at which the complete
    /// physical page is sent.
    ///
    /// The threshold is scaled for narrower panels. Valid values are
    /// `1..=128`; `64` therefore means 50% on every supported panel.
    full_page_threshold: u8,
}

impl FlushPolicy {
    /// Creates and validates a flush policy.
    pub const fn new(merge_gap: u8, full_page_threshold: u8) -> Result<Self, ConfigError> {
        if full_page_threshold == 0 || full_page_threshold as usize > DISPLAY_WIDTH {
            return Err(ConfigError::InvalidFlushPolicy);
        }
        Ok(Self {
            merge_gap,
            full_page_threshold,
        })
    }

    /// Returns the maximum clean-byte gap merged between dirty runs.
    #[must_use]
    pub const fn merge_gap(self) -> u8 {
        self.merge_gap
    }

    /// Returns the 128-column full-page threshold.
    #[must_use]
    pub const fn full_page_threshold(self) -> u8 {
        self.full_page_threshold
    }
}

impl Default for FlushPolicy {
    fn default() -> Self {
        Self {
            merge_gap: 8,
            full_page_threshold: 64,
        }
    }
}

/// User-owned full framebuffer with an internal exact dirty bitmap.
///
/// The byte layout is native SSD1306 page layout:
/// `index = x + (y / 8) * 128`.
pub struct FullBuffer<'a> {
    data: &'a mut [u8],
    dirty: [u8; DIRTY_BYTES],
    size: DisplaySize,
}

impl<'a> FullBuffer<'a> {
    /// Wraps an exactly 1024-byte user buffer and clears it.
    pub fn new(data: &'a mut [u8]) -> Result<Self, ConfigError> {
        Self::new_for(DisplaySize::Display128x64, data)
    }

    /// Wraps a framebuffer matching `size` and clears it.
    pub fn new_for(size: DisplaySize, data: &'a mut [u8]) -> Result<Self, ConfigError> {
        let required = size.buffer_len();
        if data.len() != required {
            return Err(ConfigError::InvalidFullBufferLength {
                actual: data.len(),
                required,
            });
        }
        data.fill(0);
        Ok(Self {
            data,
            dirty: [0; DIRTY_BYTES],
            size,
        })
    }

    /// Returns the selected panel geometry.
    #[must_use]
    pub const fn size(&self) -> DisplaySize {
        self.size
    }

    pub(crate) const fn width(&self) -> usize {
        self.size.width()
    }

    pub(crate) const fn height(&self) -> usize {
        self.size.height()
    }

    pub(crate) const fn pages(&self) -> usize {
        self.size.pages()
    }

    /// Returns the native framebuffer bytes.
    #[must_use]
    pub fn as_bytes(&self) -> &[u8] {
        self.data
    }

    /// Releases the caller-owned framebuffer memory.
    #[must_use]
    pub fn release(self) -> &'a mut [u8] {
        self.data
    }

    /// Returns whether at least one byte needs synchronization.
    #[must_use]
    pub fn is_dirty(&self) -> bool {
        self.dirty.iter().any(|byte| *byte != 0)
    }

    /// Marks every framebuffer byte dirty.
    pub fn mark_all_dirty(&mut self) {
        for index in 0..self.data.len() {
            self.set_dirty_bit(index, true);
        }
    }

    pub(crate) fn clear_all_dirty(&mut self) {
        self.dirty.fill(0);
    }

    /// Writes one logical pixel. Out-of-bounds coordinates are clipped.
    pub fn set_pixel(&mut self, x: i32, y: i32, color: bool) {
        if x < 0 || y < 0 || x >= self.width() as i32 || y >= self.height() as i32 {
            return;
        }

        let x = x as usize;
        let y = y as usize;
        let index = x + (y / 8) * self.width();
        let mask = 1_u8 << (y & 7);
        let old = self.data[index];
        let new = if color { old | mask } else { old & !mask };
        self.update_byte(index, new);
    }

    /// Copies native page-layout bytes into the framebuffer.
    ///
    /// `bytes` must contain `width * pages` bytes, page by page.
    pub fn blit_native(
        &mut self,
        x: usize,
        page: usize,
        width: usize,
        pages: usize,
        bytes: &[u8],
    ) -> Result<(), ConfigError> {
        let expected = width
            .checked_mul(pages)
            .ok_or(ConfigError::InvalidBitmapLength {
                actual: bytes.len(),
                expected: usize::MAX,
            })?;
        if bytes.len() != expected {
            return Err(ConfigError::InvalidBitmapLength {
                actual: bytes.len(),
                expected,
            });
        }
        if x > self.width()
            || page > self.pages()
            || width > self.width() - x
            || pages > self.pages() - page
        {
            return Err(ConfigError::BitmapOutOfBounds);
        }

        for source_page in 0..pages {
            let destination = (page + source_page) * self.width() + x;
            let source = source_page * width;
            for offset in 0..width {
                self.update_byte(destination + offset, bytes[source + offset]);
            }
        }
        Ok(())
    }

    pub(crate) fn fill_all(&mut self, color: bool) {
        let value = if color { u8::MAX } else { 0 };
        for index in 0..self.data.len() {
            self.update_byte(index, value);
        }
    }

    #[cfg(feature = "graphics")]
    fn fill_rectangle(&mut self, area: &Rectangle, color: bool) {
        let Some((x0, y0, x1, y1)) = clipped_bounds(area, self.width(), self.height()) else {
            return;
        };
        let width = self.width();
        let first_page = y0 as usize / 8;
        let last_page = (y1 as usize - 1) / 8;
        for page in first_page..=last_page {
            let page_y = page * 8;
            let bit_start = (y0 as usize).saturating_sub(page_y);
            let bit_end = (y1 as usize).saturating_sub(page_y).min(8);
            let mask = bit_range_mask(bit_start, bit_end);
            for x in x0 as usize..x1 as usize {
                let index = page * width + x;
                let old = self.data[index];
                self.update_byte(index, if color { old | mask } else { old & !mask });
            }
        }
    }

    #[cfg(feature = "graphics")]
    fn fill_contiguous_pixels<I>(&mut self, area: &Rectangle, colors: I)
    where
        I: IntoIterator<Item = BinaryColor>,
    {
        let mut values = [0_u8; DISPLAY_WIDTH];
        let mut masks = [0_u8; DISPLAY_WIDTH];
        let mut active_page = None;
        let mut colors = colors.into_iter();

        for row in 0..area.size.height {
            let y = area.top_left.y.saturating_add_unsigned(row);
            let page = (0..self.height() as i32)
                .contains(&y)
                .then_some(y as usize / 8);
            if page != active_page {
                if let Some(previous) = active_page {
                    self.commit_contiguous_page(previous, &values, &masks);
                }
                values.fill(0);
                masks.fill(0);
                active_page = page;
            }

            for column in 0..area.size.width {
                let Some(color) = colors.next() else {
                    if let Some(previous) = active_page {
                        self.commit_contiguous_page(previous, &values, &masks);
                    }
                    return;
                };
                let x = area.top_left.x.saturating_add_unsigned(column);
                if let Some(page) = page
                    && (0..self.width() as i32).contains(&x)
                {
                    let x = x as usize;
                    let mask = 1_u8 << (y as usize & 7);
                    masks[x] |= mask;
                    if color == BinaryColor::On {
                        values[x] |= mask;
                    }
                    active_page = Some(page);
                }
            }
        }
        if let Some(previous) = active_page {
            self.commit_contiguous_page(previous, &values, &masks);
        }
    }

    #[cfg(feature = "graphics")]
    fn commit_contiguous_page(
        &mut self,
        page: usize,
        values: &[u8; DISPLAY_WIDTH],
        masks: &[u8; DISPLAY_WIDTH],
    ) {
        for x in 0..self.width() {
            if masks[x] != 0 {
                let index = page * self.width() + x;
                let new = (self.data[index] & !masks[x]) | values[x];
                self.update_byte(index, new);
            }
        }
    }

    pub(crate) fn page_slice(&self, page: usize) -> &[u8] {
        let start = page * self.width();
        &self.data[start..start + self.width()]
    }

    pub(crate) fn dirty_count(&self, page: usize) -> usize {
        let start = page * self.width();
        (start..start + self.width())
            .filter(|index| self.dirty_bit(*index))
            .count()
    }

    pub(crate) fn next_dirty_run(
        &self,
        page: usize,
        cursor: usize,
        policy: FlushPolicy,
    ) -> Option<(usize, usize)> {
        let width = self.width();
        if cursor >= width {
            return None;
        }
        let threshold = (width * usize::from(policy.full_page_threshold())).div_ceil(DISPLAY_WIDTH);
        if cursor == 0 && self.dirty_count(page) >= threshold {
            return Some((0, width - 1));
        }

        let base = page * width;
        let start = (cursor..width).find(|x| self.dirty_bit(base + *x))?;
        let mut end = start;
        let mut search = start + 1;
        while let Some(next) = (search..width).find(|x| self.dirty_bit(base + *x)) {
            let clean_gap = next - end - 1;
            if clean_gap > usize::from(policy.merge_gap()) {
                break;
            }
            end = next;
            search = next + 1;
        }
        Some((start, end))
    }

    pub(crate) fn clear_dirty_range(&mut self, page: usize, start: usize, end: usize) {
        let base = page * self.width();
        for x in start..=end {
            self.set_dirty_bit(base + x, false);
        }
    }

    fn update_byte(&mut self, index: usize, value: u8) {
        if self.data[index] != value {
            self.data[index] = value;
            self.set_dirty_bit(index, true);
        }
    }

    fn dirty_bit(&self, index: usize) -> bool {
        self.dirty[index / 8] & (1 << (index & 7)) != 0
    }

    fn set_dirty_bit(&mut self, index: usize, dirty: bool) {
        let mask = 1 << (index & 7);
        if dirty {
            self.dirty[index / 8] |= mask;
        } else {
            self.dirty[index / 8] &= !mask;
        }
    }
}

/// User-owned page buffer for low-RAM repeated rendering.
pub struct PageBuffer<'a> {
    data: &'a mut [u8],
    size: DisplaySize,
}

impl<'a> PageBuffer<'a> {
    /// Wraps a page-aligned buffer of at most 1024 bytes and clears it.
    pub fn new(data: &'a mut [u8]) -> Result<Self, ConfigError> {
        Self::new_for(DisplaySize::Display128x64, data)
    }

    /// Wraps a page-aligned buffer suitable for `size` and clears it.
    pub fn new_for(size: DisplaySize, data: &'a mut [u8]) -> Result<Self, ConfigError> {
        if data.is_empty()
            || data.len() > size.buffer_len()
            || !data.len().is_multiple_of(size.width())
        {
            return Err(ConfigError::InvalidPageBufferLength { actual: data.len() });
        }
        data.fill(0);
        Ok(Self { data, size })
    }

    /// Returns the selected panel geometry.
    #[must_use]
    pub const fn size(&self) -> DisplaySize {
        self.size
    }

    pub(crate) const fn width(&self) -> usize {
        self.size.width()
    }

    pub(crate) const fn pages(&self) -> usize {
        self.size.pages()
    }

    /// Returns the number of physical pages rendered per pass.
    #[must_use]
    pub fn capacity_pages(&self) -> usize {
        self.data.len() / self.width()
    }

    /// Releases the caller-owned page-buffer memory.
    #[must_use]
    pub fn release(self) -> &'a mut [u8] {
        self.data
    }

    pub(crate) fn clear(&mut self) {
        self.data.fill(0);
    }

    pub(crate) fn as_bytes(&self, page_count: usize) -> &[u8] {
        &self.data[..page_count * self.width()]
    }

    pub(crate) fn canvas(&mut self, start_page: usize, page_count: usize) -> PageCanvas<'_> {
        let width = self.width();
        PageCanvas {
            data: &mut self.data[..page_count * width],
            start_page,
            page_count,
            size: self.size,
        }
    }
}

/// A clipped drawing surface for one page-rendering pass.
///
/// Coordinates are always global display coordinates. Pixels outside the
/// active page range are silently clipped.
pub struct PageCanvas<'a> {
    data: &'a mut [u8],
    start_page: usize,
    page_count: usize,
    size: DisplaySize,
}

impl PageCanvas<'_> {
    /// First physical page currently being rendered.
    #[must_use]
    pub fn start_page(&self) -> usize {
        self.start_page
    }

    /// Number of physical pages currently being rendered.
    #[must_use]
    pub fn page_count(&self) -> usize {
        self.page_count
    }

    /// Writes a logical pixel and clips it to the active page range.
    pub fn set_pixel(&mut self, x: i32, y: i32, color: bool) {
        let first_y = self.start_page * 8;
        let last_y = first_y + self.page_count * 8;
        if x < 0
            || x >= self.size.width() as i32
            || y < first_y as i32
            || y >= last_y as i32
            || y >= self.size.height() as i32
        {
            return;
        }
        let x = x as usize;
        let y = y as usize;
        let local_page = y / 8 - self.start_page;
        let index = local_page * self.size.width() + x;
        let mask = 1_u8 << (y & 7);
        if color {
            self.data[index] |= mask;
        } else {
            self.data[index] &= !mask;
        }
    }

    #[cfg(feature = "graphics")]
    fn fill_rectangle(&mut self, area: &Rectangle, color: bool) {
        let Some((x0, mut y0, x1, mut y1)) =
            clipped_bounds(area, self.size.width(), self.size.height())
        else {
            return;
        };
        let active_y0 = (self.start_page * 8) as i32;
        let active_y1 = ((self.start_page + self.page_count) * 8) as i32;
        y0 = y0.max(active_y0);
        y1 = y1.min(active_y1);
        if y0 >= y1 {
            return;
        }

        let first_page = y0 as usize / 8;
        let last_page = (y1 as usize - 1) / 8;
        for page in first_page..=last_page {
            let page_y = page * 8;
            let bit_start = (y0 as usize).saturating_sub(page_y);
            let bit_end = (y1 as usize).saturating_sub(page_y).min(8);
            let mask = bit_range_mask(bit_start, bit_end);
            let local_page = page - self.start_page;
            for x in x0 as usize..x1 as usize {
                let byte = &mut self.data[local_page * self.size.width() + x];
                *byte = if color { *byte | mask } else { *byte & !mask };
            }
        }
    }

    #[cfg(feature = "graphics")]
    fn fill_contiguous_pixels<I>(&mut self, area: &Rectangle, colors: I)
    where
        I: IntoIterator<Item = BinaryColor>,
    {
        let mut values = [0_u8; DISPLAY_WIDTH];
        let mut masks = [0_u8; DISPLAY_WIDTH];
        let mut active_page = None;
        let mut colors = colors.into_iter();
        let first_page = self.start_page;
        let page_limit = first_page + self.page_count;

        for row in 0..area.size.height {
            let y = area.top_left.y.saturating_add_unsigned(row);
            let page = (0..self.size.height() as i32)
                .contains(&y)
                .then_some(y as usize / 8)
                .filter(|page| (first_page..page_limit).contains(page));
            if page != active_page {
                if let Some(previous) = active_page {
                    self.commit_contiguous_page(previous, &values, &masks);
                }
                values.fill(0);
                masks.fill(0);
                active_page = page;
            }

            for column in 0..area.size.width {
                let Some(color) = colors.next() else {
                    if let Some(previous) = active_page {
                        self.commit_contiguous_page(previous, &values, &masks);
                    }
                    return;
                };
                let x = area.top_left.x.saturating_add_unsigned(column);
                if let Some(page) = page
                    && (0..self.size.width() as i32).contains(&x)
                {
                    let x = x as usize;
                    let mask = 1_u8 << (y as usize & 7);
                    masks[x] |= mask;
                    if color == BinaryColor::On {
                        values[x] |= mask;
                    }
                    active_page = Some(page);
                }
            }
        }
        if let Some(previous) = active_page {
            self.commit_contiguous_page(previous, &values, &masks);
        }
    }

    #[cfg(feature = "graphics")]
    fn commit_contiguous_page(
        &mut self,
        page: usize,
        values: &[u8; DISPLAY_WIDTH],
        masks: &[u8; DISPLAY_WIDTH],
    ) {
        let local_page = page - self.start_page;
        for x in 0..self.size.width() {
            if masks[x] != 0 {
                let byte = &mut self.data[local_page * self.size.width() + x];
                *byte = (*byte & !masks[x]) | values[x];
            }
        }
    }
}

#[cfg(feature = "graphics")]
fn clipped_bounds(
    area: &Rectangle,
    display_width: usize,
    display_height: usize,
) -> Option<(i32, i32, i32, i32)> {
    let x0 = area.top_left.x.max(0);
    let y0 = area.top_left.y.max(0);
    let x1 = area
        .top_left
        .x
        .saturating_add_unsigned(area.size.width)
        .min(display_width as i32);
    let y1 = area
        .top_left
        .y
        .saturating_add_unsigned(area.size.height)
        .min(display_height as i32);
    (x0 < x1 && y0 < y1).then_some((x0, y0, x1, y1))
}

#[cfg(feature = "graphics")]
const fn bit_range_mask(start: usize, end: usize) -> u8 {
    let upper = (1_u16 << end) - 1;
    let lower = (1_u16 << start) - 1;
    (upper ^ lower) as u8
}

#[cfg(feature = "graphics")]
impl OriginDimensions for FullBuffer<'_> {
    fn size(&self) -> Size {
        Size::new(self.width() as u32, self.height() as u32)
    }
}

#[cfg(feature = "graphics")]
impl DrawTarget for FullBuffer<'_> {
    type Color = BinaryColor;
    type Error = Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        for Pixel(Point { x, y }, color) in pixels {
            self.set_pixel(x, y, color == BinaryColor::On);
        }
        Ok(())
    }

    fn fill_solid(&mut self, area: &Rectangle, color: Self::Color) -> Result<(), Self::Error> {
        self.fill_rectangle(area, color == BinaryColor::On);
        Ok(())
    }

    fn fill_contiguous<I>(&mut self, area: &Rectangle, colors: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Self::Color>,
    {
        self.fill_contiguous_pixels(area, colors);
        Ok(())
    }

    fn clear(&mut self, color: Self::Color) -> Result<(), Self::Error> {
        self.fill_all(color == BinaryColor::On);
        Ok(())
    }
}

#[cfg(feature = "graphics")]
impl Dimensions for PageCanvas<'_> {
    fn bounding_box(&self) -> Rectangle {
        Rectangle::new(
            Point::new(0, (self.start_page * 8) as i32),
            Size::new(self.size.width() as u32, (self.page_count * 8) as u32),
        )
    }
}

#[cfg(feature = "graphics")]
impl DrawTarget for PageCanvas<'_> {
    type Color = BinaryColor;
    type Error = Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        for Pixel(Point { x, y }, color) in pixels {
            self.set_pixel(x, y, color == BinaryColor::On);
        }
        Ok(())
    }

    fn fill_solid(&mut self, area: &Rectangle, color: Self::Color) -> Result<(), Self::Error> {
        self.fill_rectangle(area, color == BinaryColor::On);
        Ok(())
    }

    fn fill_contiguous<I>(&mut self, area: &Rectangle, colors: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Self::Color>,
    {
        self.fill_contiguous_pixels(area, colors);
        Ok(())
    }

    fn clear(&mut self, color: Self::Color) -> Result<(), Self::Error> {
        self.data
            .fill(if color == BinaryColor::On { u8::MAX } else { 0 });
        Ok(())
    }
}
