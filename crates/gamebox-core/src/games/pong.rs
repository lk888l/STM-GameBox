//! Two-player Pong model with continuously sampled paddles.

use super::{GamePhase, deadline_reached, random::Random};

const STEP_PERIOD_MS: u32 = 22;
const TOP: i16 = 11;
const BOTTOM: i16 = 62;
const LEFT_PADDLE_X: i16 = 7;
const RIGHT_PADDLE_X: i16 = 120;

/// Complete deterministic two-player Pong simulation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Pong {
    random: Random,
    phase: GamePhase,
    next_step_ms: u32,
    ball_x: i16,
    ball_y: i16,
    velocity_x: i8,
    velocity_y: i8,
    left_y: i16,
    right_y: i16,
    left_direction: i8,
    right_direction: i8,
    left_half_length: u8,
    right_half_length: u8,
    winner: u8,
}

impl Pong {
    /// Create a ready match.
    #[must_use]
    pub fn new(seed: u32) -> Self {
        let mut game = Self {
            random: Random::new(seed),
            phase: GamePhase::Ready,
            next_step_ms: 0,
            ball_x: 64,
            ball_y: 34,
            velocity_x: 1,
            velocity_y: 1,
            left_y: 36,
            right_y: 36,
            left_direction: 0,
            right_direction: 0,
            left_half_length: 8,
            right_half_length: 8,
            winner: 0,
        };
        let direction = if game.random.bounded(2) == 0 { -1 } else { 1 };
        game.reset_ball(direction);
        game
    }

    /// Reset the match with full paddles.
    pub fn reset(&mut self, seed: u32) {
        *self = Self::new(seed);
    }

    /// Start a ready match.
    pub fn start(&mut self) {
        if self.phase == GamePhase::Ready {
            self.phase = GamePhase::Playing;
            self.next_step_ms = 0;
        }
    }

    /// Set left-player movement to -1, 0, or 1.
    pub fn set_left_direction(&mut self, direction: i8) {
        self.left_direction = direction.signum();
    }

    /// Set right-player movement to -1, 0, or 1.
    pub fn set_right_direction(&mut self, direction: i8) {
        self.right_direction = direction.signum();
    }

    /// Current lifecycle state.
    #[must_use]
    pub const fn phase(&self) -> GamePhase {
        self.phase
    }

    /// Ball horizontal position.
    #[must_use]
    pub const fn ball_x(&self) -> i16 {
        self.ball_x
    }

    /// Ball vertical position.
    #[must_use]
    pub const fn ball_y(&self) -> i16 {
        self.ball_y
    }

    /// Left paddle center.
    #[must_use]
    pub const fn left_y(&self) -> i16 {
        self.left_y
    }

    /// Right paddle center.
    #[must_use]
    pub const fn right_y(&self) -> i16 {
        self.right_y
    }

    /// Left paddle half-length.
    #[must_use]
    pub const fn left_half_length(&self) -> u8 {
        self.left_half_length
    }

    /// Right paddle half-length.
    #[must_use]
    pub const fn right_half_length(&self) -> u8 {
        self.right_half_length
    }

    /// Winning player number, or zero before game over.
    #[must_use]
    pub const fn winner(&self) -> u8 {
        self.winner
    }

    /// Advance through elapsed fixed steps with bounded catch-up.
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
            && catch_up < 6
            && self.phase == GamePhase::Playing
        {
            self.step();
            self.next_step_ms = self.next_step_ms.wrapping_add(STEP_PERIOD_MS);
            catch_up += 1;
        }
        if catch_up == 6 && deadline_reached(now_ms, self.next_step_ms) {
            self.next_step_ms = now_ms.wrapping_add(STEP_PERIOD_MS);
        }
    }

    fn reset_ball(&mut self, horizontal_direction: i8) {
        self.ball_x = 64;
        self.ball_y = 23 + self.random.bounded(24) as i16;
        self.velocity_x = if horizontal_direction < 0 { -1 } else { 1 };
        self.velocity_y = if self.random.bounded(2) == 0 { -1 } else { 1 };
    }

    fn miss(&mut self, left_side: bool) {
        let half_length = if left_side {
            &mut self.left_half_length
        } else {
            &mut self.right_half_length
        };
        if *half_length > 2 {
            *half_length -= 1;
            self.reset_ball(if left_side { 1 } else { -1 });
        } else {
            self.winner = if left_side { 2 } else { 1 };
            self.phase = GamePhase::GameOver;
        }
    }

    fn step(&mut self) {
        self.left_y += i16::from(self.left_direction);
        self.right_y += i16::from(self.right_direction);
        self.left_y = self.left_y.clamp(
            TOP + i16::from(self.left_half_length),
            BOTTOM - i16::from(self.left_half_length),
        );
        self.right_y = self.right_y.clamp(
            TOP + i16::from(self.right_half_length),
            BOTTOM - i16::from(self.right_half_length),
        );

        self.ball_x += i16::from(self.velocity_x);
        self.ball_y += i16::from(self.velocity_y);
        if self.ball_y <= TOP + 1 {
            self.ball_y = TOP + 1;
            self.velocity_y = 1;
        } else if self.ball_y >= BOTTOM - 1 {
            self.ball_y = BOTTOM - 1;
            self.velocity_y = -1;
        }

        if self.velocity_x < 0 && self.ball_x <= LEFT_PADDLE_X + 1 {
            let distance = (self.ball_y - self.left_y).unsigned_abs() as u8;
            if self.ball_x >= LEFT_PADDLE_X - 1 && distance <= self.left_half_length {
                self.ball_x = LEFT_PADDLE_X + 2;
                self.velocity_x = 1;
                self.velocity_y = (self.ball_y - self.left_y).signum() as i8;
                if self.velocity_y == 0 {
                    self.velocity_y = 1;
                }
            } else if self.ball_x < 0 {
                self.miss(true);
            }
        } else if self.velocity_x > 0 && self.ball_x >= RIGHT_PADDLE_X - 1 {
            let distance = (self.ball_y - self.right_y).unsigned_abs() as u8;
            if self.ball_x <= RIGHT_PADDLE_X + 1 && distance <= self.right_half_length {
                self.ball_x = RIGHT_PADDLE_X - 2;
                self.velocity_x = -1;
                self.velocity_y = (self.ball_y - self.right_y).signum() as i8;
                if self.velocity_y == 0 {
                    self.velocity_y = 1;
                }
            } else if self.ball_x > 127 {
                self.miss(false);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn paddles_and_ball_advance_from_continuous_input() {
        let mut pong = Pong::new(321);
        pong.start();
        let y = pong.left_y();
        pong.set_left_direction(-1);
        pong.update(1_000);
        pong.update(1_022);
        assert!(pong.left_y() < y);
        assert_ne!(pong.ball_x(), 64);
    }
}
