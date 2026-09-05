//! SSD1306 four-wire SPI1 wiring and F1-specific DMA recovery.

use embassy_stm32::{
    Peri, bind_interrupts, dma,
    gpio::{Level, Output, Speed},
    mode::Async,
    pac::spi::vals::{Bidimode, Bidioe, Br, Cpha, Cpol, Dff, Lsbfirst, Mstr, Rxonly},
    peripherals, spi,
    time::Hertz,
};
use embassy_time::{Delay, Duration};
use oled_driver::{DisplayClock, SpiTransport, reset_async};

pub(super) const INTERFACE: &str = "SPI1 TX DMA";
/// Highest PCLK2 divisor below the SSD1306 10 MHz serial-clock limit.
pub(super) const BUS_FREQUENCY_HZ: u32 = 9_000_000;
pub(super) const OPERATION_TIMEOUT: Duration = Duration::from_millis(20);
pub(super) const DISPLAY_CLOCK: DisplayClock = DisplayClock::Maximum;

pub(super) type Transport<'d> =
    SpiTransport<spi::Spi<'d, Async, spi::mode::Master>, Output<'d>, Output<'d>>;

bind_interrupts!(struct Irqs {
    DMA1_CHANNEL3 => dma::InterruptHandler<peripherals::DMA1_CH3>;
});

/// Pins and peripherals consumed by the SPI build, leaving I²C pins free.
pub struct Resources<'d> {
    /// SPI1 peripheral, clocked from PCLK2.
    pub spi: Peri<'d, peripherals::SPI1>,
    /// PA5: SCLK / D0 / CLK.
    pub sck: Peri<'d, peripherals::PA5>,
    /// PA7: SDIN / D1 / DIN.
    pub mosi: Peri<'d, peripherals::PA7>,
    /// PA6: data/command select.
    pub dc: Peri<'d, peripherals::PA6>,
    /// PA4: active-low chip select.
    pub cs: Peri<'d, peripherals::PA4>,
    /// PA8: active-low panel reset.
    pub reset: Peri<'d, peripherals::PA8>,
    /// Dedicated SPI1 TX DMA channel.
    pub tx_dma: Peri<'d, peripherals::DMA1_CH3>,
}

pub(super) struct Reset<'d>(Output<'d>);

impl Reset<'_> {
    pub(super) async fn reset_panel(&mut self) {
        reset_async(&mut self.0, &mut Delay)
            .await
            .expect("the STM32 reset GPIO is infallible");
    }
}

impl<'d> Resources<'d> {
    pub(super) fn into_parts(self) -> (Transport<'d>, Reset<'d>) {
        let mut spi = spi::Spi::new_txonly(
            self.spi,
            self.sck,
            self.mosi,
            self.tx_dma,
            Irqs,
            spi_config(),
        );
        let dc = Output::new(self.dc, Level::Low, Speed::VeryHigh);
        let cs = Output::new(self.cs, Level::High, Speed::VeryHigh);
        let reset = Reset(Output::new(self.reset, Level::High, Speed::Medium));
        prepare_spi(&mut spi);
        let transport = SpiTransport::new(spi, dc, cs).expect("STM32 GPIO outputs are infallible");
        (transport, reset)
    }
}

fn spi_config() -> spi::Config {
    let mut config = spi::Config::default();
    config.mode = spi::MODE_0;
    config.bit_order = spi::BitOrder::MsbFirst;
    config.frequency = Hertz(BUS_FREQUENCY_HZ);
    config.gpio_speed = Speed::VeryHigh;
    config.nss_output_disable = true;
    config
}

/// CS# is already high and no DMA is active when this is called.
fn prepare_spi(spi: &mut spi::Spi<'_, Async, spi::mode::Master>) {
    // Avoid unread RX data/overruns on the write-only interface.
    spi.set_direction(Some(spi::Direction::Transmit));
    // Embassy's constructor enables SPI; async writes first disable it and
    // leave it disabled afterwards. On this F1 board disabling SPI raises
    // SCLK. Normalize the first write BEFORE CS# goes low, otherwise that
    // extra selected rising edge corrupts initialization. Hardware-tested.
    embassy_stm32::pac::SPI1
        .cr1()
        .modify(|register| register.set_spe(false));
}

/// The owning Oled calls this only after cancellation has released CS# and DMA.
pub(super) fn recover_bus() {
    cortex_m::interrupt::free(|_| {
        let rcc = embassy_stm32::pac::RCC;
        let spi = embassy_stm32::pac::SPI1;
        rcc.apb2enr().modify(|register| register.set_spi1en(true));
        rcc.apb2rstr().modify(|register| register.set_spi1rst(true));
        rcc.apb2rstr()
            .modify(|register| register.set_spi1rst(false));

        // Match spi_config()/prepare_spi(): PCLK2/8 = 9 MHz, 8-bit mode 0,
        // software NSS, one-line TX. Both SPE and TXDMAEN must remain clear
        // until the next DMA write, including after fault recovery.
        spi.cr2().write(|register| register.set_ssoe(false));
        spi.cr1().write(|register| {
            register.set_cpha(Cpha::FIRST_EDGE);
            register.set_cpol(Cpol::IDLE_LOW);
            register.set_mstr(Mstr::MASTER);
            register.set_br(Br::DIV8);
            register.set_lsbfirst(Lsbfirst::MSBFIRST);
            register.set_ssi(true);
            register.set_ssm(true);
            register.set_rxonly(Rxonly::FULL_DUPLEX);
            register.set_dff(Dff::BITS8);
            register.set_crcen(false);
            register.set_bidioe(Bidioe::TRANSMIT);
            register.set_bidimode(Bidimode::BIDIRECTIONAL);
            register.set_spe(false);
        });
    });
}
