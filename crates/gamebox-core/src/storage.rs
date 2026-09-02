//! Versioned, checksummed persistent data codec.

use crate::settings::{AnimationSpeed, CursorStyle, Settings};

/// Serialized record size; divisible by the STM32F103 Flash write size.
pub const RECORD_SIZE: usize = 32;
const MAGIC: [u8; 4] = *b"GBX2";
const SCHEMA_VERSION: u8 = 1;
const CRC_OFFSET: usize = 28;

/// Application data retained across power cycles.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct PersistentData {
    /// User-facing settings.
    pub settings: Settings,
    /// Best snake score achieved on this device.
    pub snake_high_score: u16,
}

/// Valid record decoded from one journal page.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct StoredRecord {
    /// Monotonic wrapping journal generation.
    pub generation: u32,
    /// Validated application data.
    pub data: PersistentData,
}

/// Why a candidate Flash record was rejected.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DecodeError {
    /// Record buffer length differs from [`RECORD_SIZE`].
    WrongLength,
    /// Magic marker is absent, normally indicating an erased page.
    BadMagic,
    /// Record belongs to an unsupported future schema.
    UnsupportedSchema,
    /// CRC does not match the payload.
    Corrupt,
}

/// Serialize a record in a deterministic little-endian format.
#[must_use]
pub fn encode(data: PersistentData, generation: u32) -> [u8; RECORD_SIZE] {
    let data = PersistentData {
        settings: data.settings.normalized(),
        snake_high_score: data.snake_high_score,
    };
    let mut bytes = [0_u8; RECORD_SIZE];
    bytes[0..4].copy_from_slice(&MAGIC);
    bytes[4] = SCHEMA_VERSION;
    bytes[5] = u8::from(data.settings.sound_enabled);
    bytes[6] = data.settings.animation_speed as u8;
    bytes[7] = data.settings.cursor_style as u8;
    bytes[8] = data.settings.contrast;
    bytes[10..12].copy_from_slice(&data.settings.standby_refresh_seconds.to_le_bytes());
    bytes[12..14].copy_from_slice(&data.snake_high_score.to_le_bytes());
    bytes[16..20].copy_from_slice(&generation.to_le_bytes());
    let crc = crc32(&bytes[..CRC_OFFSET]);
    bytes[CRC_OFFSET..].copy_from_slice(&crc.to_le_bytes());
    bytes
}

/// Decode and validate one candidate record.
pub fn decode(bytes: &[u8]) -> Result<StoredRecord, DecodeError> {
    if bytes.len() != RECORD_SIZE {
        return Err(DecodeError::WrongLength);
    }
    if bytes[..4] != MAGIC {
        return Err(DecodeError::BadMagic);
    }
    if bytes[4] != SCHEMA_VERSION {
        return Err(DecodeError::UnsupportedSchema);
    }
    let expected = u32::from_le_bytes(
        bytes[CRC_OFFSET..RECORD_SIZE]
            .try_into()
            .map_err(|_| DecodeError::WrongLength)?,
    );
    if crc32(&bytes[..CRC_OFFSET]) != expected {
        return Err(DecodeError::Corrupt);
    }

    let settings = Settings {
        sound_enabled: bytes[5] != 0,
        animation_speed: AnimationSpeed::from_byte(bytes[6]),
        cursor_style: CursorStyle::from_byte(bytes[7]),
        contrast: bytes[8],
        standby_refresh_seconds: u16::from_le_bytes([bytes[10], bytes[11]]),
    }
    .normalized();

    Ok(StoredRecord {
        generation: u32::from_le_bytes([bytes[16], bytes[17], bytes[18], bytes[19]]),
        data: PersistentData {
            settings,
            snake_high_score: u16::from_le_bytes([bytes[12], bytes[13]]),
        },
    })
}

/// Select the newest valid record from two ping-pong pages.
#[must_use]
pub const fn newest(
    first: Option<StoredRecord>,
    second: Option<StoredRecord>,
) -> Option<StoredRecord> {
    match (first, second) {
        (Some(first), Some(second)) => {
            if generation_is_newer(second.generation, first.generation) {
                Some(second)
            } else {
                Some(first)
            }
        }
        (Some(record), None) | (None, Some(record)) => Some(record),
        (None, None) => None,
    }
}

/// Compare wrapping generation numbers using serial-number arithmetic.
#[must_use]
pub const fn generation_is_newer(candidate: u32, reference: u32) -> bool {
    candidate != reference && candidate.wrapping_sub(reference) < 0x8000_0000
}

fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = u32::MAX;
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            let mask = 0_u32.wrapping_sub(crc & 1);
            crc = (crc >> 1) ^ (0xedb8_8320 & mask);
        }
    }
    !crc
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn record_round_trip_is_exact() {
        let data = PersistentData {
            settings: Settings {
                sound_enabled: false,
                animation_speed: AnimationSpeed::Slow,
                cursor_style: CursorStyle::Heart,
                contrast: 0x72,
                standby_refresh_seconds: 24,
            },
            snake_high_score: 1234,
        };
        assert_eq!(
            decode(&encode(data, 42)),
            Ok(StoredRecord {
                generation: 42,
                data
            })
        );
    }

    #[test]
    fn one_bit_corruption_is_rejected() {
        let mut bytes = encode(PersistentData::default(), 1);
        bytes[13] ^= 0x20;
        assert_eq!(decode(&bytes), Err(DecodeError::Corrupt));
    }

    #[test]
    fn erased_page_is_not_a_record() {
        assert_eq!(decode(&[0xff; RECORD_SIZE]), Err(DecodeError::BadMagic));
    }

    #[test]
    fn newest_handles_generation_wrap() {
        let old = StoredRecord {
            generation: u32::MAX,
            data: PersistentData::default(),
        };
        let new = StoredRecord {
            generation: 0,
            data: PersistentData::default(),
        };
        assert_eq!(newest(Some(old), Some(new)), Some(new));
    }
}
