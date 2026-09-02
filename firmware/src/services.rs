//! Bounded, allocation-free communication between asynchronous tasks.

use core::sync::atomic::{AtomicU8, AtomicU32, Ordering};

use embassy_sync::{
    blocking_mutex::raw::CriticalSectionRawMutex, channel::Channel, signal::Signal,
};
use gamebox_core::{KeyEvent, PersistentData};

/// Input queue depth. A complete simultaneous eight-key transition fits twice.
pub const KEY_EVENT_CAPACITY: usize = 32;

/// Short sound effects understood by the buzzer owner task.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BuzzerCue {
    /// Quiet, short feedback for navigation.
    Navigate,
    /// Positive feedback for activation or scoring.
    Confirm,
    /// Attention pattern for a completed countdown.
    Alert,
}

/// Single producer/single consumer input event queue.
pub static KEY_EVENTS: Channel<CriticalSectionRawMutex, KeyEvent, KEY_EVENT_CAPACITY> =
    Channel::new();
/// Latest persistent snapshot; a newer request replaces an older pending one.
pub static SAVE_REQUEST: Signal<CriticalSectionRawMutex, PersistentData> = Signal::new();
/// Small non-critical sound queue.
pub static BUZZER_CUES: Channel<CriticalSectionRawMutex, BuzzerCue, 4> = Channel::new();
/// Debounced held-key bitmask for diagnostics and future chord handling.
pub static HELD_KEYS: AtomicU8 = AtomicU8::new(0);
/// Number of input events dropped because the consumer was not keeping up.
pub static DROPPED_KEY_EVENTS: AtomicU32 = AtomicU32::new(0);

/// Publish one key event without ever blocking the 1 kHz sampler.
pub fn publish_key_event(event: KeyEvent) {
    if KEY_EVENTS.try_send(event).is_err() {
        DROPPED_KEY_EVENTS.fetch_add(1, Ordering::Relaxed);
    }
}

/// Request a sound without delaying application logic.
pub fn try_play(cue: BuzzerCue) {
    let _ = BUZZER_CUES.try_send(cue);
}
