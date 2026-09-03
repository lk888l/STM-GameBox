//! STM32F1 32-bit RTC-counter adapter with Gregorian conversion in the core.

use embassy_stm32::{Peri, pac, peripherals};
use gamebox_core::calendar::{DEFAULT_DATE_TIME, DateTime};

const BACKUP_MARKER: u16 = 0x4742;
const WAIT_SPINS: usize = 200_000;

/// Bounded RTC access failure.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RtcError {
    /// RTC synchronization or write completion did not arrive.
    Timeout,
    /// Counter lies outside the supported 2000–2099 calendar range.
    InvalidCounter,
    /// Requested calendar value is invalid.
    InvalidDateTime,
}

/// Safe owner facade over the STM32F1 RTC v1 PAC registers.
pub struct RtcClock;

impl RtcClock {
    /// Claim RTC/BKP ownership, preserve a valid backup-domain clock, and
    /// initialize a one-hertz counter after backup-domain loss.
    pub fn new(
        _rtc: Peri<'static, peripherals::RTC>,
        _bkp: Peri<'static, peripherals::BKP>,
    ) -> Result<Self, RtcError> {
        pac::RCC.apb1enr().modify(|register| {
            register.set_pwren(true);
            register.set_bkpen(true);
        });
        pac::PWR.cr().modify(|register| register.set_dbp(true));

        let clock = Self;
        clock.synchronize()?;
        if pac::BKP.dr(0).read().d() != BACKUP_MARKER || clock.now().is_err() {
            clock.configure_one_hertz()?;
            clock.set(DEFAULT_DATE_TIME)?;
            pac::BKP
                .dr(0)
                .write(|register| register.set_d(BACKUP_MARKER));
        }
        Ok(clock)
    }

    /// Read a coherent 32-bit counter and convert it to calendar components.
    pub fn now(&self) -> Result<DateTime, RtcError> {
        let rtc = pac::RTC;
        for _ in 0..4 {
            let high = rtc.cnth().read().cnth();
            let low = rtc.cntl().read().cntl();
            if rtc.cnth().read().cnth() == high {
                let seconds = (u32::from(high) << 16) | u32::from(low);
                return DateTime::from_epoch_seconds(seconds).ok_or(RtcError::InvalidCounter);
            }
        }
        Err(RtcError::Timeout)
    }

    /// Atomically replace the RTC second counter.
    pub fn set(&self, value: DateTime) -> Result<(), RtcError> {
        let seconds = value.to_epoch_seconds().ok_or(RtcError::InvalidDateTime)?;
        self.wait_write_complete()?;
        let rtc = pac::RTC;
        rtc.crl().modify(|register| register.set_cnf(true));
        rtc.cnth()
            .write(|register| register.set_cnth((seconds >> 16) as u16));
        rtc.cntl()
            .write(|register| register.set_cntl(seconds as u16));
        rtc.crl().modify(|register| register.set_cnf(false));
        self.wait_write_complete()
    }

    fn configure_one_hertz(&self) -> Result<(), RtcError> {
        self.wait_write_complete()?;
        let rtc = pac::RTC;
        rtc.crl().modify(|register| register.set_cnf(true));
        rtc.prlh().write(|register| register.set_prlh(0));
        rtc.prll().write(|register| register.set_prll(32_767));
        rtc.crl().modify(|register| register.set_cnf(false));
        self.wait_write_complete()
    }

    fn synchronize(&self) -> Result<(), RtcError> {
        let rtc = pac::RTC;
        rtc.crl().modify(|register| register.set_rsf(false));
        for _ in 0..WAIT_SPINS {
            if rtc.crl().read().rsf() {
                return Ok(());
            }
            core::hint::spin_loop();
        }
        Err(RtcError::Timeout)
    }

    fn wait_write_complete(&self) -> Result<(), RtcError> {
        use pac::rtc::vals::Rtoff;
        for _ in 0..WAIT_SPINS {
            if pac::RTC.crl().read().rtoff() == Rtoff::TERMINATED {
                return Ok(());
            }
            core::hint::spin_loop();
        }
        Err(RtcError::Timeout)
    }
}
