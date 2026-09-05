#![no_std]
#![no_main]
#![forbid(unsafe_code)]

use core::sync::atomic::Ordering;

use defmt_rtt as _;
use embassy_executor::Spawner;
use embassy_futures::{
    select::{Either, select},
    yield_now,
};
use embassy_stm32::{
    adc::{self, Adc, SampleTime},
    bind_interrupts, dma,
    flash::Flash,
    gpio::{Input, Level, Output, Pull, Speed},
    peripherals, usart,
};
use embassy_time::{Instant, Ticker, Timer};
use gamebox_core::{
    App, AppEffect, Gesture, RenderSchedule, SystemStats,
    ui::{FRAME_BYTES, UiRenderer},
};
use gamebox_f103_firmware::{
    board,
    platform::{oled, rtc::RtcClock, storage::SettingsJournal},
    services::{
        BuzzerCue, DROPPED_KEY_EVENTS, DROPPED_UART_EVENTS, HELD_KEYS, KEY_EVENTS,
        KEY_EVENTS_PROCESSED, MAX_KEY_PRESS_AGE_MS, MAX_RENDER_TIME_US, OLED_ERRORS,
        OLED_TRANSFERS, RENDERED_FRAMES, SAVE_REQUEST, STORAGE_ERRORS, UART_ERRORS, try_play,
    },
    tasks::{button_task, buzzer_task, storage_task, uart_task},
};
use panic_probe as _;
use static_cell::StaticCell;

bind_interrupts!(struct Irqs {
    DMA1_CHANNEL4 => dma::InterruptHandler<peripherals::DMA1_CH4>;
    ADC1_2 => adc::InterruptHandler<peripherals::ADC1>;
});

static FRONT_MEMORY: StaticCell<[u8; FRAME_BYTES]> = StaticCell::new();
static SCENE_MEMORY: StaticCell<[u8; FRAME_BYTES]> = StaticCell::new();

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(board::mcu_config());

    let rtc = match RtcClock::new(p.RTC, p.BKP) {
        Ok(clock) => Some(clock),
        Err(_) => {
            defmt::warn!("RTC did not start; clock UI remains available in error state");
            None
        }
    };

    let mut flash = Flash::new_blocking(p.FLASH);
    let (journal, persistent) = SettingsJournal::load(&mut flash);

    // PA1 is an otherwise unused analog input on this PCB. It contributes a
    // little startup entropy for games; it is not presented as battery data.
    let entropy = {
        let mut adc = Adc::new(p.ADC1);
        let mut entropy_pin = p.PA1;
        let mut mixed = 0xa5c3_19e7_u32;
        for index in 0..16_u32 {
            let sample = u32::from(adc.read(&mut entropy_pin, SampleTime::CYCLES55_5).await);
            mixed ^= sample.rotate_left(index & 15);
            mixed = mixed.rotate_left(5).wrapping_mul(0x9e37_79b9);
        }
        mixed ^ (Instant::now().as_ticks() as u32).rotate_left(13)
    };

    // This composition-root resource selection is the only interface-specific
    // code outside the OLED adapter. Unselected pins and DMA remain untouched.
    #[cfg(not(feature = "oled-i2c"))]
    let oled_resources = oled::Resources {
        spi: p.SPI1,
        sck: p.PA5,
        mosi: p.PA7,
        dc: p.PA6,
        cs: p.PA4,
        reset: p.PA8,
        tx_dma: p.DMA1_CH3,
    };
    #[cfg(feature = "oled-i2c")]
    let oled_resources = oled::Resources {
        i2c: p.I2C1,
        scl: p.PB8,
        sda: p.PB9,
        tx_dma: p.DMA1_CH6,
        rx_dma: p.DMA1_CH7,
    };
    let mut display = oled::Oled::new(oled_resources, FRONT_MEMORY.init([0; FRAME_BYTES]));
    let scene = SCENE_MEMORY.init([0; FRAME_BYTES]);
    defmt::info!(
        "OLED interface: {}, {} Hz",
        oled::INTERFACE,
        oled::BUS_FREQUENCY_HZ
    );

    let mut startup_backoff_ms = 25_u64;
    loop {
        match display.init().await {
            Ok(report) => {
                defmt::info!("SSD1306 ready: {} data bytes", report.data_bytes);
                break;
            }
            Err(oled::Error::Driver(_)) => {
                defmt::warn!(
                    "SSD1306 bus initialization error; retry in {} ms",
                    startup_backoff_ms
                );
                display.recover_bus();
            }
            Err(oled::Error::Timeout) => {
                defmt::warn!(
                    "SSD1306 initialization timed out; retry in {} ms",
                    startup_backoff_ms
                );
                display.recover_bus();
            }
        }
        Timer::after_millis(startup_backoff_ms).await;
        startup_backoff_ms = (startup_backoff_ms * 2).min(1_000);
    }

    let desired_contrast = persistent.settings.brightness.contrast();
    let mut applied_contrast = match display.set_contrast(desired_contrast).await {
        Ok(_) => Some(desired_contrast),
        Err(_) => {
            OLED_ERRORS.fetch_add(1, Ordering::Relaxed);
            defmt::warn!("initial OLED contrast command failed or timed out");
            display.recover_bus();
            None
        }
    };

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
    // The four board LEDs are normally dark; retaining their output handles
    // keeps the pins in the same defined state as the reference firmware.
    let _status_leds = [
        Output::new(p.PB0, Level::Low, Speed::Low),
        Output::new(p.PB1, Level::Low, Speed::Low),
        Output::new(p.PB10, Level::Low, Speed::Low),
        Output::new(p.PB11, Level::Low, Speed::Low),
    ];
    let buzzer = Output::new(p.PA12, Level::Low, Speed::Low);
    let mut uart_config = usart::Config::default();
    uart_config.baudrate = 115_200;
    let uart_tx = usart::UartTx::new(p.USART1, p.PA9, p.DMA1_CH4, Irqs, uart_config)
        .expect("USART1 TX at 115200 baud is supported by the 72 MHz clock tree");

    spawner.spawn(button_task(buttons).expect("button task has one static slot"));
    spawner.spawn(buzzer_task(buzzer).expect("buzzer task has one static slot"));
    spawner.spawn(uart_task(uart_tx).expect("UART task has one static slot"));
    spawner.spawn(storage_task(flash, journal).expect("storage task has one static slot"));

    let renderer = UiRenderer;
    let now_ms = Instant::now().as_millis() as u32;
    let mut app = App::new(now_ms, persistent, entropy);
    app.set_clock_snapshot(rtc.as_ref().and_then(|clock| clock.now().ok()));

    let mut frame_tick = Ticker::every(oled::FRAME_INTERVAL);
    let mut next_rtc_read_ms = now_ms.wrapping_add(250);
    let mut render_needed = true;
    let mut panel_online = true;
    let mut retry_at_ms = 0_u32;
    let mut retry_backoff_ms = 50_u32;
    let mut reported_input_drops = 0_u32;
    let mut reported_uart_drops = 0_u32;

    loop {
        match select(KEY_EVENTS.receive(), frame_tick.next()).await {
            Either::First(event) => {
                let view_before = app.view();
                let processed_at_ms = Instant::now().as_millis() as u32;
                KEY_EVENTS_PROCESSED.fetch_add(1, Ordering::Relaxed);
                if event.gesture == Gesture::Pressed {
                    MAX_KEY_PRESS_AGE_MS
                        .fetch_max(processed_at_ms.wrapping_sub(event.at_ms), Ordering::Relaxed);
                }
                let effect = app.handle_event_at(event, processed_at_ms);
                defmt::debug!(
                    "key: key={} gesture={} held={} ms at={} view={}=>{}",
                    event.key as u8,
                    event.gesture as u8,
                    event.held_ms,
                    event.at_ms,
                    view_before as u8,
                    app.view() as u8
                );

                dispatch_effect(effect, rtc.as_ref());
                let feedback_requested = app.take_feedback_request();
                if feedback_requested && app.persistent_data().settings.sound_enabled {
                    try_play(BuzzerCue::Feedback);
                }
                render_needed |= event.gesture != Gesture::Released;
            }
            Either::Second(()) => {
                // A slow render or bus recovery must not build an unbounded
                // backlog of immediately-ready ticks. Advance from now;
                // animation and game state already use elapsed wall time.
                frame_tick.reset();
                let now_ms = Instant::now().as_millis() as u32;
                let input_drops = DROPPED_KEY_EVENTS.load(Ordering::Relaxed);
                let uart_drops = DROPPED_UART_EVENTS.load(Ordering::Relaxed);
                if input_drops != reported_input_drops {
                    defmt::warn!("button event queue drops: {}", input_drops);
                    reported_input_drops = input_drops;
                }
                if uart_drops != reported_uart_drops {
                    defmt::warn!("UART observer queue drops: {}", uart_drops);
                    reported_uart_drops = uart_drops;
                }

                if deadline_reached(now_ms, next_rtc_read_ms) {
                    if !app.clock_editing() {
                        app.set_clock_snapshot(rtc.as_ref().and_then(|clock| clock.now().ok()));
                    }
                    next_rtc_read_ms = now_ms.wrapping_add(250);
                }
                app.set_system_stats(runtime_stats());
                let held_keys = HELD_KEYS.load(Ordering::Relaxed);
                if app.tick(now_ms, held_keys) == RenderSchedule::Render {
                    render_needed = true;
                }
                loop {
                    let effect = app.take_pending_effect();
                    if effect == AppEffect::None {
                        break;
                    }
                    dispatch_effect(effect, rtc.as_ref());
                }
            }
        }

        let now_ms = Instant::now().as_millis() as u32;
        let wanted_contrast = app.persistent_data().settings.brightness.contrast();
        if panel_online && applied_contrast != Some(wanted_contrast) {
            match display.set_contrast(wanted_contrast).await {
                Ok(_) => applied_contrast = Some(wanted_contrast),
                Err(_) => {
                    OLED_ERRORS.fetch_add(1, Ordering::Relaxed);
                    panel_online = false;
                    retry_at_ms = now_ms.wrapping_add(retry_backoff_ms);
                    display.recover_bus();
                    defmt::warn!("OLED contrast transfer failed; entering recovery backoff");
                }
            }
        }

        let retry_due = !panel_online && deadline_reached(now_ms, retry_at_ms);
        if render_needed {
            let render_started = Instant::now();
            renderer.draw(scene, &app, now_ms);
            display.stage_frame(scene);
            MAX_RENDER_TIME_US.fetch_max(
                render_started
                    .elapsed()
                    .as_micros()
                    .min(u64::from(u32::MAX)) as u32,
                Ordering::Relaxed,
            );
            RENDERED_FRAMES.fetch_add(1, Ordering::Relaxed);
        }

        let frame_changed = display.is_dirty();
        if panel_online && render_needed && frame_changed {
            match display.flush().await {
                Ok(report) => {
                    if report.regions != 0 {
                        OLED_TRANSFERS.fetch_add(1, Ordering::Relaxed);
                    }
                }
                Err(_) => {
                    OLED_ERRORS.fetch_add(1, Ordering::Relaxed);
                    panel_online = false;
                    retry_at_ms = now_ms.wrapping_add(retry_backoff_ms);
                    display.recover_bus();
                    defmt::warn!("OLED transfer failed or timed out; entering recovery backoff");
                }
            }
        } else if retry_due {
            match display.reinitialize().await {
                Ok(report) => {
                    if report.regions != 0 {
                        OLED_TRANSFERS.fetch_add(1, Ordering::Relaxed);
                    }
                    panel_online = true;
                    applied_contrast = None;
                    retry_backoff_ms = 50;
                    defmt::info!("OLED link recovered and framebuffer resent");
                }
                Err(_) => {
                    OLED_ERRORS.fetch_add(1, Ordering::Relaxed);
                    retry_at_ms = now_ms.wrapping_add(retry_backoff_ms);
                    retry_backoff_ms = (retry_backoff_ms * 2).min(2_000);
                }
            }
        }
        render_needed = false;
        // An unchanged scene skips DMA, and an overdue ticker is also Ready.
        // Explicitly yield even on that path so the physical button sampler,
        // buzzer and UART can run between frames or queued input events.
        yield_now().await;
    }
}

fn dispatch_effect(effect: AppEffect, rtc: Option<&RtcClock>) {
    match effect {
        AppEffect::None => {}
        AppEffect::Persist(snapshot) => SAVE_REQUEST.signal(snapshot),
        AppEffect::SetClock(value) => {
            if rtc.is_none_or(|clock| clock.set(value).is_err()) {
                defmt::warn!("RTC update failed");
            }
        }
        AppEffect::Tone {
            frequency_hz,
            duration_ms,
        } => {
            try_play(BuzzerCue::Tone {
                frequency_hz,
                duration_ms,
            });
        }
    }
}

fn runtime_stats() -> SystemStats {
    SystemStats {
        input_drops: DROPPED_KEY_EVENTS.load(Ordering::Relaxed),
        uart_drops: DROPPED_UART_EVENTS.load(Ordering::Relaxed),
        oled_errors: OLED_ERRORS.load(Ordering::Relaxed),
        uart_errors: UART_ERRORS.load(Ordering::Relaxed),
        oled_transfers: OLED_TRANSFERS.load(Ordering::Relaxed),
        storage_errors: STORAGE_ERRORS.load(Ordering::Relaxed),
    }
}

const fn deadline_reached(now: u32, deadline: u32) -> bool {
    now.wrapping_sub(deadline) < 0x8000_0000
}
