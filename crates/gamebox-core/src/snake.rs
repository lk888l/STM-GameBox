//! Deterministic, allocation-free Snake game engine.

use crate::games::{GamePhase, deadline_reached};

/// Number of horizontal cells in the playfield.
pub const GRID_WIDTH: u8 = 30;
/// Number of vertical cells in the playfield.
pub const GRID_HEIGHT: u8 = 12;
/// Maximum retained body length.
pub const MAX_BODY: usize = 96;

/// Integer grid coordinate.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Cell {
    /// Horizontal cell.
    pub x: u8,
    /// Vertical cell.
    pub y: u8,
}

impl Cell {
    /// Construct a coordinate.
    #[must_use]
    pub const fn new(x: u8, y: u8) -> Self {
        Self { x, y }
    }
}

/// Movement direction.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Direction {
    /// Negative vertical direction.
    Up,
    /// Positive vertical direction.
    Down,
    /// Negative horizontal direction.
    Left,
    /// Positive horizontal direction.
    Right,
}

impl Direction {
    const fn is_opposite(self, other: Self) -> bool {
        matches!(
            (self, other),
            (Self::Up, Self::Down)
                | (Self::Down, Self::Up)
                | (Self::Left, Self::Right)
                | (Self::Right, Self::Left)
        )
    }
}

/// Result of one simulation step.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StepResult {
    /// Normal movement.
    Moved,
    /// Food consumed and body extended.
    Ate,
    /// Wall or self collision ended the run.
    GameOver,
}

/// Complete Snake simulation state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Snake {
    body: [Cell; MAX_BODY],
    length: u8,
    direction: Direction,
    queued_direction: Direction,
    food: Cell,
    rng: u32,
    score: u16,
    phase: GamePhase,
    next_step_ms: u32,
}

impl Snake {
    /// Create a ready game with a non-cryptographic entropy seed.
    #[must_use]
    pub fn new(seed: u32) -> Self {
        let mut body = [Cell::default(); MAX_BODY];
        let center_x = GRID_WIDTH / 2;
        let center_y = GRID_HEIGHT / 2;
        body[0] = Cell::new(center_x, center_y);
        body[1] = Cell::new(center_x - 1, center_y);
        body[2] = Cell::new(center_x - 2, center_y);
        let mut snake = Self {
            body,
            length: 3,
            direction: Direction::Right,
            queued_direction: Direction::Right,
            food: Cell::default(),
            rng: seed.max(1),
            score: 0,
            phase: GamePhase::Ready,
            next_step_ms: 0,
        };
        snake.place_food();
        snake
    }

    /// Reset while incorporating fresh entropy.
    pub fn reset(&mut self, seed: u32) {
        *self = Self::new(seed);
    }

    /// Start a ready run.
    pub fn start(&mut self) {
        if self.phase == GamePhase::Ready {
            self.phase = GamePhase::Playing;
            self.next_step_ms = 0;
        }
    }

    /// Queue a direction for the next step, rejecting direct reversal.
    pub fn steer(&mut self, direction: Direction) {
        if !direction.is_opposite(self.direction) {
            self.queued_direction = direction;
        }
    }

    /// Advance through elapsed fixed simulation steps with bounded catch-up.
    pub fn update(&mut self, now_ms: u32) {
        if self.phase != GamePhase::Playing {
            return;
        }
        if self.next_step_ms == 0 {
            self.next_step_ms = now_ms.wrapping_add(u32::from(self.step_interval_ms()));
            return;
        }
        let mut catch_up = 0;
        while deadline_reached(now_ms, self.next_step_ms)
            && catch_up < 4
            && self.phase == GamePhase::Playing
        {
            self.step();
            self.next_step_ms = self
                .next_step_ms
                .wrapping_add(u32::from(self.step_interval_ms()));
            catch_up += 1;
        }
        if catch_up == 4 && deadline_reached(now_ms, self.next_step_ms) {
            self.next_step_ms = now_ms.wrapping_add(u32::from(self.step_interval_ms()));
        }
    }

    /// Advance the simulation by one cell.
    pub fn step(&mut self) -> StepResult {
        if self.phase == GamePhase::GameOver {
            return StepResult::GameOver;
        }
        self.direction = self.queued_direction;
        let head = self.body[0];
        let next = match self.direction {
            Direction::Up if head.y > 0 => Cell::new(head.x, head.y - 1),
            Direction::Down if head.y + 1 < GRID_HEIGHT => Cell::new(head.x, head.y + 1),
            Direction::Left if head.x > 0 => Cell::new(head.x - 1, head.y),
            Direction::Right if head.x + 1 < GRID_WIDTH => Cell::new(head.x + 1, head.y),
            _ => {
                self.phase = GamePhase::GameOver;
                return StepResult::GameOver;
            }
        };

        let ate = next == self.food;
        let collision_len = usize::from(self.length).saturating_sub(usize::from(!ate));
        if self.body[..collision_len].contains(&next) {
            self.phase = GamePhase::GameOver;
            return StepResult::GameOver;
        }

        let old_len = usize::from(self.length);
        if ate && old_len < MAX_BODY {
            self.length += 1;
        }
        let new_len = usize::from(self.length);
        self.body.copy_within(0..new_len.saturating_sub(1), 1);
        self.body[0] = next;
        if ate {
            self.score = self.score.saturating_add(1);
            self.place_food();
            StepResult::Ate
        } else {
            StepResult::Moved
        }
    }

    /// Current lifecycle state.
    #[must_use]
    pub const fn phase(&self) -> GamePhase {
        self.phase
    }

    /// Body cells ordered from head to tail.
    #[must_use]
    pub fn body(&self) -> &[Cell] {
        &self.body[..usize::from(self.length)]
    }

    /// Food coordinate.
    #[must_use]
    pub const fn food(&self) -> Cell {
        self.food
    }

    /// Food count for this run.
    #[must_use]
    pub const fn score(&self) -> u16 {
        self.score
    }

    /// Whether a collision has ended the run.
    #[must_use]
    pub const fn is_game_over(&self) -> bool {
        matches!(self.phase, GamePhase::GameOver)
    }

    /// Recommended simulation interval, decreasing with score.
    #[must_use]
    pub const fn step_interval_ms(&self) -> u16 {
        let reduction = self.score.saturating_mul(3);
        if reduction < 80 { 150 - reduction } else { 70 }
    }

    fn place_food(&mut self) {
        let cell_count = u32::from(GRID_WIDTH) * u32::from(GRID_HEIGHT);
        let mut candidate = self.random() % cell_count;
        for _ in 0..cell_count {
            let cell = Cell::new(
                (candidate % u32::from(GRID_WIDTH)) as u8,
                (candidate / u32::from(GRID_WIDTH)) as u8,
            );
            if !self.body().contains(&cell) {
                self.food = cell;
                return;
            }
            candidate = (candidate + 1) % cell_count;
        }
    }

    fn random(&mut self) -> u32 {
        let mut value = self.rng;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        self.rng = value;
        value
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn direct_reverse_is_rejected() {
        let mut snake = Snake::new(1);
        snake.start();
        snake.steer(Direction::Left);
        assert_eq!(snake.step(), StepResult::Moved);
        assert_eq!(
            snake.body()[0],
            Cell::new(GRID_WIDTH / 2 + 1, GRID_HEIGHT / 2)
        );
    }

    #[test]
    fn wall_collision_is_terminal() {
        let mut snake = Snake::new(1);
        snake.start();
        for _ in 0..GRID_WIDTH {
            if snake.step() == StepResult::GameOver {
                break;
            }
        }
        assert_eq!(snake.phase(), GamePhase::GameOver);
    }

    #[test]
    fn food_never_spawns_inside_body() {
        for seed in 1..64 {
            let snake = Snake::new(seed);
            assert!(!snake.body().contains(&snake.food()));
        }
    }
}
