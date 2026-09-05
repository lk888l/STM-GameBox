//! Board pin contract, clocks, and electrical policy.

use embassy_stm32::{
    gpio::{Input, SwjCfg},
    rcc::{
        ADCPrescaler, AHBPrescaler, APBPrescaler, Hse, HseMode, LsConfig, Pll, PllMul, PllPreDiv,
        PllSource, Sysclk,
    },
    time::Hertz,
};

/// External crystal fitted to the legacy board.
pub const HSE_FREQUENCY_HZ: u32 = 8_000_000;
/// Cortex-M3 system clock after the HSE ×9 PLL.
pub const SYSTEM_CLOCK_FREQUENCY_HZ: u32 = 72_000_000;

/// Read all eight active-low controls from one coherent GPIOB IDR snapshot.
#[must_use]
pub fn sample_buttons(_configured_inputs: &[Input<'_>; 8]) -> [bool; 8] {
    let levels = embassy_stm32::pac::GPIOB.idr().read().0;
    [7_u8, 5, 6, 4, 12, 13, 14, 15].map(|pin| levels & (1_u32 << pin) == 0)
}

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
    config.rcc.ls = LsConfig::default_lse();

    // PB4 is the right button on the original PCB. SWD remains enabled on
    // PA13/PA14, while the unused JTAG port releases PB4 (and PA15/PB3).
    config.swj = SwjCfg::SwdOnly;
    config
}
