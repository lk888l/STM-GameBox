//! Versioned, checksummed persistent-data codec.

use crate::settings::{Brightness, HomeHeaderMode, MotionLevel, Settings};

/// Serialized record size; divisible by the STM32F103 Flash write size.
pub const RECORD_SIZE: usize = 32;
const MAGIC: [u8; 4] = *b"GBX2";
const SCHEMA_VERSION: u8 = 2;
const LEGACY_SCHEMA_VERSION: u8 = 1;
const CRC_OFFSET: usize = 28;

/// Application data retained across power cycles.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct PersistentData {
    /// User-facing settings.
    pub settings: Settings,
    /// Best Snake score achieved on this device.
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
    let mut bytes = [0_u8; RECORD_SIZE];
    bytes[0..4].copy_from_slice(&MAGIC);
    bytes[4] = SCHEMA_VERSION;
    bytes[5] = u8::from(data.settings.sound_enabled);
    bytes[6] = data.settings.motion as u8;
    bytes[7] = data.settings.brightness as u8;
    bytes[8] = data.settings.home_header as u8;
    bytes[12..14].copy_from_slice(&data.snake_high_score.to_le_bytes());
    bytes[16..20].copy_from_slice(&generation.to_le_bytes());
    let crc = crc32(&bytes[..CRC_OFFSET]);
    bytes[CRC_OFFSET..].copy_from_slice(&crc.to_le_bytes());
    bytes
}

/// Decode one candidate record, including schema-v1 migration.
pub fn decode(bytes: &[u8]) -> Result<StoredRecord, DecodeError> {
    if bytes.len() != RECORD_SIZE {
        return Err(DecodeError::WrongLength);
    }
    if bytes[..4] != MAGIC {
        return Err(DecodeError::BadMagic);
    }
    if bytes[4] != SCHEMA_VERSION && bytes[4] != LEGACY_SCHEMA_VERSION {
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

    let settings = if bytes[4] == SCHEMA_VERSION {
        Settings {
            sound_enabled: bytes[5] != 0,
            motion: MotionLevel::from_byte(bytes[6]),
            brightness: Brightness::from_byte(bytes[7]),
            home_header: HomeHeaderMode::from_byte(bytes[8]),
        }
    } else {
        // v1 stored Off/Slow/Fast animation and a raw contrast byte. Preserve
        // intent while retiring cursor and standby-only settings.
        let motion = match bytes[6] {
            0 => MotionLevel::Off,
            1 => MotionLevel::Reduced,
            _ => MotionLevel::Full,
        };
        let brightness = match bytes[8] {
            0..=0x43 => Brightness::Low,
            0x44..=0x7f => Brightness::Medium,
            0x80..=0xb7 => Brightness::High,
            _ => Brightness::Max,
        };
        Settings {
            sound_enabled: bytes[5] != 0,
            motion,
            brightness,
            home_header: HomeHeaderMode::Time,
        }
    };

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
                motion: MotionLevel::Reduced,
                brightness: Brightness::High,
                home_header: HomeHeaderMode::Pet,
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
    fn schema_one_is_migrated() {
        let mut bytes = [0_u8; RECORD_SIZE];
        bytes[..4].copy_from_slice(&MAGIC);
        bytes[4] = LEGACY_SCHEMA_VERSION;
        bytes[5] = 1;
        bytes[6] = 1;
        bytes[8] = 0x72;
        let crc = crc32(&bytes[..CRC_OFFSET]);
        bytes[CRC_OFFSET..].copy_from_slice(&crc.to_le_bytes());
        let record = decode(&bytes).unwrap();
        assert_eq!(record.data.settings.motion, MotionLevel::Reduced);
        assert_eq!(record.data.settings.brightness, Brightness::Medium);
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
