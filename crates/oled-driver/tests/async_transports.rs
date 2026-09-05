#![cfg(feature = "async")]

use core::{
    convert::Infallible,
    future::Future,
    pin::pin,
    task::{Context, Poll, Waker},
};
use std::{cell::RefCell, rc::Rc};

use embedded_hal::{digital, i2c, spi};
use oled_driver::{
    AsyncTransport, BufferedDisplay, DISPLAY_BYTES, FullBuffer, I2cTransport, SpiTransport,
    SpiTransportError, Ssd1306,
};

fn ready<F: Future>(future: F) -> F::Output {
    match pin!(future)
        .as_mut()
        .poll(&mut Context::from_waker(Waker::noop()))
    {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("mock operation unexpectedly suspended"),
    }
}

#[derive(Debug, PartialEq)]
struct I2cTransaction {
    address: u8,
    writes: Vec<Vec<u8>>,
}

#[derive(Clone, Default)]
struct MockI2c(Rc<RefCell<Vec<I2cTransaction>>>);

impl i2c::ErrorType for MockI2c {
    type Error = Infallible;
}

impl embedded_hal_async::i2c::I2c for MockI2c {
    async fn transaction(
        &mut self,
        address: u8,
        operations: &mut [i2c::Operation<'_>],
    ) -> Result<(), Self::Error> {
        let writes = operations
            .iter()
            .map(|operation| match operation {
                i2c::Operation::Write(bytes) => bytes.to_vec(),
                i2c::Operation::Read(_) => panic!("OLED must not read the bus"),
            })
            .collect();
        self.0.borrow_mut().push(I2cTransaction { address, writes });
        Ok(())
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
struct BusFailure;

impl spi::Error for BusFailure {
    fn kind(&self) -> spi::ErrorKind {
        spi::ErrorKind::Other
    }
}

#[derive(Clone, Copy, Default)]
enum WriteOutcome {
    #[default]
    Ready,
    Fail,
    Pending,
}

#[derive(Debug, PartialEq)]
enum Event {
    Dc(bool),
    Cs(bool),
    Write(Vec<u8>),
    Flush,
}

#[derive(Default)]
struct SpiState {
    dc_high: bool,
    cs_high: bool,
    events: Vec<Event>,
    outcome: WriteOutcome,
}

struct MockSpi(Rc<RefCell<SpiState>>);

impl spi::ErrorType for MockSpi {
    type Error = BusFailure;
}

impl embedded_hal_async::spi::SpiBus for MockSpi {
    async fn read(&mut self, _: &mut [u8]) -> Result<(), Self::Error> {
        panic!("write-only display")
    }
    async fn transfer(&mut self, _: &mut [u8], _: &[u8]) -> Result<(), Self::Error> {
        panic!("write-only display")
    }
    async fn transfer_in_place(&mut self, _: &mut [u8]) -> Result<(), Self::Error> {
        panic!("write-only display")
    }

    async fn write(&mut self, bytes: &[u8]) -> Result<(), Self::Error> {
        let outcome = {
            let mut state = self.0.borrow_mut();
            assert!(!state.cs_high, "CS must be asserted during write");
            state.events.push(Event::Write(bytes.to_vec()));
            state.outcome
        };
        match outcome {
            WriteOutcome::Ready => Ok(()),
            WriteOutcome::Fail => Err(BusFailure),
            WriteOutcome::Pending => core::future::pending().await,
        }
    }

    async fn flush(&mut self) -> Result<(), Self::Error> {
        let mut state = self.0.borrow_mut();
        assert!(!state.cs_high, "CS must remain asserted through flush");
        state.events.push(Event::Flush);
        Ok(())
    }
}

struct MockPin {
    state: Rc<RefCell<SpiState>>,
    is_cs: bool,
}

impl digital::ErrorType for MockPin {
    type Error = Infallible;
}

impl digital::OutputPin for MockPin {
    fn set_low(&mut self) -> Result<(), Self::Error> {
        self.set(false);
        Ok(())
    }
    fn set_high(&mut self) -> Result<(), Self::Error> {
        self.set(true);
        Ok(())
    }
}

impl MockPin {
    fn set(&mut self, high: bool) {
        let mut state = self.state.borrow_mut();
        if self.is_cs {
            state.cs_high = high;
            state.events.push(Event::Cs(high));
        } else {
            state.dc_high = high;
            state.events.push(Event::Dc(high));
        }
    }
}

fn spi_transport(state: &Rc<RefCell<SpiState>>) -> SpiTransport<MockSpi, MockPin, MockPin> {
    SpiTransport::new(
        MockSpi(state.clone()),
        MockPin {
            state: state.clone(),
            is_cs: false,
        },
        MockPin {
            state: state.clone(),
            is_cs: true,
        },
    )
    .unwrap()
}

#[test]
fn i2c_prefix_and_payload_share_one_transaction() {
    let bus = MockI2c::default();
    let mut transport = I2cTransport::new(bus.clone(), 0x3c).unwrap();
    ready(transport.write_commands(&[0xae, 0xaf])).unwrap();
    ready(transport.write_data(&[0x12, 0x34])).unwrap();
    ready(transport.write_commands(&[])).unwrap();
    ready(transport.write_data(&[])).unwrap();
    assert_eq!(
        *bus.0.borrow(),
        [
            I2cTransaction {
                address: 0x3c,
                writes: vec![vec![0x00], vec![0xae, 0xaf]]
            },
            I2cTransaction {
                address: 0x3c,
                writes: vec![vec![0x40], vec![0x12, 0x34]]
            },
        ]
    );
}

#[test]
fn spi_uses_dc_without_i2c_prefix_and_holds_cs_through_flush() {
    let state = Rc::new(RefCell::new(SpiState::default()));
    let mut transport = spi_transport(&state);
    state.borrow_mut().events.clear();
    ready(transport.write_commands(&[0xae, 0xaf])).unwrap();
    ready(transport.write_data(&[0x12, 0x34])).unwrap();
    ready(transport.write_data(&[])).unwrap();
    assert_eq!(
        state.borrow().events,
        [
            Event::Dc(false),
            Event::Cs(false),
            Event::Write(vec![0xae, 0xaf]),
            Event::Flush,
            Event::Cs(true),
            Event::Dc(true),
            Event::Cs(false),
            Event::Write(vec![0x12, 0x34]),
            Event::Flush,
            Event::Cs(true),
        ]
    );
}

#[test]
fn spi_error_releases_cs() {
    let state = Rc::new(RefCell::new(SpiState {
        outcome: WriteOutcome::Fail,
        ..Default::default()
    }));
    let mut transport = spi_transport(&state);
    assert_eq!(
        ready(transport.write_data(&[0x55])),
        Err(SpiTransportError::Spi(BusFailure))
    );
    assert!(state.borrow().cs_high);
    assert!(!state.borrow().events.contains(&Event::Flush));
}

#[test]
fn cancelling_the_actual_async_spi_future_releases_cs() {
    let state = Rc::new(RefCell::new(SpiState {
        outcome: WriteOutcome::Pending,
        ..Default::default()
    }));
    let mut transport = spi_transport(&state);
    {
        let mut operation = pin!(transport.write_data(&[0x55]));
        assert!(
            operation
                .as_mut()
                .poll(&mut Context::from_waker(Waker::noop()))
                .is_pending()
        );
        assert!(!state.borrow().cs_high);
    }
    assert!(
        state.borrow().cs_high,
        "timeout cancellation must deselect the panel"
    );
    state.borrow_mut().outcome = WriteOutcome::Ready;
    ready(transport.write_data(&[0xaa])).unwrap();
}

#[test]
fn i2c_dirty_flush_sends_only_changed_bytes_and_skips_static_scene() {
    let bus = MockI2c::default();
    let transport = I2cTransport::new(bus.clone(), 0x3c).unwrap();
    let mut memory = [0; DISPLAY_BYTES];
    let mut display = BufferedDisplay::new(
        Ssd1306::default(),
        transport,
        FullBuffer::new(&mut memory).unwrap(),
    )
    .unwrap();
    ready(display.init_async()).unwrap();
    bus.0.borrow_mut().clear();
    display
        .buffer_mut()
        .blit_native(12, 3, 1, 1, &[0x55])
        .unwrap();
    let report = ready(display.flush_async()).unwrap();
    assert_eq!(report.data_bytes, 1);
    assert_eq!(
        *bus.0.borrow(),
        [
            I2cTransaction {
                address: 0x3c,
                writes: vec![vec![0x00], vec![0x21, 12, 12, 0x22, 3, 3]]
            },
            I2cTransaction {
                address: 0x3c,
                writes: vec![vec![0x40], vec![0x55]]
            },
        ]
    );
    bus.0.borrow_mut().clear();
    assert_eq!(ready(display.flush_async()).unwrap().data_bytes, 0);
    assert!(bus.0.borrow().is_empty());
}

#[test]
fn spi_full_flush_uses_one_contiguous_frame_with_the_same_controller() {
    let state = Rc::new(RefCell::new(SpiState::default()));
    let transport = spi_transport(&state);
    let mut memory = [0; DISPLAY_BYTES];
    let mut display = BufferedDisplay::new(
        Ssd1306::default(),
        transport,
        FullBuffer::new(&mut memory).unwrap(),
    )
    .unwrap();
    ready(display.init_async()).unwrap();
    state.borrow_mut().events.clear();
    display
        .buffer_mut()
        .blit_native(12, 3, 1, 1, &[0x55])
        .unwrap();
    let report = ready(display.flush_full_async()).unwrap();
    assert_eq!(usize::try_from(report.data_bytes).unwrap(), DISPLAY_BYTES);
    assert_eq!(report.regions, 1);
    let state = state.borrow();
    let writes: Vec<_> = state
        .events
        .iter()
        .filter_map(|event| match event {
            Event::Write(bytes) => Some(bytes),
            _ => None,
        })
        .collect();
    assert_eq!(writes.len(), 2);
    assert_eq!(writes[0], &[0x21, 0, 127, 0x22, 0, 7]);
    assert_eq!(writes[1].len(), DISPLAY_BYTES);
    assert_eq!(writes[1][3 * 128 + 12], 0x55);
}
