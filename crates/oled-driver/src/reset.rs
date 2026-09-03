//! Optional SSD1306 hardware-reset helpers.

#[cfg(any(feature = "blocking", feature = "async"))]
use embedded_hal::digital::OutputPin;

/// Reset a panel using a blocking output pin and delay provider.
#[cfg(feature = "blocking")]
pub fn reset_blocking<P, D>(reset: &mut P, delay: &mut D) -> Result<(), P::Error>
where
    P: OutputPin,
    D: embedded_hal::delay::DelayNs,
{
    reset.set_high()?;
    delay.delay_ms(1);
    reset.set_low()?;
    delay.delay_ms(10);
    reset.set_high()?;
    delay.delay_ms(10);
    Ok(())
}

/// Reset a panel using an output pin and asynchronous delay provider.
#[cfg(feature = "async")]
pub async fn reset_async<P, D>(reset: &mut P, delay: &mut D) -> Result<(), P::Error>
where
    P: OutputPin,
    D: embedded_hal_async::delay::DelayNs,
{
    reset.set_high()?;
    delay.delay_ms(1).await;
    reset.set_low()?;
    delay.delay_ms(10).await;
    reset.set_high()?;
    delay.delay_ms(10).await;
    Ok(())
}
