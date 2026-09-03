//! Fixed-capacity side-scrolling air-raid game model.

use super::{GamePhase, deadline_reached, random::Random};

const STEP_PERIOD_MS: u32 = 33;
const PLAYER_X: i16 = 5;
const PLAYER_WIDTH: i16 = 12;
const PLAYER_HEIGHT: i16 = 7;
const ENEMY_WIDTH: i16 = 9;
const ENEMY_HEIGHT: i16 = 7;

/// Maximum bullets simultaneously in flight.
pub const BULLET_COUNT: usize = 3;
/// Maximum enemies simultaneously active.
pub const ENEMY_COUNT: usize = 3;

/// A compact moving game entity.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Entity {
    x: i16,
    y: i16,
    active: bool,
}

impl Entity {
    /// Horizontal position.
    #[must_use]
    pub const fn x(self) -> i16 {
        self.x
    }

    /// Vertical position.
    #[must_use]
    pub const fn y(self) -> i16 {
        self.y
    }

    /// Whether the slot participates in the simulation.
    #[must_use]
    pub const fn is_active(self) -> bool {
        self.active
    }
}

/// Complete air-raid simulation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AirRaid {
    random: Random,
    bullets: [Entity; BULLET_COUNT],
    enemies: [Entity; ENEMY_COUNT],
    phase: GamePhase,
    next_step_ms: u32,
    spawn_steps: u16,
    score: u16,
    player_y: i16,
    vertical: i8,
    lives: u8,
}

impl AirRaid {
    /// Create a ready game.
    #[must_use]
    pub const fn new(seed: u32) -> Self {
        Self {
            random: Random::new(seed),
            bullets: [Entity {
                x: 0,
                y: 0,
                active: false,
            }; BULLET_COUNT],
            enemies: [Entity {
                x: 0,
                y: 0,
                active: false,
            }; ENEMY_COUNT],
            phase: GamePhase::Ready,
            next_step_ms: 0,
            spawn_steps: 10,
            score: 0,
            player_y: 32,
            vertical: 0,
            lives: 3,
        }
    }

    /// Reset all slots and reseed gameplay randomness.
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

    /// Set continuous vertical movement to -1, 0, or 1.
    pub fn set_vertical(&mut self, direction: i8) {
        self.vertical = direction.signum();
    }

    /// Launch one bullet if a slot is available.
    #[must_use]
    pub fn fire(&mut self) -> bool {
        if self.phase != GamePhase::Playing {
            return false;
        }
        if let Some(bullet) = self.bullets.iter_mut().find(|item| !item.active) {
            *bullet = Entity {
                x: PLAYER_X + PLAYER_WIDTH,
                y: self.player_y + 3,
                active: true,
            };
            true
        } else {
            false
        }
    }

    /// Current lifecycle state.
    #[must_use]
    pub const fn phase(&self) -> GamePhase {
        self.phase
    }

    /// Destroyed enemy count.
    #[must_use]
    pub const fn score(&self) -> u16 {
        self.score
    }

    /// Remaining lives.
    #[must_use]
    pub const fn lives(&self) -> u8 {
        self.lives
    }

    /// Player vertical position.
    #[must_use]
    pub const fn player_y(&self) -> i16 {
        self.player_y
    }

    /// Bullet slots for rendering.
    #[must_use]
    pub const fn bullets(&self) -> &[Entity; BULLET_COUNT] {
        &self.bullets
    }

    /// Enemy slots for rendering.
    #[must_use]
    pub const fn enemies(&self) -> &[Entity; ENEMY_COUNT] {
        &self.enemies
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
        self.player_y = (self.player_y + i16::from(self.vertical) * 2).clamp(12, 56);

        self.spawn_steps = self.spawn_steps.saturating_sub(1);
        if self.spawn_steps == 0 {
            self.spawn_enemy();
            let difficulty = (self.score / 8).min(5);
            self.spawn_steps = 14 + self.random.bounded(11) as u16 - difficulty;
        }

        for bullet in &mut self.bullets {
            if bullet.active {
                bullet.x += 4;
                if bullet.x >= 128 {
                    bullet.active = false;
                }
            }
        }

        let enemy_speed = if self.score < 20 { 2 } else { 3 };
        for enemy_index in 0..self.enemies.len() {
            if !self.enemies[enemy_index].active {
                continue;
            }
            self.enemies[enemy_index].x -= enemy_speed;
            let mut destroyed = false;
            for bullet in &mut self.bullets {
                if intersects(*bullet, self.enemies[enemy_index]) {
                    bullet.active = false;
                    self.enemies[enemy_index].active = false;
                    self.score = self.score.saturating_add(1);
                    destroyed = true;
                    break;
                }
            }
            if destroyed {
                continue;
            }
            let enemy = self.enemies[enemy_index];
            if overlaps(
                (PLAYER_X, self.player_y, PLAYER_WIDTH, PLAYER_HEIGHT),
                (enemy.x, enemy.y, ENEMY_WIDTH, ENEMY_HEIGHT),
            ) || enemy.x + ENEMY_WIDTH < 0
            {
                self.enemies[enemy_index].active = false;
                self.lose_life();
                if self.phase == GamePhase::GameOver {
                    return;
                }
            }
        }
    }

    fn spawn_enemy(&mut self) {
        if let Some(enemy) = self.enemies.iter_mut().find(|item| !item.active) {
            *enemy = Entity {
                x: 127,
                y: 13 + self.random.bounded(43) as i16,
                active: true,
            };
        }
    }

    fn lose_life(&mut self) {
        self.lives = self.lives.saturating_sub(1);
        if self.lives == 0 {
            self.phase = GamePhase::GameOver;
        }
    }
}

const fn overlaps(a: (i16, i16, i16, i16), b: (i16, i16, i16, i16)) -> bool {
    let (left_a, top_a, width_a, height_a) = a;
    let (left_b, top_b, width_b, height_b) = b;
    left_a < left_b + width_b
        && left_a + width_a > left_b
        && top_a < top_b + height_b
        && top_a + height_a > top_b
}

const fn intersects(bullet: Entity, enemy: Entity) -> bool {
    bullet.active
        && enemy.active
        && overlaps(
            (bullet.x, bullet.y, 3, 2),
            (enemy.x, enemy.y, ENEMY_WIDTH, ENEMY_HEIGHT),
        )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn enforces_three_bullet_capacity_and_moves_continuously() {
        let mut air = AirRaid::new(456);
        air.start();
        assert!(air.fire());
        assert!(air.fire());
        assert!(air.fire());
        assert!(!air.fire());
        let x = air.bullets()[0].x();
        air.update(1_000);
        air.update(1_033);
        assert!(air.bullets()[0].x() > x);
        let y = air.player_y();
        air.set_vertical(-1);
        air.update(1_066);
        assert!(air.player_y() < y);
    }
}
