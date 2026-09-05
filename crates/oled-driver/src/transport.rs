use core::fmt;

#[cfg(any(feature = "blocking", feature = "async"))]
use embedded_hal::digital::OutputPin;

use crate::{ConfigError, TransferReport};

const COMMAND_CONTROL: [u8; 1] = [0x00];
const DATA_CONTROL: [u8; 1] = [0x40];

/// Blocking command/data transport used by display controllers.
#[cfg(feature = "blocking")]
pub trait BlockingTransport {
    /// Underlying bus error.
    type Error;

    /// Send controller command bytes.
    fn write_commands(&mut self, commands: &[u8]) -> Result<TransferReport, Self::Error>;

    /// Send display memory bytes.
    fn write_data(&mut self, data: &[u8]) -> Result<TransferReport, Self::Error>;
}

/// Asynchronous command/data transport used by display controllers.
#[cfg(feature = "async")]
pub trait AsyncTransport {
    /// Underlying bus error.
    type Error;

    /// Send controller command bytes.
    async fn write_commands(&mut self, commands: &[u8]) -> Result<TransferReport, Self::Error>;

    /// Send display memory bytes.
    async fn write_data(&mut self, data: &[u8]) -> Result<TransferReport, Self::Error>;
}

/// I²C transport for controllers using `0x00` commands and `0x40` data.
///
/// The control byte and caller-owned payload are submitted as two write
/// operations in one I²C transaction. This avoids a staging-buffer copy while
/// retaining a single START/address phase on HAL implementations that support
/// transactions natively.
#[derive(Debug)]
pub struct I2cTransport<I2C> {
    i2c: I2C,
    address: u8,
}

/// Values returned when an I²C transport is released.
#[derive(Debug)]
pub struct TransportParts<I2C> {
    /// Owned I²C peripheral.
    pub i2c: I2C,
    /// Seven-bit display address.
    pub address: u8,
}

/// Four-wire SPI transport for displays with separate D/C# and CS# pins.
///
/// The SPI peripheral owns SCLK and SDIN/MOSI. This transport drives D/C# low
/// for controller commands and high for display data, and brackets every
/// payload with an active-low CS# transaction.
#[derive(Debug)]
pub struct SpiTransport<SPI, DC, CS> {
    spi: SPI,
    data_command: DC,
    chip_select: CS,
}

/// Values returned when a four-wire SPI transport is released.
#[derive(Debug)]
pub struct SpiTransportParts<SPI, DC, CS> {
    /// Owned SPI bus.
    pub spi: SPI,
    /// Owned data/command pin.
    pub data_command: DC,
    /// Owned active-low chip-select pin.
    pub chip_select: CS,
}

/// Error from a four-wire SPI bus or one of its control pins.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpiTransportError<SpiError, PinError> {
    /// The SPI bus failed while transferring a payload.
    Spi(SpiError),
    /// The D/C# pin could not select command or data mode.
    DataCommand(PinError),
    /// The active-low CS# pin could not be asserted or released.
    ChipSelect(PinError),
}

impl<SpiError, PinError> fmt::Display for SpiTransportError<SpiError, PinError>
where
    SpiError: fmt::Display,
    PinError: fmt::Display,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Spi(error) => write!(formatter, "SPI bus error: {error}"),
            Self::DataCommand(error) => write!(formatter, "D/C# pin error: {error}"),
            Self::ChipSelect(error) => write!(formatter, "CS# pin error: {error}"),
        }
    }
}

impl<SpiError, PinError> core::error::Error for SpiTransportError<SpiError, PinError>
where
    SpiError: core::error::Error + 'static,
    PinError: core::error::Error + 'static,
{
    fn source(&self) -> Option<&(dyn core::error::Error + 'static)> {
        match self {
            Self::Spi(error) => Some(error),
            Self::DataCommand(error) | Self::ChipSelect(error) => Some(error),
        }
    }
}

#[cfg(any(feature = "blocking", feature = "async"))]
impl<SPI, DC, CS> SpiTransport<SPI, DC, CS>
where
    DC: OutputPin,
    CS: OutputPin<Error = DC::Error>,
{
    /// Construct a four-wire SPI transport and leave the panel deselected.
    pub fn new(spi: SPI, mut data_command: DC, mut chip_select: CS) -> Result<Self, DC::Error> {
        chip_select.set_high()?;
        data_command.set_low()?;
        Ok(Self {
            spi,
            data_command,
            chip_select,
        })
    }

    /// Release the SPI peripheral and control pins.
    #[must_use]
    pub fn release(self) -> SpiTransportParts<SPI, DC, CS> {
        SpiTransportParts {
            spi: self.spi,
            data_command: self.data_command,
            chip_select: self.chip_select,
        }
    }
}

/// Active-low chip-select guard that also releases CS# when an async transfer
/// future is cancelled by a timeout.
#[cfg(any(feature = "blocking", feature = "async"))]
struct ChipSelectGuard<'a, CS>
where
    CS: OutputPin,
{
    pin: &'a mut CS,
    selected: bool,
}

#[cfg(any(feature = "blocking", feature = "async"))]
impl<'a, CS> ChipSelectGuard<'a, CS>
where
    CS: OutputPin,
{
    fn select(pin: &'a mut CS) -> Result<Self, CS::Error> {
        pin.set_low()?;
        Ok(Self {
            pin,
            selected: true,
        })
    }

    fn deselect(mut self) -> Result<(), CS::Error> {
        let result = self.pin.set_high();
        self.selected = false;
        result
    }
}

#[cfg(any(feature = "blocking", feature = "async"))]
impl<CS> Drop for ChipSelectGuard<'_, CS>
where
    CS: OutputPin,
{
    fn drop(&mut self) {
        if self.selected {
            let _ = self.pin.set_high();
        }
    }
}

impl<I2C> I2cTransport<I2C> {
    /// Construct an I²C transport.
    pub fn new(i2c: I2C, address: u8) -> Result<Self, ConfigError> {
        if address >= 0x80 {
            return Err(ConfigError::InvalidI2cAddress(address));
        }
        Ok(Self { i2c, address })
    }

    /// Release the bus.
    pub fn release(self) -> TransportParts<I2C> {
        TransportParts {
            i2c: self.i2c,
            address: self.address,
        }
    }

    /// Return the configured seven-bit address.
    pub const fn address(&self) -> u8 {
        self.address
    }
}

#[cfg(feature = "blocking")]
impl<I2C> I2cTransport<I2C>
where
    I2C: embedded_hal::i2c::I2c,
{
    fn write_prefixed_blocking(
        &mut self,
        control: &'static [u8; 1],
        payload: &[u8],
    ) -> Result<TransferReport, I2C::Error> {
        use embedded_hal::i2c::Operation;

        if payload.is_empty() {
            return Ok(TransferReport::default());
        }

        let mut operations = [Operation::Write(control), Operation::Write(payload)];
        self.i2c.transaction(self.address, &mut operations)?;
        Ok(TransferReport::new(payload.len(), 1))
    }
}

#[cfg(feature = "blocking")]
impl<I2C> BlockingTransport for I2cTransport<I2C>
where
    I2C: embedded_hal::i2c::I2c,
{
    type Error = I2C::Error;

    fn write_commands(&mut self, commands: &[u8]) -> Result<TransferReport, Self::Error> {
        self.write_prefixed_blocking(&COMMAND_CONTROL, commands)
    }

    fn write_data(&mut self, data: &[u8]) -> Result<TransferReport, Self::Error> {
        self.write_prefixed_blocking(&DATA_CONTROL, data)
    }
}

#[cfg(feature = "async")]
impl<I2C> I2cTransport<I2C>
where
    I2C: embedded_hal_async::i2c::I2c,
{
    async fn write_prefixed_async(
        &mut self,
        control: &'static [u8; 1],
        payload: &[u8],
    ) -> Result<TransferReport, I2C::Error> {
        use embedded_hal_async::i2c::Operation;

        if payload.is_empty() {
            return Ok(TransferReport::default());
        }

        let mut operations = [Operation::Write(control), Operation::Write(payload)];
        self.i2c.transaction(self.address, &mut operations).await?;
        Ok(TransferReport::new(payload.len(), 1))
    }
}

#[cfg(feature = "async")]
impl<I2C> AsyncTransport for I2cTransport<I2C>
where
    I2C: embedded_hal_async::i2c::I2c,
{
    type Error = I2C::Error;

    async fn write_commands(&mut self, commands: &[u8]) -> Result<TransferReport, Self::Error> {
        self.write_prefixed_async(&COMMAND_CONTROL, commands).await
    }

    async fn write_data(&mut self, data: &[u8]) -> Result<TransferReport, Self::Error> {
        self.write_prefixed_async(&DATA_CONTROL, data).await
    }
}

#[cfg(feature = "blocking")]
impl<SPI, DC, CS> SpiTransport<SPI, DC, CS>
where
    SPI: embedded_hal::spi::SpiBus<u8>,
    DC: OutputPin,
    CS: OutputPin<Error = DC::Error>,
{
    fn write_payload_blocking(
        &mut self,
        data_mode: bool,
        payload: &[u8],
    ) -> Result<TransferReport, SpiTransportError<SPI::Error, DC::Error>> {
        if payload.is_empty() {
            return Ok(TransferReport::default());
        }

        if data_mode {
            self.data_command.set_high()
        } else {
            self.data_command.set_low()
        }
        .map_err(SpiTransportError::DataCommand)?;

        let guard = ChipSelectGuard::select(&mut self.chip_select)
            .map_err(SpiTransportError::ChipSelect)?;
        let bus_result = self.spi.write(payload).and_then(|()| self.spi.flush());
        let chip_select_result = guard.deselect();

        bus_result.map_err(SpiTransportError::Spi)?;
        chip_select_result.map_err(SpiTransportError::ChipSelect)?;
        Ok(TransferReport::new(payload.len(), 1))
    }
}

#[cfg(feature = "blocking")]
impl<SPI, DC, CS> BlockingTransport for SpiTransport<SPI, DC, CS>
where
    SPI: embedded_hal::spi::SpiBus<u8>,
    DC: OutputPin,
    CS: OutputPin<Error = DC::Error>,
{
    type Error = SpiTransportError<SPI::Error, DC::Error>;

    fn write_commands(&mut self, commands: &[u8]) -> Result<TransferReport, Self::Error> {
        self.write_payload_blocking(false, commands)
    }

    fn write_data(&mut self, data: &[u8]) -> Result<TransferReport, Self::Error> {
        self.write_payload_blocking(true, data)
    }
}

#[cfg(feature = "async")]
impl<SPI, DC, CS> SpiTransport<SPI, DC, CS>
where
    SPI: embedded_hal_async::spi::SpiBus<u8>,
    DC: OutputPin,
    CS: OutputPin<Error = DC::Error>,
{
    async fn write_payload_async(
        &mut self,
        data_mode: bool,
        payload: &[u8],
    ) -> Result<TransferReport, SpiTransportError<SPI::Error, DC::Error>> {
        if payload.is_empty() {
            return Ok(TransferReport::default());
        }

        if data_mode {
            self.data_command.set_high()
        } else {
            self.data_command.set_low()
        }
        .map_err(SpiTransportError::DataCommand)?;

        let guard = ChipSelectGuard::select(&mut self.chip_select)
            .map_err(SpiTransportError::ChipSelect)?;
        let bus_result = match self.spi.write(payload).await {
            Ok(()) => self.spi.flush().await,
            Err(error) => Err(error),
        };
        let chip_select_result = guard.deselect();

        bus_result.map_err(SpiTransportError::Spi)?;
        chip_select_result.map_err(SpiTransportError::ChipSelect)?;
        Ok(TransferReport::new(payload.len(), 1))
    }
}

#[cfg(feature = "async")]
impl<SPI, DC, CS> AsyncTransport for SpiTransport<SPI, DC, CS>
where
    SPI: embedded_hal_async::spi::SpiBus<u8>,
    DC: OutputPin,
    CS: OutputPin<Error = DC::Error>,
{
    type Error = SpiTransportError<SPI::Error, DC::Error>;

    async fn write_commands(&mut self, commands: &[u8]) -> Result<TransferReport, Self::Error> {
        self.write_payload_async(false, commands).await
    }

    async fn write_data(&mut self, data: &[u8]) -> Result<TransferReport, Self::Error> {
        self.write_payload_async(true, data).await
    }
}

#[cfg(all(test, feature = "blocking"))]
mod tests {
    use core::convert::Infallible;

    use embedded_hal::{
        digital::{ErrorType as DigitalErrorType, OutputPin},
        spi::{ErrorType as SpiErrorType, SpiBus},
    };
    use std::vec::Vec;

    use super::{BlockingTransport, ChipSelectGuard, SpiTransport};

    #[derive(Default)]
    struct MockSpi {
        writes: Vec<Vec<u8>>,
        flushes: u8,
    }

    impl SpiErrorType for MockSpi {
        type Error = Infallible;
    }

    impl SpiBus<u8> for MockSpi {
        fn read(&mut self, words: &mut [u8]) -> Result<(), Self::Error> {
            words.fill(0);
            Ok(())
        }

        fn write(&mut self, words: &[u8]) -> Result<(), Self::Error> {
            self.writes.push(words.to_vec());
            Ok(())
        }

        fn transfer(&mut self, read: &mut [u8], write: &[u8]) -> Result<(), Self::Error> {
            for (destination, source) in read.iter_mut().zip(write.iter().copied()) {
                *destination = source;
            }
            Ok(())
        }

        fn transfer_in_place(&mut self, _words: &mut [u8]) -> Result<(), Self::Error> {
            Ok(())
        }

        fn flush(&mut self) -> Result<(), Self::Error> {
            self.flushes += 1;
            Ok(())
        }
    }

    #[derive(Default)]
    struct MockPin {
        high: bool,
        high_calls: u8,
        low_calls: u8,
    }

    impl DigitalErrorType for MockPin {
        type Error = Infallible;
    }

    impl OutputPin for MockPin {
        fn set_low(&mut self) -> Result<(), Self::Error> {
            self.high = false;
            self.low_calls += 1;
            Ok(())
        }

        fn set_high(&mut self) -> Result<(), Self::Error> {
            self.high = true;
            self.high_calls += 1;
            Ok(())
        }
    }

    #[test]
    fn spi_transport_switches_dc_and_brackets_each_payload_with_cs() {
        let mut transport =
            SpiTransport::new(MockSpi::default(), MockPin::default(), MockPin::default()).unwrap();

        let command_report = transport.write_commands(&[0xae, 0xd5]).unwrap();
        let data_report = transport.write_data(&[0x12, 0x34, 0x56]).unwrap();
        let parts = transport.release();

        assert_eq!(command_report.payload_bytes, 2);
        assert_eq!(data_report.payload_bytes, 3);
        assert_eq!(parts.spi.writes.len(), 2);
        assert_eq!(parts.spi.writes[0], [0xae, 0xd5]);
        assert_eq!(parts.spi.writes[1], [0x12, 0x34, 0x56]);
        assert_eq!(parts.spi.flushes, 2);
        assert!(parts.data_command.high);
        assert_eq!(parts.data_command.low_calls, 2);
        assert_eq!(parts.data_command.high_calls, 1);
        assert!(parts.chip_select.high);
        assert_eq!(parts.chip_select.low_calls, 2);
        assert_eq!(parts.chip_select.high_calls, 3);
    }

    #[test]
    fn chip_select_guard_releases_pin_when_dropped() {
        let mut chip_select = MockPin::default();
        {
            let _guard = ChipSelectGuard::select(&mut chip_select).unwrap();
        }

        assert!(chip_select.high);
        assert_eq!(chip_select.low_calls, 1);
        assert_eq!(chip_select.high_calls, 1);
    }
}
