//! Side-scrolling dinosaur game model.

use super::{GamePhase, deadline_reached, random::Random};

const JUMP_ARC: [u8; 15] = [0, 5, 10, 14, 18, 21, 23, 24, 23, 21, 18, 14, 10, 5, 0];
const STEP_PERIOD_MS: u32 = 45;
const DINO_WIDTH: i16 = 10;
const DINO_HEIGHT: i16 = 12;

/// Maximum simultaneous obstacles.
pub const OBSTACLE_COUNT: usize = 3;
/// Fixed dinosaur horizontal location.
pub const DINO_X: i16 = 12;
/// Ground baseline used by model and renderer.
pub const GROUND_Y: i16 = 58;

/// One bounded obstacle slot.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Obstacle {
    x: i16,
    width: u8,
    height: u8,
    active: bool,
}

impl Obstacle {
    /// Horizontal position.
    #[must_use]
    pub const fn x(self) -> i16 {
        self.x
    }

    /// Width in pixels.
    #[must_use]
    pub const fn width(self) -> u8 {
        self.width
    }

    /// Height in pixels.
    #[must_use]
    pub const fn height(self) -> u8 {
        self.height
    }

    /// Whether this slot is visible and collidable.
    #[must_use]
    pub const fn is_active(self) -> bool {
        self.active
    }
}

/// Complete deterministic dinosaur simulation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Dino {
    random: Random,
    obstacles: [Obstacle; OBSTACLE_COUNT],
    phase: GamePhase,
    next_step_ms: u32,
    spawn_steps: u16,
    score: u16,
    jump_frame: u8,
}

impl Dino {
    /// Create a ready game.
    #[must_use]
    pub const fn new(seed: u32) -> Self {
        Self {
            random: Random::new(seed),
            obstacles: [Obstacle {
                x: 0,
                width: 0,
                height: 0,
                active: false,
            }; OBSTACLE_COUNT],
            phase: GamePhase::Ready,
            next_step_ms: 0,
            spawn_steps: 18,
            score: 0,
            jump_frame: 0,
        }
    }

    /// Reset the complete run with a new seed.
    pub fn reset(&mut self, seed: u32) {
        *self = Self::new(seed);
    }

    /// Start a ready game.
    pub fn start(&mut self) {
        if self.phase == GamePhase::Ready {
            self.phase = GamePhase::Playing;
            self.next_step_ms = 0;
        }
    }

    /// Begin a jump when currently grounded.
    pub fn jump(&mut self) {
        if self.phase == GamePhase::Playing && self.jump_frame == 0 {
            self.jump_frame = 1;
        }
    }

    /// Current lifecycle state.
    #[must_use]
    pub const fn phase(&self) -> GamePhase {
        self.phase
    }

    /// Number of obstacles passed.
    #[must_use]
    pub const fn score(&self) -> u16 {
        self.score
    }

    /// Vertical displacement along the fixed jump arc.
    #[must_use]
    pub const fn jump_height(&self) -> u8 {
        JUMP_ARC[self.jump_frame as usize]
    }

    /// Bounded obstacle slots for rendering.
    #[must_use]
    pub const fn obstacles(&self) -> &[Obstacle; OBSTACLE_COUNT] {
        &self.obstacles
    }

    /// Advance through elapsed fixed simulation steps with bounded catch-up.
    pub fn update(&mut self, now_ms: u32) {
        if self.phase != GamePhase::Playing {
            return;
        }
        if self.next_step_ms == 0 {
            self.next_step_ms = now_ms.wrapping_add(STEP_PERIOD_MS);
            return;
        }
        let mut catch_up = 0;
        while deadline_reached(now_ms, self.next_step_ms)
            && catch_up < 5
            && self.phase == GamePhase::Playing
        {
            self.step();
            self.next_step_ms = self.next_step_ms.wrapping_add(STEP_PERIOD_MS);
            catch_up += 1;
        }
        if catch_up == 5 && deadline_reached(now_ms, self.next_step_ms) {
            self.next_step_ms = now_ms.wrapping_add(STEP_PERIOD_MS);
        }
    }

    fn step(&mut self) {
        if self.jump_frame != 0 {
            self.jump_frame += 1;
            if usize::from(self.jump_frame) >= JUMP_ARC.len() {
                self.jump_frame = 0;
            }
        }

        self.spawn_steps = self.spawn_steps.saturating_sub(1);
        if self.spawn_steps == 0 {
            self.spawn_obstacle();
            let difficulty = (self.score / 10).min(6);
            self.spawn_steps = 19 + self.random.bounded(13) as u16 - difficulty;
        }

        let speed = if self.score < 25 {
            3
        } else if self.score < 70 {
            4
        } else {
            5
        };
        let jump_height = i16::from(self.jump_height());
        for obstacle in &mut self.obstacles {
            if !obstacle.active {
                continue;
            }
            obstacle.x -= speed;
            if Self::collides(jump_height, *obstacle) {
                self.phase = GamePhase::GameOver;
                return;
            }
            if obstacle.x + i16::from(obstacle.width) < 0 {
                obstacle.active = false;
                self.score = self.score.saturating_add(1);
            }
        }
    }

    fn spawn_obstacle(&mut self) {
        if let Some(obstacle) = self.obstacles.iter_mut().find(|item| !item.active) {
            let tall = self.random.bounded(3) == 0;
            *obstacle = Obstacle {
                x: 127,
                width: if tall { 6 } else { 9 },
                height: if tall { 15 } else { 9 },
                active: true,
            };
        }
    }

    fn collides(jump_height: i16, obstacle: Obstacle) -> bool {
        let dino_right = DINO_X + DINO_WIDTH - 1;
        let dino_bottom = GROUND_Y - jump_height;
        let dino_top = dino_bottom - DINO_HEIGHT + 1;
        let obstacle_right = obstacle.x + i16::from(obstacle.width) - 1;
        let obstacle_top = GROUND_Y - i16::from(obstacle.height) + 1;
        dino_right >= obstacle.x
            && DINO_X <= obstacle_right
            && dino_bottom >= obstacle_top
            && dino_top <= GROUND_Y
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn jump_advances_without_blocking() {
        let mut dino = Dino::new(123);
        dino.start();
        dino.jump();
        dino.update(1_000);
        dino.update(1_045);
        assert!(dino.jump_height() > 0);
    }

    #[test]
    fn unattended_run_eventually_collides() {
        let mut dino = Dino::new(123);
        dino.start();
        for tick in 0..800 {
            dino.update(1_000 + tick * STEP_PERIOD_MS);
            if dino.phase() == GamePhase::GameOver {
                break;
            }
        }
        assert_eq!(dino.phase(), GamePhase::GameOver);
    }
}
