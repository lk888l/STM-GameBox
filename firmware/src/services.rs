//! Bounded, allocation-free communication between asynchronous tasks.

use core::sync::atomic::{AtomicU8, AtomicU32, Ordering};

use embassy_sync::{
    blocking_mutex::raw::CriticalSectionRawMutex, channel::Channel, signal::Signal,
};
use gamebox_core::{KeyEvent, PersistentData};

/// Main UI input queue depth.
pub const KEY_EVENT_CAPACITY: usize = 32;
/// Independent UART-observer queue depth.
pub const UART_EVENT_CAPACITY: usize = 16;

/// Sound commands understood by the buzzer owner task.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BuzzerCue {
    /// Ordinary UI feedback pulse used by the reference firmware.
    Feedback,
    /// One arbitrary bounded game or piano tone.
    Tone {
        /// Frequency in hertz.
        frequency_hz: u16,
        /// Duration in milliseconds.
        duration_ms: u16,
    },
}

/// UI input queue.
pub static KEY_EVENTS: Channel<CriticalSectionRawMutex, KeyEvent, KEY_EVENT_CAPACITY> =
    Channel::new();
/// UART diagnostics observer queue.
pub static UART_EVENTS: Channel<CriticalSectionRawMutex, KeyEvent, UART_EVENT_CAPACITY> =
    Channel::new();
/// Latest persistent snapshot; a newer request replaces an older pending one.
pub static SAVE_REQUEST: Signal<CriticalSectionRawMutex, PersistentData> = Signal::new();
/// Small non-critical sound queue.
pub static BUZZER_CUES: Channel<CriticalSectionRawMutex, BuzzerCue, 4> = Channel::new();
/// Debounced held-key bitmask.
pub static HELD_KEYS: AtomicU8 = AtomicU8::new(0);
/// Number of overwritten UI input events.
pub static DROPPED_KEY_EVENTS: AtomicU32 = AtomicU32::new(0);
/// Number of overwritten UART observer events.
pub static DROPPED_UART_EVENTS: AtomicU32 = AtomicU32::new(0);
/// UART DMA transfer failures.
pub static UART_ERRORS: AtomicU32 = AtomicU32::new(0);
/// OLED transfer/recovery failures.
pub static OLED_ERRORS: AtomicU32 = AtomicU32::new(0);
/// OLED flushes that transferred at least one region.
pub static OLED_TRANSFERS: AtomicU32 = AtomicU32::new(0);
/// Failed settings journal commits.
pub static STORAGE_ERRORS: AtomicU32 = AtomicU32::new(0);

/// Publish one event to UI and diagnostics without blocking the 5 ms sampler.
/// Full queues discard their oldest entry so the latest physical state wins.
pub fn publish_key_event(event: KeyEvent) {
    if KEY_EVENTS.try_send(event).is_err() {
        let _ = KEY_EVENTS.try_receive();
        let _ = KEY_EVENTS.try_send(event);
        DROPPED_KEY_EVENTS.fetch_add(1, Ordering::Relaxed);
    }
    if UART_EVENTS.try_send(event).is_err() {
        let _ = UART_EVENTS.try_receive();
        let _ = UART_EVENTS.try_send(event);
        DROPPED_UART_EVENTS.fetch_add(1, Ordering::Relaxed);
    }
}

/// Request a sound without delaying application logic.
pub fn try_play(cue: BuzzerCue) {
    let _ = BUZZER_CUES.try_send(cue);
}
