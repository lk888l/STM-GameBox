//! Long-lived hardware owner tasks.

use core::sync::atomic::Ordering;

use embassy_stm32::{
    flash::{Blocking, Flash},
    gpio::{Input, Output},
};
use embassy_time::{Duration, Instant, Ticker, Timer};
use gamebox_core::{ButtonBank, Gesture, Key};

use crate::{
    platform::storage::SettingsJournal,
    services::{BUZZER_CUES, BuzzerCue, HELD_KEYS, SAVE_REQUEST, publish_key_event},
};

/// Sample all active-low buttons at 1 kHz and emit debounced gestures.
#[embassy_executor::task]
pub async fn button_task(inputs: [Input<'static>; 8]) {
    let mut bank = ButtonBank::default();
    let mut ticker = Ticker::every(Duration::from_millis(1));
    loop {
        ticker.next().await;
        let now_ms = Instant::now().as_millis() as u32;
        let pressed = core::array::from_fn(|index| inputs[index].is_low());
        bank.update(now_ms, pressed, publish_key_event);
        HELD_KEYS.store(bank.stable_mask(), Ordering::Relaxed);
    }
}

/// Own PA12 and serialize all non-blocking sound patterns.
#[embassy_executor::task]
pub async fn buzzer_task(mut buzzer: Output<'static>) {
    loop {
        match BUZZER_CUES.receive().await {
            BuzzerCue::Navigate => tone(&mut buzzer, 2_200, 14).await,
            BuzzerCue::Confirm => {
                tone(&mut buzzer, 1_800, 22).await;
                Timer::after_millis(12).await;
                tone(&mut buzzer, 2_600, 30).await;
            }
            BuzzerCue::Alert => {
                for _ in 0..3 {
                    tone(&mut buzzer, 1_350, 110).await;
                    Timer::after_millis(55).await;
                }
            }
        }
        buzzer.set_low();
    }
}

/// Coalesce bursts of setting changes and commit only the latest snapshot.
#[embassy_executor::task]
pub async fn storage_task(mut flash: Flash<'static, Blocking>, mut journal: SettingsJournal) {
    loop {
        let mut latest = SAVE_REQUEST.wait().await;
        Timer::after_millis(750).await;
        while let Some(newer) = SAVE_REQUEST.try_take() {
            latest = newer;
        }
        if journal.commit(&mut flash, latest).is_err() {
            defmt::error!("settings Flash commit failed; previous page is retained");
        } else {
            defmt::debug!("settings journal committed");
        }
    }
}

async fn tone(buzzer: &mut Output<'_>, frequency_hz: u32, duration_ms: u32) {
    let half_period_us = (500_000 / frequency_hz).max(1);
    let half_cycles = duration_ms.saturating_mul(1_000) / half_period_us;
    for _ in 0..half_cycles {
        buzzer.toggle();
        Timer::after_micros(u64::from(half_period_us)).await;
    }
    buzzer.set_low();
}

/// Whether a gesture should produce ordinary UI feedback.
#[must_use]
pub const fn gesture_cue(key: Key, gesture: Gesture) -> Option<BuzzerCue> {
    match (key, gesture) {
        (Key::Up | Key::Down | Key::Left | Key::Right, Gesture::Pressed) => {
            Some(BuzzerCue::Navigate)
        }
        (Key::Jump | Key::Function, Gesture::Pressed) => Some(BuzzerCue::Confirm),
        (_, Gesture::Click | Gesture::DoubleClick | Gesture::LongPress) => Some(BuzzerCue::Confirm),
        (_, Gesture::Pressed | Gesture::Released | Gesture::Repeat) => None,
    }
}
