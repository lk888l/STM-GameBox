//! Legacy SSD1306 I²C1 wiring and STM32F1 BUSY/START recovery.

use core::marker::PhantomData;

use embassy_stm32::{
    Peri, bind_interrupts, dma, i2c,
    mode::Async,
    pac::i2c::vals::{Duty, FS},
    peripherals,
    time::Hertz,
};
use embassy_time::Duration;
use oled_driver::{DisplayClock, I2cTransport};

pub(super) const INTERFACE: &str = "I2C1 DMA (0x3c)";
pub(super) const BUS_FREQUENCY_HZ: u32 = 400_000;
// A full frame needs about 23 ms on the wire, so the SPI 20 ms deadline would
// wrongly cancel valid I²C frames. Preserve the validated legacy 80 ms budget.
pub(super) const OPERATION_TIMEOUT: Duration = Duration::from_millis(80);
pub(super) const DISPLAY_CLOCK: DisplayClock = DisplayClock::Default;
const ADDRESS: u8 = 0x3c;

pub(super) type Transport<'d> = I2cTransport<i2c::I2c<'d, Async, i2c::Master>>;

bind_interrupts!(struct Irqs {
    I2C1_EV => i2c::EventInterruptHandler<peripherals::I2C1>;
    I2C1_ER => i2c::ErrorInterruptHandler<peripherals::I2C1>;
    DMA1_CHANNEL6 => dma::InterruptHandler<peripherals::DMA1_CH6>;
    DMA1_CHANNEL7 => dma::InterruptHandler<peripherals::DMA1_CH7>;
});

/// Legacy remapped I²C1 pins; SPI/control pins are not consumed in this build.
pub struct Resources<'d> {
    /// I²C1 peripheral, clocked from PCLK1.
    pub i2c: Peri<'d, peripherals::I2C1>,
    /// PB8: SCL, requiring an external pull-up.
    pub scl: Peri<'d, peripherals::PB8>,
    /// PB9: SDA, requiring an external pull-up.
    pub sda: Peri<'d, peripherals::PB9>,
    /// I²C1 TX DMA.
    pub tx_dma: Peri<'d, peripherals::DMA1_CH6>,
    /// I²C1 RX DMA required by Embassy's I²C bus constructor.
    pub rx_dma: Peri<'d, peripherals::DMA1_CH7>,
}

// The four-pin legacy module has no MCU-controlled RES#. Do not claim PA8 or
// any SPI pin just to provide a shared reset API; this owner is zero-sized.
pub(super) struct Reset<'d>(PhantomData<&'d ()>);

impl Reset<'_> {
    pub(super) async fn reset_panel(&mut self) {}
}

impl<'d> Resources<'d> {
    pub(super) fn into_parts(self) -> (Transport<'d>, Reset<'d>) {
        let mut config = i2c::Config::default();
        config.frequency = Hertz(BUS_FREQUENCY_HZ);
        config.timeout = Duration::from_millis(30);
        let i2c = i2c::I2c::new(
            self.i2c,
            self.scl,
            self.sda,
            self.tx_dma,
            self.rx_dma,
            Irqs,
            config,
        );
        let transport = I2cTransport::new(i2c, ADDRESS).expect("OLED address is seven-bit");
        (transport, Reset(PhantomData))
    }
}

/// The owning Oled calls this only after the I²C/DMA future has been dropped.
pub(super) fn recover_bus() {
    cortex_m::interrupt::free(|_| {
        let rcc = embassy_stm32::pac::RCC;
        let i2c = embassy_stm32::pac::I2C1;
        rcc.apb1enr().modify(|register| register.set_i2c1en(true));
        rcc.apb1rstr().modify(|register| register.set_i2c1rst(true));
        rcc.apb1rstr()
            .modify(|register| register.set_i2c1rst(false));

        // Repeat Embassy's F1 SWRST initialization workaround after lockup.
        i2c.cr1().write(|register| register.set_swrst(true));
        i2c.cr1().write(|register| register.set_swrst(false));
        // PCLK1=36 MHz, 400 kHz fast mode, duty 2:1; keep in sync with config.
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
