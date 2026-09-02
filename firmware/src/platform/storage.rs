//! Power-loss-tolerant two-page Flash journal.

use embassy_stm32::flash::{Blocking, Error as FlashError, Flash};
use gamebox_core::{
    PersistentData,
    storage::{RECORD_SIZE, StoredRecord, decode, encode, generation_is_newer},
};

/// STM32F103C8 medium-density Flash erase page size.
pub const PAGE_SIZE: u32 = 1_024;
/// First settings page; excluded from the linker region by `memory.x`.
pub const PAGE_A_OFFSET: u32 = 62 * PAGE_SIZE;
/// Second settings page; excluded from the linker region by `memory.x`.
pub const PAGE_B_OFFSET: u32 = 63 * PAGE_SIZE;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Page {
    A,
    B,
}

impl Page {
    const fn offset(self) -> u32 {
        match self {
            Self::A => PAGE_A_OFFSET,
            Self::B => PAGE_B_OFFSET,
        }
    }

    const fn other(self) -> Self {
        match self {
            Self::A => Self::B,
            Self::B => Self::A,
        }
    }
}

/// Journal state retained by the one Flash owner task.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SettingsJournal {
    active: Option<Page>,
    generation: u32,
}

/// A commit did not reach a verified durable state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CommitError {
    /// STM32 Flash controller rejected an operation.
    Flash(FlashError),
    /// Read-back failed schema or CRC validation.
    Verification,
}

impl From<FlashError> for CommitError {
    fn from(error: FlashError) -> Self {
        Self::Flash(error)
    }
}

impl SettingsJournal {
    /// Read both journal heads, choose the newest valid one, and recover defaults
    /// if both pages are erased or corrupt.
    pub fn load(flash: &mut Flash<'_, Blocking>) -> (Self, PersistentData) {
        let first = read_record(flash, Page::A).ok();
        let second = read_record(flash, Page::B).ok();
        match (first, second) {
            (Some(first), Some(second)) => {
                if generation_is_newer(second.generation, first.generation) {
                    (
                        Self {
                            active: Some(Page::B),
                            generation: second.generation,
                        },
                        second.data,
                    )
                } else {
                    (
                        Self {
                            active: Some(Page::A),
                            generation: first.generation,
                        },
                        first.data,
                    )
                }
            }
            (Some(record), None) => (
                Self {
                    active: Some(Page::A),
                    generation: record.generation,
                },
                record.data,
            ),
            (None, Some(record)) => (
                Self {
                    active: Some(Page::B),
                    generation: record.generation,
                },
                record.data,
            ),
            (None, None) => (
                Self {
                    active: None,
                    generation: 0,
                },
                PersistentData::default(),
            ),
        }
    }

    /// Commit to the inactive page, verify it, and only then make it active.
    /// The previous page remains a valid recovery point throughout the update.
    pub fn commit(
        &mut self,
        flash: &mut Flash<'_, Blocking>,
        data: PersistentData,
    ) -> Result<(), CommitError> {
        let data = PersistentData {
            settings: data.settings.normalized(),
            snake_high_score: data.snake_high_score,
        };
        let target = self.active.map_or(Page::A, Page::other);
        let generation = self.generation.wrapping_add(1);
        let bytes = encode(data, generation);
        let offset = target.offset();

        flash.blocking_erase(offset, offset + PAGE_SIZE)?;
        flash.blocking_write(offset, &bytes)?;
        let verified = read_record(flash, target).map_err(|_| CommitError::Verification)?;
        if verified.generation != generation || verified.data != data {
            return Err(CommitError::Verification);
        }

        self.active = Some(target);
        self.generation = generation;
        Ok(())
    }
}

fn read_record(flash: &mut Flash<'_, Blocking>, page: Page) -> Result<StoredRecord, CommitError> {
    let mut bytes = [0_u8; RECORD_SIZE];
    flash.blocking_read(page.offset(), &mut bytes)?;
    decode(&bytes).map_err(|_| CommitError::Verification)
}
