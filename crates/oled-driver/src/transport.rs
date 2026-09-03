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
