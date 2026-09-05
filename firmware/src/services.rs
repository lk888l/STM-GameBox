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
/// Completed physical input samples, observable through SWD without logging.
pub static BUTTON_SAMPLES: AtomicU32 = AtomicU32::new(0);
/// Longest interval between physical samples, including debugger halts.
pub static MAX_BUTTON_SCAN_GAP_US: AtomicU32 = AtomicU32::new(0);
/// Semantic events consumed by the UI owner.
pub static KEY_EVENTS_PROCESSED: AtomicU32 = AtomicU32::new(0);
/// Longest UI delivery delay for an immediate debounced press.
pub static MAX_KEY_PRESS_AGE_MS: AtomicU32 = AtomicU32::new(0);
/// Completed scene renders, including unchanged frames that skip the bus.
pub static RENDERED_FRAMES: AtomicU32 = AtomicU32::new(0);
/// Longest synchronous scene render and framebuffer staging time.
pub static MAX_RENDER_TIME_US: AtomicU32 = AtomicU32::new(0);
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
