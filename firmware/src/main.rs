#![no_std]
#![no_main]
#![forbid(unsafe_code)]

use core::sync::atomic::Ordering;

use defmt_rtt as _;
use embassy_executor::Spawner;
use embassy_futures::select::{Either, select};
use embassy_stm32::{
    adc::{self, Adc, SampleTime},
    bind_interrupts, dma,
    flash::Flash,
    gpio::{Input, Level, Output, Pull, Speed},
    i2c, peripherals,
};
use embassy_time::{Duration, Instant, Ticker, Timer, with_timeout};
use gamebox_core::{App, AppEffect, Gesture, RenderSchedule, ui::UiRenderer};
use gamebox_f103_firmware::{
    board,
    platform::storage::SettingsJournal,
    services::{BuzzerCue, DROPPED_KEY_EVENTS, KEY_EVENTS, SAVE_REQUEST, try_play},
    tasks::{button_task, buzzer_task, gesture_cue, storage_task},
};
use oled_driver::{
    BufferedDisplay, DISPLAY_BYTES, DISPLAY_PAGES, DISPLAY_WIDTH, FullBuffer, I2cTransport, Ssd1306,
};
use panic_probe as _;
use static_cell::StaticCell;

bind_interrupts!(struct Irqs {
    I2C1_EV => i2c::EventInterruptHandler<peripherals::I2C1>;
    I2C1_ER => i2c::ErrorInterruptHandler<peripherals::I2C1>;
    DMA1_CHANNEL6 => dma::InterruptHandler<peripherals::DMA1_CH6>;
    DMA1_CHANNEL7 => dma::InterruptHandler<peripherals::DMA1_CH7>;
    ADC1_2 => adc::InterruptHandler<peripherals::ADC1>;
});

static FRONT_MEMORY: StaticCell<[u8; DISPLAY_BYTES]> = StaticCell::new();
static SCENE_MEMORY: StaticCell<[u8; DISPLAY_BYTES]> = StaticCell::new();

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(board::mcu_config());

    let mut flash = Flash::new_blocking(p.FLASH);
    let (journal, persistent) = SettingsJournal::load(&mut flash);

    // The old PCB leaves PA1 available as an analog noise source and used it
    // only to seed games. It is not documented as a battery divider, so no
    // fabricated battery voltage is shown in the UI.
    let adc_entropy = {
        let mut adc = Adc::new(p.ADC1);
        let mut entropy_pin = p.PA1;
        adc.read(&mut entropy_pin, SampleTime::CYCLES55_5).await
    };
    let entropy =
        u32::from(adc_entropy) ^ (Instant::now().as_ticks() as u32).rotate_left(13) ^ 0xa5c3_19e7;

    let oled_i2c = i2c::I2c::new(
        p.I2C1,
        p.PB8,
        p.PB9,
        p.DMA1_CH6,
        p.DMA1_CH7,
        Irqs,
        board::oled_i2c_config(),
    );
    let transport = I2cTransport::new(oled_i2c, board::OLED_ADDRESS)
        .expect("the board OLED address is a valid seven-bit address");
    let front = FullBuffer::new(FRONT_MEMORY.init([0; DISPLAY_BYTES]))
        .expect("front framebuffer geometry is fixed at 128x64");
    let mut scene = FullBuffer::new(SCENE_MEMORY.init([0; DISPLAY_BYTES]))
        .expect("scene framebuffer geometry is fixed at 128x64");
    let mut display = BufferedDisplay::new(Ssd1306::default(), transport, front)
        .expect("SSD1306 geometry matches the framebuffer");

    let mut startup_backoff_ms = 25_u64;
    loop {
        match with_timeout(board::OLED_OPERATION_TIMEOUT, display.init_async()).await {
            Ok(Ok(report)) => {
                defmt::info!("SSD1306 ready: {} data bytes", report.data_bytes);
                break;
            }
            Ok(Err(_)) => {
                defmt::warn!(
                    "SSD1306 initialization bus error; retry in {} ms",
                    startup_backoff_ms
                );
                board::recover_oled_i2c();
                Timer::after_millis(startup_backoff_ms).await;
                startup_backoff_ms = (startup_backoff_ms * 2).min(1_000);
            }
            Err(_) => {
                defmt::warn!(
                    "SSD1306 initialization timed out; retry in {} ms",
                    startup_backoff_ms
                );
                board::recover_oled_i2c();
                Timer::after_millis(startup_backoff_ms).await;
                startup_backoff_ms = (startup_backoff_ms * 2).min(1_000);
            }
        }
    }
    if !matches!(
        with_timeout(
            board::OLED_OPERATION_TIMEOUT,
            display.set_contrast_async(persistent.settings.contrast)
        )
        .await,
        Ok(Ok(_))
    ) {
        defmt::warn!("initial OLED contrast command failed or timed out");
        board::recover_oled_i2c();
    }

    let buttons = [
        Input::new(p.PB7, Pull::Up),  // Up
        Input::new(p.PB5, Pull::Up),  // Down
        Input::new(p.PB6, Pull::Up),  // Left
        Input::new(p.PB4, Pull::Up),  // Right; JTAG released, SWD retained
        Input::new(p.PB12, Pull::Up), // Jump
        Input::new(p.PB13, Pull::Up), // Function
        Input::new(p.PB14, Pull::Up), // Enter
        Input::new(p.PB15, Pull::Up), // Back
    ];
    let buzzer = Output::new(p.PA12, Level::Low, Speed::Low);
    spawner.spawn(button_task(buttons).expect("button task has one static slot"));
    spawner.spawn(buzzer_task(buzzer).expect("buzzer task has one static slot"));
    spawner.spawn(storage_task(flash, journal).expect("storage task has one static slot"));

    let renderer = UiRenderer;
    let now_ms = Instant::now().as_millis() as u32;
    let mut app = App::new(now_ms, persistent, entropy);
    let mut frame_tick = Ticker::every(Duration::from_millis(16));
    let mut render_needed = true;
    let mut panel_online = true;
    let mut retry_at_ms = 0_u32;
    let mut retry_backoff_ms = 50_u32;
    let mut reported_dropped_events = 0_u32;

    loop {
        match select(KEY_EVENTS.receive(), frame_tick.next()).await {
            Either::First(event) => {
                let sound_enabled = app.persistent_data().settings.sound_enabled;
                let mode_before = app.mode() as u8;
                let effect = app.handle_event(event);
                defmt::debug!(
                    "key event: key={} gesture={} at_ms={} mode={}=>{} page={} selection={}",
                    event.key as u8,
                    event.gesture as u8,
                    event.at_ms,
                    mode_before,
                    app.mode() as u8,
                    app.menu().page_id() as u8,
                    app.menu().selected_index() as u8
                );
                dispatch_effect(effect);
                if sound_enabled && let Some(cue) = gesture_cue(event.key, event.gesture) {
                    try_play(cue);
                }
                render_needed |= event.gesture != Gesture::Released;
            }
            Either::Second(()) => {
                let now_ms = Instant::now().as_millis() as u32;
                let dropped_events = DROPPED_KEY_EVENTS.load(Ordering::Relaxed);
                if dropped_events != reported_dropped_events {
                    defmt::warn!("button event queue drops: {}", dropped_events);
                    reported_dropped_events = dropped_events;
                }
                let score_before = app.snake().score();
                let countdown_was_complete = app.countdown_is_completed();
                if app.tick(now_ms) == RenderSchedule::Render {
                    render_needed = true;
                }
                dispatch_effect(app.take_pending_effect());

                if app.persistent_data().settings.sound_enabled {
                    if app.snake().score() > score_before {
                        try_play(BuzzerCue::Confirm);
                    }
                    if !countdown_was_complete && app.countdown_is_completed() {
                        try_play(BuzzerCue::Alert);
                    }
                }
            }
        }

        let now_ms = Instant::now().as_millis() as u32;
        let retry_due = !panel_online && deadline_reached(now_ms, retry_at_ms);
        if render_needed {
            if renderer.draw(&mut scene, &app, now_ms, 0).is_err() {
                defmt::error!("infallible framebuffer renderer reported an error");
            }
            display
                .buffer_mut()
                .blit_native(0, 0, DISPLAY_WIDTH, DISPLAY_PAGES, scene.as_bytes())
                .expect("full-scene blit dimensions are compile-time constants");
        }

        if panel_online && render_needed {
            match with_timeout(board::OLED_OPERATION_TIMEOUT, display.flush_async()).await {
                Ok(Ok(_)) => {}
                Ok(Err(_)) => {
                    panel_online = false;
                    retry_at_ms = now_ms.wrapping_add(retry_backoff_ms);
                    board::recover_oled_i2c();
                    defmt::warn!("OLED transfer failed; entering recovery backoff");
                }
                Err(_) => {
                    panel_online = false;
                    retry_at_ms = now_ms.wrapping_add(retry_backoff_ms);
                    board::recover_oled_i2c();
                    defmt::warn!("OLED transfer timed out; entering recovery backoff");
                }
            }
        } else if retry_due {
            board::recover_oled_i2c();
            match with_timeout(board::OLED_OPERATION_TIMEOUT, display.reinitialize_async()).await {
                Ok(Ok(_)) => {
                    panel_online = true;
                    retry_backoff_ms = 50;
                    defmt::info!("OLED link recovered and framebuffer resent");
                }
                Ok(Err(_)) | Err(_) => {
                    retry_at_ms = now_ms.wrapping_add(retry_backoff_ms);
                    retry_backoff_ms = (retry_backoff_ms * 2).min(2_000);
                }
            }
        }
        render_needed = false;
    }
}

fn dispatch_effect(effect: AppEffect) {
    if let AppEffect::Persist(snapshot) = effect {
        SAVE_REQUEST.signal(snapshot);
    }
}

const fn deadline_reached(now: u32, deadline: u32) -> bool {
    now.wrapping_sub(deadline) < 0x8000_0000
}
