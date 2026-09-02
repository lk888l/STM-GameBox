//! Board pin contract, clocks, and electrical policy.

use embassy_stm32::{
    gpio::SwjCfg,
    i2c,
    pac::i2c::vals::{Duty, FS},
    rcc::{
        ADCPrescaler, AHBPrescaler, APBPrescaler, Hse, HseMode, Pll, PllMul, PllPreDiv, PllSource,
        Sysclk,
    },
    time::Hertz,
};
use embassy_time::Duration;

/// External crystal fitted to the legacy board.
pub const HSE_FREQUENCY_HZ: u32 = 8_000_000;
/// Cortex-M3 system clock after the HSE ×9 PLL.
pub const SYSTEM_CLOCK_FREQUENCY_HZ: u32 = 72_000_000;
/// Seven-bit SSD1306 bus address.
pub const OLED_ADDRESS: u8 = 0x3c;
/// OLED bus clock; the panel and the legacy wiring must have external pull-ups.
pub const OLED_I2C_FREQUENCY_HZ: u32 = 400_000;
/// Application-level deadline around every OLED transaction.
///
/// Embassy's F1 async I²C implementation is interrupt-driven. A missing
/// peripheral clock or the F1 misplaced-STOP erratum can therefore prevent
/// the HAL's ordinary error path from ever being polled. This deadline makes
/// cancellation and recovery independent of an I²C interrupt.
pub const OLED_OPERATION_TIMEOUT: Duration = Duration::from_millis(80);

/// Return a 72 MHz clock tree that keeps APB1 and ADC within datasheet limits.
#[must_use]
pub fn mcu_config() -> embassy_stm32::Config {
    let mut config = embassy_stm32::Config::default();
    config.rcc.hsi = true;
    config.rcc.hse = Some(Hse {
        freq: Hertz(HSE_FREQUENCY_HZ),
        mode: HseMode::Oscillator,
    });
    config.rcc.pll = Some(Pll {
        src: PllSource::HSE,
        prediv: PllPreDiv::DIV1,
        mul: PllMul::MUL9,
    });
    config.rcc.sys = Sysclk::PLL1_P;
    config.rcc.ahb_pre = AHBPrescaler::DIV1;
    config.rcc.apb1_pre = APBPrescaler::DIV2;
    config.rcc.apb2_pre = APBPrescaler::DIV1;
    config.rcc.adc_pre = ADCPrescaler::DIV6;

    // PB4 is the right button on the original PCB. SWD remains enabled on
    // PA13/PA14, while the unused JTAG port releases PB4 (and PA15/PB3).
    config.swj = SwjCfg::SwdOnly;
    config
}

/// OLED I²C policy shared by the composition root.
#[must_use]
pub fn oled_i2c_config() -> i2c::Config {
    let mut config = i2c::Config::default();
    config.frequency = Hertz(OLED_I2C_FREQUENCY_HZ);
    config.timeout = Duration::from_millis(30);
    config
}

/// Restore I²C1 after a cancelled transfer or an F1 BUSY/START lockup.
///
/// The caller must own I²C1 and must only invoke this after the outstanding
/// operation future has been dropped. Direct PAC access is intentionally
/// isolated here because Embassy 0.6 does not expose the F1 runtime SWRST
/// workaround through its public `I2c` API.
pub fn recover_oled_i2c() {
    cortex_m::interrupt::free(|_| {
        let rcc = embassy_stm32::pac::RCC;
        let i2c = embassy_stm32::pac::I2C1;

        // Re-enable and reset only I²C1; the DMA future's drop guard has
        // already disabled its channel before this function is entered.
        rcc.apb1enr().modify(|register| register.set_i2c1en(true));
        rcc.apb1rstr().modify(|register| register.set_i2c1rst(true));
        rcc.apb1rstr()
            .modify(|register| register.set_i2c1rst(false));

        // RM0008 erratum workaround used by embassy-stm32 during initial
        // construction, repeated here for runtime recovery.
        i2c.cr1().write(|register| register.set_swrst(true));
        i2c.cr1().write(|register| register.set_swrst(false));

        // PCLK1=36 MHz, fast mode, duty 2:1: CCR=36 MHz/(3*400 kHz)=30;
        // TRISE=11 for the 300 ns fast-mode rise-time limit.
        i2c.cr2().write(|register| register.set_freq(36));
        i2c.ccr().write(|register| {
            register.set_f_s(FS::FAST);
            register.set_duty(Duty::DUTY2_1);
            register.set_ccr(30);
        });
        i2c.trise().write(|register| register.set_trise(11));
        i2c.cr1().write(|register| register.set_pe(true));
    });
}
