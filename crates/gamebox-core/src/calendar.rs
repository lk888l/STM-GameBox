//! Gregorian calendar value types and allocation-free conversion helpers.

/// First year represented by the STM32F1 RTC counter.
pub const EPOCH_YEAR: u16 = 2000;
/// Product default used after backup-domain loss.
pub const DEFAULT_DATE_TIME: DateTime = DateTime {
    date: Date {
        year: 26,
        month: 1,
        day: 1,
    },
    hours: 0,
    minutes: 0,
    seconds: 0,
};

/// A date in the RTC-supported 2000–2099 range.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Date {
    /// Year offset from 2000.
    pub year: u8,
    /// One-based month.
    pub month: u8,
    /// One-based day of month.
    pub day: u8,
}

impl Default for Date {
    fn default() -> Self {
        DEFAULT_DATE_TIME.date
    }
}

impl Date {
    /// Return whether every component is in the supported Gregorian range.
    #[must_use]
    pub const fn is_valid(self) -> bool {
        let maximum = days_in_month(self.year, self.month);
        self.year <= 99 && maximum != 0 && self.day != 0 && self.day <= maximum
    }

    /// Advance by whole days, saturating at 2099-12-31 on overflow.
    ///
    /// The return value is false only when the requested date exceeds the
    /// representable RTC range or the input date is invalid.
    pub fn advance(&mut self, mut days: u32) -> bool {
        if !self.is_valid() {
            return false;
        }
        while days != 0 {
            let maximum = days_in_month(self.year, self.month);
            if self.day < maximum {
                self.day += 1;
            } else if self.month < 12 {
                self.month += 1;
                self.day = 1;
            } else if self.year < 99 {
                self.year += 1;
                self.month = 1;
                self.day = 1;
            } else {
                *self = Self {
                    year: 99,
                    month: 12,
                    day: 31,
                };
                return false;
            }
            days -= 1;
        }
        true
    }
}

/// Complete wall-clock value shown and edited by the UI.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DateTime {
    /// Calendar date.
    pub date: Date,
    /// Hour in the range 0–23.
    pub hours: u8,
    /// Minute in the range 0–59.
    pub minutes: u8,
    /// Second in the range 0–59.
    pub seconds: u8,
}

impl Default for DateTime {
    fn default() -> Self {
        DEFAULT_DATE_TIME
    }
}

impl DateTime {
    /// Return whether date and time components are all valid.
    #[must_use]
    pub const fn is_valid(self) -> bool {
        self.date.is_valid() && self.hours < 24 && self.minutes < 60 && self.seconds < 60
    }

    /// Convert to seconds since 2000-01-01.
    #[must_use]
    pub fn to_epoch_seconds(self) -> Option<u32> {
        if !self.is_valid() {
            return None;
        }
        let mut days = 0_u32;
        let mut year = 0_u8;
        while year < self.date.year {
            days += if is_leap_year(year) { 366 } else { 365 };
            year += 1;
        }
        let mut month = 1_u8;
        while month < self.date.month {
            days += u32::from(days_in_month(self.date.year, month));
            month += 1;
        }
        days += u32::from(self.date.day - 1);
        Some(
            days * 86_400
                + u32::from(self.hours) * 3_600
                + u32::from(self.minutes) * 60
                + u32::from(self.seconds),
        )
    }

    /// Convert seconds since 2000-01-01 to a supported date and time.
    #[must_use]
    pub fn from_epoch_seconds(seconds: u32) -> Option<Self> {
        let mut days = seconds / 86_400;
        let seconds_today = seconds % 86_400;
        let mut year = 0_u8;
        loop {
            let length = if is_leap_year(year) { 366 } else { 365 };
            if days < length {
                break;
            }
            if year == 99 {
                return None;
            }
            days -= length;
            year += 1;
        }
        let mut month = 1_u8;
        loop {
            let length = u32::from(days_in_month(year, month));
            if days < length {
                break;
            }
            days -= length;
            month += 1;
        }
        Some(Self {
            date: Date {
                year,
                month,
                day: days as u8 + 1,
            },
            hours: (seconds_today / 3_600) as u8,
            minutes: ((seconds_today / 60) % 60) as u8,
            seconds: (seconds_today % 60) as u8,
        })
    }
}

/// Number of days in one month, or zero for an invalid month.
#[must_use]
pub const fn days_in_month(year: u8, month: u8) -> u8 {
    match month {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        2 if is_leap_year(year) => 29,
        2 => 28,
        _ => 0,
    }
}

const fn is_leap_year(year: u8) -> bool {
    // The represented range is 2000–2099; the Gregorian century exception
    // therefore never occurs inside it.
    year.is_multiple_of(4)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validates_leap_days_and_range_edges() {
        assert!(
            Date {
                year: 24,
                month: 2,
                day: 29
            }
            .is_valid()
        );
        assert!(
            !Date {
                year: 25,
                month: 2,
                day: 29
            }
            .is_valid()
        );
    }

    #[test]
    fn date_advance_crosses_year_and_saturates() {
        let mut date = Date {
            year: 23,
            month: 12,
            day: 31,
        };
        assert!(date.advance(60));
        assert_eq!(
            date,
            Date {
                year: 24,
                month: 2,
                day: 29
            }
        );

        let mut maximum = Date {
            year: 99,
            month: 12,
            day: 31,
        };
        assert!(!maximum.advance(1));
        assert_eq!(maximum.day, 31);
    }

    #[test]
    fn epoch_conversion_round_trips_boundaries() {
        for value in [
            DEFAULT_DATE_TIME,
            DateTime {
                date: Date {
                    year: 0,
                    month: 2,
                    day: 29,
                },
                hours: 23,
                minutes: 59,
                seconds: 59,
            },
            DateTime {
                date: Date {
                    year: 99,
                    month: 12,
                    day: 31,
                },
                hours: 23,
                minutes: 59,
                seconds: 59,
            },
        ] {
            assert_eq!(
                DateTime::from_epoch_seconds(value.to_epoch_seconds().unwrap()),
                Some(value)
            );
        }
    }
}
