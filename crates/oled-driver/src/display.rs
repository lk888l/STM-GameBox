//! Display orchestration for full and page buffers.

use crate::{
    CommandBuffer, ConfigError, Controller, DriverError, FlushPolicy, FlushReport, FullBuffer,
    PageBuffer, PageCanvas,
};

#[cfg(feature = "async")]
use crate::AsyncTransport;
#[cfg(feature = "blocking")]
use crate::BlockingTransport;
#[cfg(feature = "graphics")]
use core::convert::Infallible;
#[cfg(feature = "graphics")]
use embedded_graphics_core::{
    Pixel,
    draw_target::DrawTarget,
    geometry::{OriginDimensions, Size},
    pixelcolor::BinaryColor,
    primitives::Rectangle,
};

/// Owned components returned by [`BufferedDisplay::release`] or
/// [`PagedDisplay::release`].
#[derive(Debug)]
pub struct DisplayParts<C, T, B> {
    /// Controller command encoder.
    pub controller: C,
    /// Command/data transport.
    pub transport: T,
    /// User-owned framebuffer wrapper.
    pub buffer: B,
}

/// A full-framebuffer display with exact dirty-byte tracking.
pub struct BufferedDisplay<'a, C, T> {
    controller: C,
    transport: T,
    buffer: FullBuffer<'a>,
    flush_policy: FlushPolicy,
    initialized: bool,
}

impl<'a, C, T> BufferedDisplay<'a, C, T>
where
    C: Controller,
{
    /// Combines a compatible controller, transport, and full framebuffer.
    pub fn new(controller: C, transport: T, buffer: FullBuffer<'a>) -> Result<Self, ConfigError> {
        controller.validate_geometry()?;
        if controller.width() != buffer.width()
            || controller.height() != buffer.height()
            || controller.pages() != buffer.pages()
        {
            return Err(ConfigError::GeometryMismatch);
        }
        Ok(Self {
            controller,
            transport,
            buffer,
            flush_policy: FlushPolicy::default(),
            initialized: false,
        })
    }

    /// Changes dirty-region coalescing behavior.
    #[must_use]
    pub fn with_flush_policy(mut self, flush_policy: FlushPolicy) -> Self {
        self.flush_policy = flush_policy;
        self
    }

    /// Returns the active flush policy.
    #[must_use]
    pub const fn flush_policy(&self) -> FlushPolicy {
        self.flush_policy
    }

    /// Returns a read-only view of the framebuffer.
    #[must_use]
    pub const fn buffer(&self) -> &FullBuffer<'a> {
        &self.buffer
    }

    /// Returns the framebuffer drawing surface.
    pub const fn buffer_mut(&mut self) -> &mut FullBuffer<'a> {
        &mut self.buffer
    }

    /// Returns whether initialization completed successfully.
    #[must_use]
    pub const fn is_initialized(&self) -> bool {
        self.initialized
    }

    /// Releases all owned components.
    #[must_use]
    pub fn release(self) -> DisplayParts<C, T, FullBuffer<'a>> {
        DisplayParts {
            controller: self.controller,
            transport: self.transport,
            buffer: self.buffer,
        }
    }
}

#[cfg(feature = "blocking")]
impl<C, T> BufferedDisplay<'_, C, T>
where
    C: Controller,
    T: BlockingTransport,
{
    /// Initializes the controller, clears GDDRAM, then enables the panel.
    pub fn init(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        self.initialized = false;
        let mut report = FlushReport::default();

        let mut commands = CommandBuffer::new();
        self.controller.encode_init(&mut commands)?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .map_err(DriverError::Bus)?,
        );

        self.buffer.fill_all(false);
        self.buffer.mark_all_dirty();
        report.merge(self.flush_full_blocking_inner()?);

        let mut commands = CommandBuffer::new();
        self.controller.encode_display_on(true, &mut commands)?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .map_err(DriverError::Bus)?,
        );
        self.initialized = true;
        Ok(report)
    }

    /// Reinitializes the controller while preserving and fully resending the framebuffer.
    ///
    /// This recovery path marks the complete framebuffer dirty before touching the bus.
    /// A command or data failure therefore remains retryable without losing the scene.
    pub fn reinitialize(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        self.initialized = false;
        self.buffer.mark_all_dirty();
        let mut report = FlushReport::default();

        let mut commands = CommandBuffer::new();
        self.controller.encode_init(&mut commands)?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .map_err(DriverError::Bus)?,
        );

        report.merge(self.flush_full_blocking_inner()?);

        let mut commands = CommandBuffer::new();
        self.controller.encode_display_on(true, &mut commands)?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .map_err(DriverError::Bus)?,
        );
        self.initialized = true;
        Ok(report)
    }

    /// Flushes dirty bytes according to the configured policy.
    pub fn flush(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        if !self.initialized {
            return Err(DriverError::NotInitialized);
        }
        self.flush_dirty_blocking()
    }

    /// Marks and flushes the complete framebuffer.
    pub fn flush_full(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        if !self.initialized {
            return Err(DriverError::NotInitialized);
        }
        self.buffer.mark_all_dirty();
        self.flush_full_blocking_inner()
    }

    /// Enables or disables panel output without changing framebuffer memory.
    pub fn set_display_on(&mut self, on: bool) -> Result<FlushReport, DriverError<T::Error>> {
        self.ensure_initialized()?;
        let mut commands = CommandBuffer::new();
        self.controller.encode_display_on(on, &mut commands)?;
        self.send_commands_blocking(&commands)
    }

    /// Sets panel contrast.
    pub fn set_contrast(&mut self, contrast: u8) -> Result<FlushReport, DriverError<T::Error>> {
        self.ensure_initialized()?;
        let mut commands = CommandBuffer::new();
        self.controller.encode_contrast(contrast, &mut commands)?;
        self.send_commands_blocking(&commands)
    }

    /// Enables or disables hardware inversion.
    pub fn set_invert(&mut self, invert: bool) -> Result<FlushReport, DriverError<T::Error>> {
        self.ensure_initialized()?;
        let mut commands = CommandBuffer::new();
        self.controller.encode_invert(invert, &mut commands)?;
        self.send_commands_blocking(&commands)
    }

    fn ensure_initialized(&self) -> Result<(), DriverError<T::Error>> {
        if self.initialized {
            Ok(())
        } else {
            Err(DriverError::NotInitialized)
        }
    }

    fn send_commands_blocking(
        &mut self,
        commands: &CommandBuffer,
    ) -> Result<FlushReport, DriverError<T::Error>> {
        let transfer = self
            .transport
            .write_commands(commands.as_slice())
            .map_err(DriverError::Bus)?;
        let mut report = FlushReport::default();
        report.add_commands(transfer);
        Ok(report)
    }

    fn flush_dirty_blocking(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        let mut report = FlushReport::default();
        for page in 0..self.buffer.pages() {
            let mut cursor = 0;
            let mut page_touched = false;
            while let Some((start, end)) =
                self.buffer.next_dirty_run(page, cursor, self.flush_policy)
            {
                let mut commands = CommandBuffer::new();
                self.controller.encode_window(
                    start as u8,
                    end as u8,
                    page as u8,
                    page as u8,
                    &mut commands,
                )?;
                report.add_commands(
                    self.transport
                        .write_commands(commands.as_slice())
                        .map_err(DriverError::Bus)?,
                );

                let data_report = self
                    .transport
                    .write_data(&self.buffer.page_slice(page)[start..=end])
                    .map_err(DriverError::Bus)?;
                report.add_data(data_report);
                self.buffer.clear_dirty_range(page, start, end);

                report.regions = report.regions.saturating_add(1);
                page_touched = true;
                cursor = end + 1;
            }
            if page_touched {
                report.pages = report.pages.saturating_add(1);
            }
        }
        Ok(report)
    }

    fn flush_full_blocking_inner(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        let mut report = FlushReport::default();
        let mut commands = CommandBuffer::new();
        self.controller.encode_window(
            0,
            (self.buffer.width() - 1) as u8,
            0,
            (self.buffer.pages() - 1) as u8,
            &mut commands,
        )?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .map_err(DriverError::Bus)?,
        );
        report.add_data(
            self.transport
                .write_data(self.buffer.as_bytes())
                .map_err(DriverError::Bus)?,
        );
        self.buffer.clear_all_dirty();
        report.regions = 1;
        report.pages = self.buffer.pages() as u8;
        Ok(report)
    }
}

#[cfg(feature = "async")]
impl<C, T> BufferedDisplay<'_, C, T>
where
    C: Controller,
    T: AsyncTransport,
{
    /// Asynchronously initializes, clears GDDRAM, and enables the panel.
    pub async fn init_async(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        self.initialized = false;
        let mut report = FlushReport::default();

        let mut commands = CommandBuffer::new();
        self.controller.encode_init(&mut commands)?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .await
                .map_err(DriverError::Bus)?,
        );

        self.buffer.fill_all(false);
        self.buffer.mark_all_dirty();
        report.merge(self.flush_full_async_inner().await?);

        let mut commands = CommandBuffer::new();
        self.controller.encode_display_on(true, &mut commands)?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .await
                .map_err(DriverError::Bus)?,
        );
        self.initialized = true;
        Ok(report)
    }

    /// Asynchronously reinitializes while preserving and fully resending the framebuffer.
    ///
    /// The complete framebuffer is marked dirty before the first bus operation, so a
    /// failed or cancelled recovery can be retried without losing the scene.
    pub async fn reinitialize_async(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        self.initialized = false;
        self.buffer.mark_all_dirty();
        let mut report = FlushReport::default();

        let mut commands = CommandBuffer::new();
        self.controller.encode_init(&mut commands)?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .await
                .map_err(DriverError::Bus)?,
        );

        report.merge(self.flush_full_async_inner().await?);

        let mut commands = CommandBuffer::new();
        self.controller.encode_display_on(true, &mut commands)?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .await
                .map_err(DriverError::Bus)?,
        );
        self.initialized = true;
        Ok(report)
    }

    /// Asynchronously flushes dirty bytes.
    pub async fn flush_async(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        if !self.initialized {
            return Err(DriverError::NotInitialized);
        }
        self.flush_dirty_async_inner().await
    }

    /// Marks and asynchronously flushes the complete framebuffer.
    pub async fn flush_full_async(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        if !self.initialized {
            return Err(DriverError::NotInitialized);
        }
        self.buffer.mark_all_dirty();
        self.flush_full_async_inner().await
    }

    /// Asynchronously enables or disables panel output.
    pub async fn set_display_on_async(
        &mut self,
        on: bool,
    ) -> Result<FlushReport, DriverError<T::Error>> {
        self.ensure_initialized_async()?;
        let mut commands = CommandBuffer::new();
        self.controller.encode_display_on(on, &mut commands)?;
        self.send_commands_async(&commands).await
    }

    /// Asynchronously sets panel contrast.
    pub async fn set_contrast_async(
        &mut self,
        contrast: u8,
    ) -> Result<FlushReport, DriverError<T::Error>> {
        self.ensure_initialized_async()?;
        let mut commands = CommandBuffer::new();
        self.controller.encode_contrast(contrast, &mut commands)?;
        self.send_commands_async(&commands).await
    }

    /// Asynchronously enables or disables hardware inversion.
    pub async fn set_invert_async(
        &mut self,
        invert: bool,
    ) -> Result<FlushReport, DriverError<T::Error>> {
        self.ensure_initialized_async()?;
        let mut commands = CommandBuffer::new();
        self.controller.encode_invert(invert, &mut commands)?;
        self.send_commands_async(&commands).await
    }

    fn ensure_initialized_async(&self) -> Result<(), DriverError<T::Error>> {
        if self.initialized {
            Ok(())
        } else {
            Err(DriverError::NotInitialized)
        }
    }

    async fn send_commands_async(
        &mut self,
        commands: &CommandBuffer,
    ) -> Result<FlushReport, DriverError<T::Error>> {
        let transfer = self
            .transport
            .write_commands(commands.as_slice())
            .await
            .map_err(DriverError::Bus)?;
        let mut report = FlushReport::default();
        report.add_commands(transfer);
        Ok(report)
    }

    async fn flush_dirty_async_inner(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        let mut report = FlushReport::default();
        for page in 0..self.buffer.pages() {
            let mut cursor = 0;
            let mut page_touched = false;
            while let Some((start, end)) =
                self.buffer.next_dirty_run(page, cursor, self.flush_policy)
            {
                let mut commands = CommandBuffer::new();
                self.controller.encode_window(
                    start as u8,
                    end as u8,
                    page as u8,
                    page as u8,
                    &mut commands,
                )?;
                report.add_commands(
                    self.transport
                        .write_commands(commands.as_slice())
                        .await
                        .map_err(DriverError::Bus)?,
                );

                let data_report = self
                    .transport
                    .write_data(&self.buffer.page_slice(page)[start..=end])
                    .await
                    .map_err(DriverError::Bus)?;
                report.add_data(data_report);
                self.buffer.clear_dirty_range(page, start, end);

                report.regions = report.regions.saturating_add(1);
                page_touched = true;
                cursor = end + 1;
            }
            if page_touched {
                report.pages = report.pages.saturating_add(1);
            }
        }
        Ok(report)
    }

    async fn flush_full_async_inner(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        let mut report = FlushReport::default();
        let mut commands = CommandBuffer::new();
        self.controller.encode_window(
            0,
            (self.buffer.width() - 1) as u8,
            0,
            (self.buffer.pages() - 1) as u8,
            &mut commands,
        )?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .await
                .map_err(DriverError::Bus)?,
        );
        report.add_data(
            self.transport
                .write_data(self.buffer.as_bytes())
                .await
                .map_err(DriverError::Bus)?,
        );
        self.buffer.clear_all_dirty();
        report.regions = 1;
        report.pages = self.buffer.pages() as u8;
        Ok(report)
    }
}

#[cfg(feature = "graphics")]
impl<C, T> OriginDimensions for BufferedDisplay<'_, C, T>
where
    C: Controller<Color = BinaryColor>,
{
    fn size(&self) -> Size {
        Size::new(
            self.controller.width() as u32,
            self.controller.height() as u32,
        )
    }
}

#[cfg(feature = "graphics")]
impl<C, T> DrawTarget for BufferedDisplay<'_, C, T>
where
    C: Controller<Color = BinaryColor>,
{
    type Color = BinaryColor;
    type Error = Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        self.buffer.draw_iter(pixels)
    }

    fn fill_solid(&mut self, area: &Rectangle, color: Self::Color) -> Result<(), Self::Error> {
        self.buffer.fill_solid(area, color)
    }

    fn fill_contiguous<I>(&mut self, area: &Rectangle, colors: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Self::Color>,
    {
        self.buffer.fill_contiguous(area, colors)
    }

    fn clear(&mut self, color: Self::Color) -> Result<(), Self::Error> {
        self.buffer.clear(color)
    }
}

/// A low-RAM display that redraws a scene once for each page-buffer pass.
pub struct PagedDisplay<'a, C, T> {
    controller: C,
    transport: T,
    buffer: PageBuffer<'a>,
    initialized: bool,
}

impl<'a, C, T> PagedDisplay<'a, C, T>
where
    C: Controller,
{
    /// Combines a compatible controller, transport, and page buffer.
    pub fn new(controller: C, transport: T, buffer: PageBuffer<'a>) -> Result<Self, ConfigError> {
        controller.validate_geometry()?;
        if controller.width() != buffer.width() || controller.pages() != buffer.pages() {
            return Err(ConfigError::GeometryMismatch);
        }
        Ok(Self {
            controller,
            transport,
            buffer,
            initialized: false,
        })
    }

    /// Returns whether initialization completed successfully.
    #[must_use]
    pub const fn is_initialized(&self) -> bool {
        self.initialized
    }

    /// Releases all owned components.
    #[must_use]
    pub fn release(self) -> DisplayParts<C, T, PageBuffer<'a>> {
        DisplayParts {
            controller: self.controller,
            transport: self.transport,
            buffer: self.buffer,
        }
    }
}

#[cfg(feature = "blocking")]
impl<C, T> PagedDisplay<'_, C, T>
where
    C: Controller,
    T: BlockingTransport,
{
    /// Initializes the controller, clears GDDRAM, then enables the panel.
    pub fn init(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        self.initialized = false;
        let mut report = FlushReport::default();
        let mut commands = CommandBuffer::new();
        self.controller.encode_init(&mut commands)?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .map_err(DriverError::Bus)?,
        );
        report.merge(self.clear_panel_blocking()?);

        let mut commands = CommandBuffer::new();
        self.controller.encode_display_on(true, &mut commands)?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .map_err(DriverError::Bus)?,
        );
        self.initialized = true;
        Ok(report)
    }

    /// Renders the scene for every page-buffer pass and sends each pass.
    pub fn render_pages<F>(&mut self, mut draw: F) -> Result<FlushReport, DriverError<T::Error>>
    where
        F: FnMut(&mut PageCanvas<'_>),
    {
        if !self.initialized {
            return Err(DriverError::NotInitialized);
        }
        let mut report = FlushReport::default();
        let capacity = self.buffer.capacity_pages();
        let mut start_page = 0;
        while start_page < self.buffer.pages() {
            let page_count = capacity.min(self.buffer.pages() - start_page);
            self.buffer.clear();
            {
                let mut canvas = self.buffer.canvas(start_page, page_count);
                draw(&mut canvas);
            }
            report.merge(self.send_page_blocking(start_page, page_count)?);
            start_page += page_count;
        }
        Ok(report)
    }

    /// Enables or disables panel output.
    pub fn set_display_on(&mut self, on: bool) -> Result<FlushReport, DriverError<T::Error>> {
        self.ensure_paged_initialized()?;
        let mut commands = CommandBuffer::new();
        self.controller.encode_display_on(on, &mut commands)?;
        self.send_paged_commands_blocking(&commands)
    }

    /// Sets panel contrast.
    pub fn set_contrast(&mut self, contrast: u8) -> Result<FlushReport, DriverError<T::Error>> {
        self.ensure_paged_initialized()?;
        let mut commands = CommandBuffer::new();
        self.controller.encode_contrast(contrast, &mut commands)?;
        self.send_paged_commands_blocking(&commands)
    }

    /// Enables or disables hardware inversion.
    pub fn set_invert(&mut self, invert: bool) -> Result<FlushReport, DriverError<T::Error>> {
        self.ensure_paged_initialized()?;
        let mut commands = CommandBuffer::new();
        self.controller.encode_invert(invert, &mut commands)?;
        self.send_paged_commands_blocking(&commands)
    }

    fn ensure_paged_initialized(&self) -> Result<(), DriverError<T::Error>> {
        if self.initialized {
            Ok(())
        } else {
            Err(DriverError::NotInitialized)
        }
    }

    fn send_paged_commands_blocking(
        &mut self,
        commands: &CommandBuffer,
    ) -> Result<FlushReport, DriverError<T::Error>> {
        let mut report = FlushReport::default();
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .map_err(DriverError::Bus)?,
        );
        Ok(report)
    }

    fn clear_panel_blocking(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        self.buffer.clear();
        let capacity = self.buffer.capacity_pages();
        let mut report = FlushReport::default();
        let mut start_page = 0;
        while start_page < self.buffer.pages() {
            let count = capacity.min(self.buffer.pages() - start_page);
            report.merge(self.send_page_blocking(start_page, count)?);
            start_page += count;
        }
        Ok(report)
    }

    fn send_page_blocking(
        &mut self,
        start_page: usize,
        page_count: usize,
    ) -> Result<FlushReport, DriverError<T::Error>> {
        let mut report = FlushReport::default();
        let mut commands = CommandBuffer::new();
        self.controller.encode_window(
            0,
            (self.buffer.width() - 1) as u8,
            start_page as u8,
            (start_page + page_count - 1) as u8,
            &mut commands,
        )?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .map_err(DriverError::Bus)?,
        );
        report.add_data(
            self.transport
                .write_data(self.buffer.as_bytes(page_count))
                .map_err(DriverError::Bus)?,
        );
        report.regions = 1;
        report.pages = page_count as u8;
        Ok(report)
    }
}

#[cfg(feature = "async")]
impl<C, T> PagedDisplay<'_, C, T>
where
    C: Controller,
    T: AsyncTransport,
{
    /// Asynchronously initializes, clears GDDRAM, and enables the panel.
    pub async fn init_async(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        self.initialized = false;
        let mut report = FlushReport::default();
        let mut commands = CommandBuffer::new();
        self.controller.encode_init(&mut commands)?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .await
                .map_err(DriverError::Bus)?,
        );
        report.merge(self.clear_panel_async().await?);

        let mut commands = CommandBuffer::new();
        self.controller.encode_display_on(true, &mut commands)?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .await
                .map_err(DriverError::Bus)?,
        );
        self.initialized = true;
        Ok(report)
    }

    /// Redraws and asynchronously sends every page-buffer pass.
    pub async fn render_pages_async<F>(
        &mut self,
        mut draw: F,
    ) -> Result<FlushReport, DriverError<T::Error>>
    where
        F: FnMut(&mut PageCanvas<'_>),
    {
        if !self.initialized {
            return Err(DriverError::NotInitialized);
        }
        let mut report = FlushReport::default();
        let capacity = self.buffer.capacity_pages();
        let mut start_page = 0;
        while start_page < self.buffer.pages() {
            let page_count = capacity.min(self.buffer.pages() - start_page);
            self.buffer.clear();
            {
                let mut canvas = self.buffer.canvas(start_page, page_count);
                draw(&mut canvas);
            }
            report.merge(self.send_page_async(start_page, page_count).await?);
            start_page += page_count;
        }
        Ok(report)
    }

    /// Asynchronously enables or disables panel output.
    pub async fn set_display_on_async(
        &mut self,
        on: bool,
    ) -> Result<FlushReport, DriverError<T::Error>> {
        self.ensure_paged_initialized_async()?;
        let mut commands = CommandBuffer::new();
        self.controller.encode_display_on(on, &mut commands)?;
        self.send_paged_commands_async(&commands).await
    }

    /// Asynchronously sets panel contrast.
    pub async fn set_contrast_async(
        &mut self,
        contrast: u8,
    ) -> Result<FlushReport, DriverError<T::Error>> {
        self.ensure_paged_initialized_async()?;
        let mut commands = CommandBuffer::new();
        self.controller.encode_contrast(contrast, &mut commands)?;
        self.send_paged_commands_async(&commands).await
    }

    /// Asynchronously enables or disables hardware inversion.
    pub async fn set_invert_async(
        &mut self,
        invert: bool,
    ) -> Result<FlushReport, DriverError<T::Error>> {
        self.ensure_paged_initialized_async()?;
        let mut commands = CommandBuffer::new();
        self.controller.encode_invert(invert, &mut commands)?;
        self.send_paged_commands_async(&commands).await
    }

    fn ensure_paged_initialized_async(&self) -> Result<(), DriverError<T::Error>> {
        if self.initialized {
            Ok(())
        } else {
            Err(DriverError::NotInitialized)
        }
    }

    async fn send_paged_commands_async(
        &mut self,
        commands: &CommandBuffer,
    ) -> Result<FlushReport, DriverError<T::Error>> {
        let mut report = FlushReport::default();
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .await
                .map_err(DriverError::Bus)?,
        );
        Ok(report)
    }

    async fn clear_panel_async(&mut self) -> Result<FlushReport, DriverError<T::Error>> {
        self.buffer.clear();
        let capacity = self.buffer.capacity_pages();
        let mut report = FlushReport::default();
        let mut start_page = 0;
        while start_page < self.buffer.pages() {
            let count = capacity.min(self.buffer.pages() - start_page);
            report.merge(self.send_page_async(start_page, count).await?);
            start_page += count;
        }
        Ok(report)
    }

    async fn send_page_async(
        &mut self,
        start_page: usize,
        page_count: usize,
    ) -> Result<FlushReport, DriverError<T::Error>> {
        let mut report = FlushReport::default();
        let mut commands = CommandBuffer::new();
        self.controller.encode_window(
            0,
            (self.buffer.width() - 1) as u8,
            start_page as u8,
            (start_page + page_count - 1) as u8,
            &mut commands,
        )?;
        report.add_commands(
            self.transport
                .write_commands(commands.as_slice())
                .await
                .map_err(DriverError::Bus)?,
        );
        report.add_data(
            self.transport
                .write_data(self.buffer.as_bytes(page_count))
                .await
                .map_err(DriverError::Bus)?,
        );
        report.regions = 1;
        report.pages = page_count as u8;
        Ok(report)
    }
}
