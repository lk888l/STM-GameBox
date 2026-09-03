//! Small deterministic PRNG used only for gameplay variety.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Random {
    state: u32,
}

impl Random {
    pub(crate) const FALLBACK_SEED: u32 = 0xa341_316c;

    pub(crate) const fn new(seed: u32) -> Self {
        Self {
            state: if seed == 0 { Self::FALLBACK_SEED } else { seed },
        }
    }

    pub(crate) fn next(&mut self) -> u32 {
        let mut value = self.state;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        self.state = value;
        value
    }

    pub(crate) fn bounded(&mut self, upper_bound: u32) -> u32 {
        if upper_bound == 0 {
            0
        } else {
            self.next() % upper_bound
        }
    }
}
