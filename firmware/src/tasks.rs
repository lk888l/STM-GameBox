//! Long-lived hardware owner tasks.

use core::sync::atomic::Ordering;

use embassy_stm32::{
    flash::{Blocking, Flash},
    gpio::{Input, Output},
    mode::Async,
    usart::UartTx,
};
use embassy_time::{Duration, Instant, Ticker, Timer};
use gamebox_core::{ButtonBank, Gesture, Key, KeyEvent};

use crate::{
    board,
    platform::storage::SettingsJournal,
    services::{
        BUZZER_CUES, BuzzerCue, HELD_KEYS, SAVE_REQUEST, STORAGE_ERRORS, UART_ERRORS, UART_EVENTS,
        publish_key_event,
    },
};

/// Sample all active-low buttons every 5 ms and emit debounced gestures.
#[embassy_executor::task]
pub async fn button_task(inputs: [Input<'static>; 8]) {
    let mut bank = ButtonBank::default();
    let mut ticker = Ticker::every(Duration::from_millis(5));
    loop {
        ticker.next().await;
        let now_ms = Instant::now().as_millis() as u32;
        let pressed = board::sample_buttons(&inputs);
        bank.update(now_ms, pressed, publish_key_event);
        HELD_KEYS.store(bank.stable_mask(), Ordering::Relaxed);
    }
}

/// Own PA12 and serialize all non-blocking sound patterns.
#[embassy_executor::task]
pub async fn buzzer_task(mut buzzer: Output<'static>) {
    loop {
        match BUZZER_CUES.receive().await {
            BuzzerCue::Feedback => tone(&mut buzzer, 1_350, 12).await,
            BuzzerCue::Tone {
                frequency_hz,
                duration_ms,
            } => {
                tone(&mut buzzer, u32::from(frequency_hz), u32::from(duration_ms)).await;
            }
        }
        buzzer.set_low();
    }
}

/// Format observed button events and transmit them with USART1 TX DMA.
#[embassy_executor::task]
pub async fn uart_task(mut tx: UartTx<'static, Async>) {
    if tx
        .write(b"GAMEBOX FW2 UART-TX-DMA READY\r\n")
        .await
        .is_err()
    {
        UART_ERRORS.fetch_add(1, Ordering::Relaxed);
    }
    loop {
        let event = UART_EVENTS.receive().await;
        let line = DiagnosticLine::from_event(event);
        if tx.write(line.as_bytes()).await.is_err() {
            UART_ERRORS.fetch_add(1, Ordering::Relaxed);
        }
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
            STORAGE_ERRORS.fetch_add(1, Ordering::Relaxed);
            defmt::error!("settings Flash commit failed; previous page is retained");
        } else {
            defmt::debug!("settings journal committed");
        }
    }
}

async fn tone(buzzer: &mut Output<'_>, frequency_hz: u32, duration_ms: u32) {
    let half_period_us = (500_000 / frequency_hz.max(1)).max(1);
    let half_cycles = duration_ms.saturating_mul(1_000) / half_period_us;
    for _ in 0..half_cycles {
        buzzer.toggle();
        Timer::after_micros(u64::from(half_period_us)).await;
    }
    buzzer.set_low();
}

struct DiagnosticLine {
    bytes: [u8; 64],
    len: usize,
}

impl DiagnosticLine {
    fn from_event(event: KeyEvent) -> Self {
        let mut line = Self {
            bytes: [0; 64],
            len: 0,
        };
        line.push(b"BTN ");
        line.push_number(event.at_ms);
        line.push(b" ");
        line.push(key_name(event.key));
        line.push(b" ");
        line.push(gesture_name(event.gesture));
        line.push(b" ");
        line.push_number(event.held_ms);
        line.push(b"\r\n");
        line
    }

    fn push(&mut self, value: &[u8]) {
        let available = self.bytes.len() - self.len;
        let count = value.len().min(available);
        self.bytes[self.len..self.len + count].copy_from_slice(&value[..count]);
        self.len += count;
    }

    fn push_number(&mut self, mut value: u32) {
        let mut digits = [0_u8; 10];
        let mut start = digits.len();
        loop {
            start -= 1;
            digits[start] = b'0' + (value % 10) as u8;
            value /= 10;
            if value == 0 {
                break;
            }
        }
        self.push(&digits[start..]);
    }

    fn as_bytes(&self) -> &[u8] {
        &self.bytes[..self.len]
    }
}

const fn key_name(key: Key) -> &'static [u8] {
    match key {
        Key::Up => b"UP",
        Key::Down => b"DOWN",
        Key::Left => b"LEFT",
        Key::Right => b"RIGHT",
        Key::Jump => b"JUMP",
        Key::Function => b"FUNC",
        Key::Enter => b"ENTER",
        Key::Back => b"BACK",
    }
}

const fn gesture_name(gesture: Gesture) -> &'static [u8] {
    match gesture {
        Gesture::Pressed => b"PRESSED",
        Gesture::Released => b"RELEASED",
        Gesture::Click => b"CLICK",
        Gesture::DoubleClick => b"DOUBLE",
        Gesture::LongPress => b"LONG",
        Gesture::Repeat => b"REPEAT",
    }
}
