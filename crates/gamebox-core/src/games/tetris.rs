//! Compact 10×15 tetromino game model.

use super::{GamePhase, deadline_reached, random::Random};

/// Board column count.
pub const COLUMNS: u8 = 10;
/// Board row count.
pub const ROWS: u8 = 15;
/// Number of tetromino shapes.
pub const PIECE_COUNT: u8 = 7;

// Four 4×4 row-major masks per tetromino: I, O, T, S, Z, J, L.
const MASKS: [[u16; 4]; PIECE_COUNT as usize] = [
    [0x00f0, 0x2222, 0x00f0, 0x2222],
    [0x0066, 0x0066, 0x0066, 0x0066],
    [0x0072, 0x0262, 0x0270, 0x0232],
    [0x0036, 0x0462, 0x0036, 0x0462],
    [0x0063, 0x0264, 0x0063, 0x0264],
    [0x0071, 0x0226, 0x0470, 0x0322],
    [0x0074, 0x0622, 0x0170, 0x0223],
];

/// Complete deterministic Tetris simulation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Tetris {
    random: Random,
    rows: [u16; ROWS as usize],
    phase: GamePhase,
    next_step_ms: u32,
    score: u32,
    lines: u16,
    piece_x: i8,
    piece_y: i8,
    piece: u8,
    next_piece: u8,
    rotation: u8,
}

impl Tetris {
    /// Create a ready board and choose the first pieces.
    #[must_use]
    pub fn new(seed: u32) -> Self {
        let mut game = Self {
            random: Random::new(seed),
            rows: [0; ROWS as usize],
            phase: GamePhase::Ready,
            next_step_ms: 0,
            score: 0,
            lines: 0,
            piece_x: 3,
            piece_y: -1,
            piece: 0,
            next_piece: 0,
            rotation: 0,
        };
        game.next_piece = game.random.bounded(u32::from(PIECE_COUNT)) as u8;
        game.spawn_piece();
        game
    }

    /// Reset the board and reseed the seven-piece stream.
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

    /// Test one cell in a tetromino rotation.
    #[must_use]
    pub const fn piece_cell(piece: u8, rotation: u8, x: u8, y: u8) -> bool {
        if piece >= PIECE_COUNT || x >= 4 || y >= 4 {
            return false;
        }
        let bit = y * 4 + x;
        MASKS[piece as usize][(rotation & 3) as usize] & (1_u16 << bit) != 0
    }

    /// Return whether a board cell is settled.
    #[must_use]
    pub const fn settled(&self, x: u8, y: u8) -> bool {
        x < COLUMNS && y < ROWS && self.rows[y as usize] & (1_u16 << x) != 0
    }

    /// Return whether the active piece occupies a board cell.
    #[must_use]
    pub fn active(&self, x: u8, y: u8) -> bool {
        for local_y in 0..4 {
            for local_x in 0..4 {
                if Self::piece_cell(self.piece, self.rotation, local_x, local_y)
                    && i16::from(self.piece_x) + i16::from(local_x) == i16::from(x)
                    && i16::from(self.piece_y) + i16::from(local_y) == i16::from(y)
                {
                    return true;
                }
            }
        }
        false
    }

    /// Move the active piece one column left or right.
    #[must_use]
    pub fn move_horizontal(&mut self, horizontal: i8) -> bool {
        if self.phase != GamePhase::Playing || horizontal == 0 {
            return false;
        }
        let candidate = self.piece_x + horizontal.signum();
        if !self.fits(candidate, self.piece_y, self.rotation) {
            return false;
        }
        self.piece_x = candidate;
        true
    }

    /// Rotate with a bounded horizontal wall-kick search.
    #[must_use]
    pub fn rotate(&mut self, direction: i8) -> bool {
        if self.phase != GamePhase::Playing || direction == 0 {
            return false;
        }
        let candidate = (self.rotation + if direction > 0 { 1 } else { 3 }) & 3;
        for kick in [0_i8, -1, 1, -2, 2] {
            let candidate_x = self.piece_x + kick;
            if self.fits(candidate_x, self.piece_y, candidate) {
                self.piece_x = candidate_x;
                self.rotation = candidate;
                return true;
            }
        }
        false
    }

    /// Drop one row and award one point when movement succeeds.
    #[must_use]
    pub fn soft_drop(&mut self) -> bool {
        if self.phase != GamePhase::Playing {
            return false;
        }
        let moved = self.step_down();
        if moved {
            self.score = self.score.saturating_add(1);
        }
        moved
    }

    /// Drop until collision, awarding two points per traversed row.
    pub fn hard_drop(&mut self) {
        if self.phase != GamePhase::Playing {
            return;
        }
        while self.step_down() {
            self.score = self.score.saturating_add(2);
        }
    }

    /// Advance gravity with bounded catch-up.
    pub fn update(&mut self, now_ms: u32) {
        if self.phase != GamePhase::Playing {
            return;
        }
        if self.next_step_ms == 0 {
            self.next_step_ms = now_ms.wrapping_add(self.gravity_period_ms());
            return;
        }
        let mut catch_up = 0;
        while deadline_reached(now_ms, self.next_step_ms)
            && catch_up < 3
            && self.phase == GamePhase::Playing
        {
            self.step_down();
            self.next_step_ms = self.next_step_ms.wrapping_add(self.gravity_period_ms());
            catch_up += 1;
        }
        if catch_up == 3 && deadline_reached(now_ms, self.next_step_ms) {
            self.next_step_ms = now_ms.wrapping_add(self.gravity_period_ms());
        }
    }

    /// Current lifecycle state.
    #[must_use]
    pub const fn phase(&self) -> GamePhase {
        self.phase
    }

    /// Accumulated score.
    #[must_use]
    pub const fn score(&self) -> u32 {
        self.score
    }

    /// Cleared line count.
    #[must_use]
    pub const fn lines(&self) -> u16 {
        self.lines
    }

    /// Next tetromino identifier.
    #[must_use]
    pub const fn next_piece(&self) -> u8 {
        self.next_piece
    }

    fn fits(&self, x: i8, y: i8, rotation: u8) -> bool {
        for local_y in 0..4_u8 {
            for local_x in 0..4_u8 {
                if !Self::piece_cell(self.piece, rotation, local_x, local_y) {
                    continue;
                }
                let world_x = i16::from(x) + i16::from(local_x);
                let world_y = i16::from(y) + i16::from(local_y);
                if world_x < 0 || world_x >= i16::from(COLUMNS) || world_y >= i16::from(ROWS) {
                    return false;
                }
                if world_y >= 0 && self.settled(world_x as u8, world_y as u8) {
                    return false;
                }
            }
        }
        true
    }

    fn step_down(&mut self) -> bool {
        let candidate_y = self.piece_y + 1;
        if self.fits(self.piece_x, candidate_y, self.rotation) {
            self.piece_y = candidate_y;
            true
        } else {
            self.lock_piece();
            false
        }
    }

    fn lock_piece(&mut self) {
        for local_y in 0..4_u8 {
            for local_x in 0..4_u8 {
                if !Self::piece_cell(self.piece, self.rotation, local_x, local_y) {
                    continue;
                }
                let world_x = i16::from(self.piece_x) + i16::from(local_x);
                let world_y = i16::from(self.piece_y) + i16::from(local_y);
                if world_y < 0 {
                    self.phase = GamePhase::GameOver;
                    return;
                }
                self.rows[world_y as usize] |= 1_u16 << world_x;
            }
        }
        self.clear_lines();
        self.spawn_piece();
    }

    fn clear_lines(&mut self) {
        const FULL: u16 = (1_u16 << COLUMNS) - 1;
        let mut cleared = 0_u8;
        let mut row = ROWS as usize;
        while row != 0 {
            let index = row - 1;
            if self.rows[index] != FULL {
                row -= 1;
                continue;
            }
            cleared += 1;
            self.rows.copy_within(0..index, 1);
            self.rows[0] = 0;
        }
        self.lines = self.lines.saturating_add(u16::from(cleared));
        self.score = self
            .score
            .saturating_add([0_u32, 100, 300, 500, 800][cleared as usize]);
    }

    fn spawn_piece(&mut self) {
        self.piece = self.next_piece;
        self.next_piece = self.random.bounded(u32::from(PIECE_COUNT)) as u8;
        self.rotation = 0;
        self.piece_x = 3;
        self.piece_y = -1;
        if !self.fits(self.piece_x, self.piece_y, self.rotation) {
            self.phase = GamePhase::GameOver;
        }
    }

    fn gravity_period_ms(&self) -> u32 {
        500 - (u32::from(self.lines / 8).min(9) * 45)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_tetromino_rotation_has_four_cells() {
        for piece in 0..PIECE_COUNT {
            for rotation in 0..4 {
                let mut cells = 0;
                for y in 0..4 {
                    for x in 0..4 {
                        cells += u8::from(Tetris::piece_cell(piece, rotation, x, y));
                    }
                }
                assert_eq!(cells, 4);
            }
        }
    }

    #[test]
    fn movement_rotation_and_stacking_are_bounded() {
        let mut game = Tetris::new(789);
        game.start();
        assert!(game.move_horizontal(-1));
        assert!(game.rotate(1));
        game.hard_drop();
        assert!(game.score() > 0);
        for _ in 0..80 {
            if game.phase() != GamePhase::Playing {
                break;
            }
            game.hard_drop();
        }
        assert_eq!(game.phase(), GamePhase::GameOver);
    }
}
