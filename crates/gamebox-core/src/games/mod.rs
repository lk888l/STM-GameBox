//! Allocation-free, deadline-driven game models.

mod air_raid;
mod dino;
mod pong;
mod random;
mod tetris;

pub use air_raid::{AirRaid, Entity as AirRaidEntity};
pub use dino::{Dino, Obstacle as DinoObstacle};
pub use pong::Pong;
pub use tetris::Tetris;

/// Shared lifecycle state for every full-screen game.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum GamePhase {
    /// Waiting for the player to begin.
    #[default]
    Ready,
    /// Simulation is advancing.
    Playing,
    /// The run has ended and can be restarted.
    GameOver,
}

pub(crate) const fn deadline_reached(now_ms: u32, deadline_ms: u32) -> bool {
    now_ms.wrapping_sub(deadline_ms) < 0x8000_0000
}
