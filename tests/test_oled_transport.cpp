#include "platform/oled_transport.hpp"
#include "display/ssd1306.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(const bool condition, const char* expression, const int line)
{
    if (!condition) {
        throw std::runtime_error(std::string(expression) + " at line " + std::to_string(line));
    }
}
#define CHECK(expression) require((expression), #expression, __LINE__)

enum class Outcome { success, error, timeout, start_error, immediate_success, immediate_error };
struct Transfer {
    bool data;
    std::uint32_t count;
    std::uint32_t source;
    std::vector<std::uint8_t> bytes;
};
struct FakeState {
    std::uint32_t tick{0U};
    std::uint32_t ipsr{0U};
    std::uint32_t primask{0U};
    std::uint32_t basepri{0U};
    bool advance_tick_on_read{false};
    BaseType_t scheduler{taskSCHEDULER_RUNNING};
    bool cs_high{true};
    bool dc_high{false};
    bool reset_high{true};
    bool dma_active{false};
    bool pending{false};
    Outcome outcome{Outcome::success};
    Outcome pending_outcome{Outcome::success};
    std::uint32_t fail_transfer{0U};
    bool fail_data{false};
    std::uint32_t wire_delays{0U};
    std::uint32_t wire_remaining{0U};
    std::uint32_t task_gives{0U};
    std::uint32_t isr_gives{0U};
    std::uint32_t selected_spe_changes{0U};
    std::uint32_t early_releases{0U};
    std::uint32_t dma_stops{0U};
    std::uint32_t recoveries{0U};
    std::uint32_t flag_reads{0U};
    bool fail_recovery{false};
    HAL_StatusTypeDef ready_status{HAL_OK};
    SPI_HandleTypeDef* spi{nullptr};
    I2C_HandleTypeDef* i2c{nullptr};
    DMA_HandleTypeDef* dma{nullptr};
    void (*saved_dma_callback)(DMA_HandleTypeDef*){nullptr};
    SemaphoreHandle_t semaphore{nullptr};
    std::array<bool, 16U> irq_enabled{};
    std::array<bool, 16U> irq_pending{};
    std::vector<std::pair<std::uint32_t, bool>> reset_edges;
    std::vector<Transfer> transfers;
} state;

SPI_TypeDef spi_registers{};
I2C_TypeDef i2c_registers{};
DMA_HandleTypeDef dma_handle{};
SPI_HandleTypeDef spi_bus{&spi_registers, &dma_handle};
I2C_HandleTypeDef i2c_bus{&i2c_registers, &dma_handle};

void resetFake()
{
    state = FakeState{};
    spi_registers = SPI_TypeDef{};
    spi_registers.SR = SPI_FLAG_TXE;
    dma_handle = DMA_HandleTypeDef{};
    state.spi = &spi_bus;
    state.i2c = &i2c_bus;
    state.dma = &dma_handle;
    state.irq_enabled.fill(true);
}

gamebox::platform::OledBus& selectedBus()
{
#if GAMEBOX_OLED_SPI
    return spi_bus;
#else
    return i2c_bus;
#endif
}

void completePending(const bool from_isr)
{
    CHECK(state.pending);
    state.pending = false;
    state.dma_active = false;
    const bool success = state.pending_outcome != Outcome::error &&
                         state.pending_outcome != Outcome::immediate_error;
    state.ipsr = from_isr ? 19U : 0U;
#if GAMEBOX_OLED_SPI
    state.wire_remaining = success ? state.wire_delays : 0U;
    spi_registers.SR = state.wire_remaining != 0U ? SPI_FLAG_BSY : SPI_FLAG_TXE;
    const auto callback = success ? dma_handle.XferCpltCallback : dma_handle.XferErrorCallback;
    CHECK(callback != nullptr);
    callback(&dma_handle);
#else
    if (success) { HAL_I2C_MasterTxCpltCallback(&i2c_bus); }
    else { HAL_I2C_ErrorCallback(&i2c_bus); }
#endif
    state.ipsr = 0U;
}

HAL_StatusTypeDef beginTransfer()
{
    state.pending_outcome = state.outcome;
    if (state.fail_transfer != 0U && state.transfers.size() == state.fail_transfer) {
        state.pending_outcome = Outcome::start_error;
    }
    if (state.fail_data && state.transfers.back().data) {
        state.pending_outcome = Outcome::start_error;
    }
    if (state.pending_outcome == Outcome::start_error) { return HAL_ERROR; }
    state.dma_active = true;
    state.pending = true;
#if !GAMEBOX_OLED_SPI
    if (state.pending_outcome == Outcome::immediate_success ||
        state.pending_outcome == Outcome::immediate_error) {
        completePending(false);
    }
#endif
    return HAL_OK;
}

struct Fixture {
    gamebox::platform::OledTransport transport;
    Fixture() : transport(selectedBus())
    {
        resetFake();
        CHECK(transport.initialize());
    }
    ~Fixture() { transport.deinitialize(); }
};

void testTransferBoundaries()
{
    Fixture fixture;
#if GAMEBOX_OLED_SPI
    std::array<std::uint8_t, 1024U> bytes{};
    state.wire_delays = 3U;
#else
    std::array<std::uint8_t, 128U> bytes{};
#endif
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(index);
    }
    CHECK(fixture.transport.writeData(bytes.data(), static_cast<std::uint16_t>(bytes.size())));
    CHECK(state.transfers.size() == 1U);
    CHECK(state.transfers[0].data);
#if GAMEBOX_OLED_SPI
    CHECK(state.transfers[0].count == 1024U);
    CHECK(state.transfers[0].source ==
          static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bytes.data())));
    CHECK(state.tick >= 3U);
    CHECK(state.flag_reads >= 4U);
    CHECK(state.early_releases == 0U);
    CHECK(state.selected_spe_changes == 0U);
    CHECK(state.cs_high);
    CHECK((spi_registers.CR2 & SPI_CR2_TXDMAEN) == 0U);
#else
    CHECK(state.transfers[0].count == 129U);
    CHECK(state.transfers[0].bytes.front() == 0x40U);
    CHECK(std::vector<std::uint8_t>(state.transfers[0].bytes.begin() + 1,
                                   state.transfers[0].bytes.end()) ==
          std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
#endif
    CHECK(!state.dma_active);
    CHECK(state.isr_gives == 1U);
    CHECK(state.task_gives == 0U);
    CHECK(fixture.transport.dmaTransferCount() == 1U);
    CHECK(fixture.transport.errorCount() == 0U);
    const std::uint8_t command[] = {0x81U, 0xCFU};
    CHECK(fixture.transport.writeCommands(command, sizeof(command)));
    CHECK(!state.transfers.back().data);
#if !GAMEBOX_OLED_SPI
    CHECK(state.transfers.back().bytes == std::vector<std::uint8_t>({0x00U, 0x81U, 0xCFU}));
#else
    CHECK(state.selected_spe_changes == 0U);
#endif
}

void injectLateCallback()
{
    state.ipsr = 19U;
#if GAMEBOX_OLED_SPI
    CHECK(state.saved_dma_callback != nullptr);
    state.saved_dma_callback(&dma_handle);
#else
    HAL_I2C_MasterTxCpltCallback(&i2c_bus);
#endif
    state.ipsr = 0U;
}

void testFailuresAndRecovery()
{
    for (const Outcome outcome : {Outcome::timeout, Outcome::error, Outcome::start_error}) {
        Fixture fixture;
        state.outcome = outcome;
        const std::uint8_t bytes[] = {1U, 2U, 3U};
        CHECK(!fixture.transport.writeData(bytes, sizeof(bytes)));
        CHECK(!state.dma_active);
        CHECK(state.dma_stops > 0U);
        CHECK(fixture.transport.errorCount() == 1U);
        CHECK(fixture.transport.timeoutCount() == (outcome == Outcome::timeout ? 1U : 0U));
#if GAMEBOX_OLED_SPI
        CHECK(state.cs_high);
        CHECK((spi_registers.CR1 & SPI_CR1_SPE) == 0U);
        CHECK((spi_registers.CR2 & SPI_CR2_TXDMAEN) == 0U);
        CHECK(state.selected_spe_changes == 0U);
#endif
        const auto gives = state.isr_gives + state.task_gives;
        injectLateCallback();
        CHECK(state.isr_gives + state.task_gives == gives);
        // Recovery must discard a semaphore token left by an earlier IRQ.
        CHECK(xSemaphoreGive(state.semaphore) == pdTRUE);
        CHECK(fixture.transport.recoverBus());
        CHECK(state.semaphore->token == 0);
        CHECK(state.recoveries == 1U);
        CHECK(!state.dma_active);
#if GAMEBOX_OLED_SPI
        CHECK(state.cs_high);
        CHECK(state.irq_enabled[DMA1_Channel3_IRQn]);
        CHECK(state.irq_enabled[SPI1_IRQn]);
#else
        CHECK(state.irq_enabled[DMA1_Channel6_IRQn]);
        CHECK(state.irq_enabled[I2C1_EV_IRQn]);
        CHECK(state.irq_enabled[I2C1_ER_IRQn]);
#endif
        state.outcome = Outcome::success;
        CHECK(fixture.transport.writeData(bytes, sizeof(bytes)));
    }
}

void testPreparationAndInvalidInputs()
{
    Fixture fixture;
    const std::uint8_t value = 0xAFU;
    CHECK(!fixture.transport.writeData(nullptr, 1U));
    CHECK(!fixture.transport.writeData(&value, 0U));
#if !GAMEBOX_OLED_SPI
    std::array<std::uint8_t, 129U> oversized{};
    CHECK(!fixture.transport.writeData(oversized.data(), static_cast<std::uint16_t>(oversized.size())));
#endif
    CHECK(state.transfers.empty());
    state.primask = 1U;
    CHECK(!fixture.transport.prepare());
    state.primask = 0U;
    state.basepri = 0x50U;
    CHECK(!fixture.transport.prepare());
    CHECK(!fixture.transport.writeCommands(&value, 1U));
    state.basepri = 0U;
    state.scheduler = taskSCHEDULER_NOT_STARTED;
    CHECK(fixture.transport.prepare());
#if GAMEBOX_OLED_SPI
    CHECK(state.reset_edges.size() == 3U);
    CHECK(state.reset_edges[0] == std::make_pair(0U, true));
    CHECK(state.reset_edges[1] == std::make_pair(1U, false));
    CHECK(state.reset_edges[2] == std::make_pair(11U, true));
    CHECK(state.tick == 21U);
    CHECK(state.reset_high);
    CHECK(state.cs_high);
#endif
    CHECK(fixture.transport.writeCommands(&value, 1U));
    CHECK(fixture.transport.dmaTransferCount() == 0U);
}

void testBusyTimeoutOrSynchronousCallback()
{
    Fixture fixture;
    const std::uint8_t value = 0xAEU;
#if GAMEBOX_OLED_SPI
    state.wire_delays = 100U;
    CHECK(!fixture.transport.writeCommands(&value, 1U));
    CHECK(fixture.transport.timeoutCount() == 1U);
    CHECK(fixture.transport.errorCount() == 1U);
    CHECK(!state.dma_active);
    CHECK(state.cs_high);
    CHECK(state.selected_spe_changes == 0U);
    CHECK(state.tick == 20U);
#else
    state.outcome = Outcome::immediate_success;
    CHECK(fixture.transport.writeCommands(&value, 1U));
    CHECK(state.task_gives == 1U);
    CHECK(state.isr_gives == 0U);
    state.outcome = Outcome::immediate_error;
    CHECK(!fixture.transport.writeCommands(&value, 1U));
    CHECK(state.task_gives == 2U);
    CHECK(state.isr_gives == 0U);
    CHECK(!state.dma_active);
    CHECK(fixture.transport.errorCount() == 1U);
#endif
}

void testTickWraparoundOrMissingDevice()
{
#if GAMEBOX_OLED_SPI
    Fixture fixture;
    state.tick = UINT32_MAX - 9U;
    state.wire_delays = 100U;
    const std::uint8_t value = 0xAEU;
    CHECK(!fixture.transport.writeCommands(&value, 1U));
    CHECK(fixture.transport.timeoutCount() == 1U);
    CHECK(state.tick == 10U);
    CHECK(!state.dma_active);
    CHECK(fixture.transport.recoverBus());
    // A missing TXE at boot must use the running HAL tick instead of spinning
    // forever. Tick advancement models the separate TIM4 interrupt here.
    state.scheduler = taskSCHEDULER_NOT_STARTED;
    state.advance_tick_on_read = true;
    spi_registers.SR = 0U;
    CHECK(!fixture.transport.writeCommands(&value, 1U));
    CHECK(fixture.transport.timeoutCount() == 2U);
    CHECK(state.cs_high);
#else
    resetFake();
    state.scheduler = taskSCHEDULER_NOT_STARTED;
    gamebox::display::Ssd1306 display(selectedBus());
    state.basepri = 0x50U;
    CHECK(!display.prepare());
    CHECK(!display.initialize());
    state.basepri = 0U;
    state.ready_status = HAL_ERROR;
    CHECK(display.prepare()); // Missing OLED must not stop application startup.
    CHECK(display.initialize());
    state.scheduler = taskSCHEDULER_RUNNING;
    CHECK(!display.setContrast(0x2AU));
    CHECK(!display.setEnabled(false));
    CHECK(state.transfers.empty());
    gamebox::display::Canvas canvas;
    canvas.clear();
    CHECK(!display.flush(canvas));
    CHECK(state.recoveries == 0U);
    state.tick += 50U;
    state.fail_recovery = true;
    CHECK(!display.flush(canvas));
    CHECK(state.recoveries == 1U);
    state.tick += 99U;
    CHECK(!display.flush(canvas));
    CHECK(state.recoveries == 1U);
    state.tick += 1U;
    state.fail_recovery = false;
    state.ready_status = HAL_OK;
    CHECK(display.flush(canvas));
    CHECK(state.recoveries == 2U);
    CHECK(state.transfers[0].bytes[14U] == 0x2AU);
    CHECK(state.transfers[0].bytes.back() == 0xAEU);
    CHECK(canvas.dirtyPages() == 0U);
    CHECK(display.deinitialize());
    const auto gives = state.isr_gives + state.task_gives;
    injectLateCallback();
    CHECK(state.isr_gives + state.task_gives == gives);
#endif
}

std::size_t dataTransferCount()
{
    std::size_t result = 0U;
    for (const auto& transfer : state.transfers) { if (transfer.data) { ++result; } }
    return result;
}

void testControllerCacheAndRecovery()
{
    resetFake();
    state.scheduler = taskSCHEDULER_NOT_STARTED;
    gamebox::display::Ssd1306 display(selectedBus());
    CHECK(display.prepare());
    CHECK(display.initialize());
    state.scheduler = taskSCHEDULER_RUNNING;
    gamebox::display::Canvas canvas;
    canvas.clear();
    CHECK(display.flush(canvas));
    CHECK(canvas.dirtyPages() == 0U);
#if GAMEBOX_OLED_SPI
    CHECK(dataTransferCount() == 1U);
    CHECK(state.transfers.back().count == 1024U);
#else
    CHECK(dataTransferCount() == 8U);
    CHECK(state.transfers.back().count == 129U);
#endif
    const auto written = state.transfers.size();
    CHECK(display.flush(canvas));
    canvas.clear(); // Dirty metadata alone must not force an identical frame.
    CHECK(display.flush(canvas));
    CHECK(state.transfers.size() == written);
    CHECK(canvas.dirtyPages() == 0U);

    CHECK(display.setContrast(0x5AU));
    CHECK(display.setEnabled(false));
    canvas.pixel(5, 10);
    state.fail_transfer = static_cast<std::uint32_t>(state.transfers.size() + 2U);
    CHECK(!display.flush(canvas)); // Fail the data transfer after its address.
    CHECK(canvas.dirtyPages() != 0U);
    CHECK(!state.dma_active);
    const auto before_retry = state.transfers.size();
    CHECK(!display.flush(canvas)); // Retry is bounded by the backoff timer.
    CHECK(state.transfers.size() == before_retry);
    state.tick += 100U;
    state.fail_transfer = 0U;
    const auto before_data = dataTransferCount();
    CHECK(display.flush(canvas));
    CHECK(canvas.dirtyPages() == 0U);
    CHECK(state.recoveries == 1U);
#if GAMEBOX_OLED_SPI
    CHECK(dataTransferCount() == before_data + 1U);
    CHECK(state.transfers.back().count == 1024U);
    CHECK(state.selected_spe_changes == 0U);
#else
    CHECK(dataTransferCount() == before_data + 8U);
    const auto& restored = state.transfers[before_retry].bytes;
    CHECK(restored.size() == 26U);
    CHECK(restored[13U] == 0x81U);
    CHECK(restored[14U] == 0x5AU);
    CHECK(restored.back() == 0xAEU);
#endif
    CHECK(display.deinitialize());
}

void testControllerDataFailureBackoff()
{
    resetFake();
    state.scheduler = taskSCHEDULER_NOT_STARTED;
    gamebox::display::Ssd1306 display(selectedBus());
    CHECK(display.prepare());
    CHECK(display.initialize());
    state.scheduler = taskSCHEDULER_RUNNING;
    gamebox::display::Canvas canvas;
    canvas.clear();
    CHECK(display.flush(canvas));
    canvas.pixel(1, 1);
    state.fail_data = true;
    CHECK(!display.flush(canvas));

    // Successful short init/address commands do not prove a recovered link.
    // Each failed framebuffer replay must increase the next retry interval.
    for (const std::uint32_t delay : {50U, 100U, 200U}) {
        const auto written = state.transfers.size();
        const auto recovered = state.recoveries;
        state.tick += delay - 1U;
        CHECK(!display.flush(canvas));
        CHECK(state.transfers.size() == written);
        CHECK(state.recoveries == recovered);
        state.tick += 1U;
        CHECK(!display.flush(canvas));
        CHECK(state.recoveries == recovered + 1U);
        CHECK(state.transfers.back().data);
        CHECK(canvas.dirtyPages() != 0U);
        CHECK(!state.dma_active);
    }
    state.fail_data = false;
    state.tick += 399U;
    CHECK(!display.flush(canvas));
    state.tick += 1U;
    CHECK(display.flush(canvas));
    CHECK(canvas.dirtyPages() == 0U);

    // Only a complete replay resets the backoff to its initial 50 ms.
    canvas.pixel(2, 2);
    state.fail_data = true;
    CHECK(!display.flush(canvas));
    const auto recovered = state.recoveries;
    state.fail_data = false;
    state.tick += 49U;
    CHECK(!display.flush(canvas));
    CHECK(state.recoveries == recovered);
    state.tick += 1U;
    CHECK(display.flush(canvas));
    CHECK(state.recoveries == recovered + 1U);
    CHECK(display.deinitialize());
}
} // namespace

extern "C" {
GPIO_TypeDef fake_gpio{};

void fake_set_bit(std::uint32_t* value, const std::uint32_t bits)
{
    if (value == &spi_registers.CR1 && !state.cs_high &&
        ((*value ^ (*value | bits)) & SPI_CR1_SPE) != 0U) { ++state.selected_spe_changes; }
    *value |= bits;
}
void fake_clear_bit(std::uint32_t* value, const std::uint32_t bits)
{
    if (value == &spi_registers.CR1 && !state.cs_high &&
        ((*value ^ (*value & ~bits)) & SPI_CR1_SPE) != 0U) { ++state.selected_spe_changes; }
    *value &= ~bits;
}
std::uint32_t fake_spi_get_flag(SPI_HandleTypeDef* bus, const std::uint32_t flag)
{
    CHECK(!state.cs_high);
    ++state.flag_reads;
    return (bus->Instance->SR & flag) != 0U ? 1U : 0U;
}
std::uint32_t HAL_GetTick()
{
    const auto tick = state.tick;
    if (state.advance_tick_on_read) { ++state.tick; }
    return tick;
}
void HAL_Delay(const std::uint32_t milliseconds) { state.tick += milliseconds; }
std::uint32_t __get_PRIMASK() { return state.primask; }
std::uint32_t __get_BASEPRI() { return state.basepri; }
std::uint32_t __get_IPSR() { return state.ipsr; }
void HAL_GPIO_WritePin(GPIO_TypeDef*, const std::uint16_t pin, const GPIO_PinState value)
{
    if (pin == OLED_CS_Pin) {
        if (value == GPIO_PIN_SET && !state.cs_high &&
            (state.dma_active || (spi_registers.SR & SPI_FLAG_BSY) != 0U)) {
            ++state.early_releases;
        }
        state.cs_high = value == GPIO_PIN_SET;
    } else if (pin == OLED_DC_Pin) { state.dc_high = value == GPIO_PIN_SET; }
    else if (pin == OLED_RESET_Pin) {
        state.reset_high = value == GPIO_PIN_SET;
        state.reset_edges.emplace_back(state.tick, state.reset_high);
    }
}
void HAL_NVIC_DisableIRQ(const int irq) { state.irq_enabled[static_cast<std::size_t>(irq)] = false; }
void HAL_NVIC_EnableIRQ(const int irq) { state.irq_enabled[static_cast<std::size_t>(irq)] = true; }
void HAL_NVIC_ClearPendingIRQ(const int irq) { state.irq_pending[static_cast<std::size_t>(irq)] = false; }
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef* dma, const std::uint32_t source,
                                 const std::uint32_t destination, const std::uint32_t count)
{
    CHECK(!state.cs_high);
    CHECK((spi_registers.CR1 & SPI_CR1_SPE) != 0U);
    CHECK(destination == static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&spi_registers.DR)));
    state.saved_dma_callback = dma->XferCpltCallback;
    state.transfers.push_back({state.dc_high, count, source, {}});
    return beginTransfer();
}
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef* dma)
{
    state.dma_active = false;
    state.pending = false;
    ++state.dma_stops;
    *dma = DMA_HandleTypeDef{};
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef*, const std::uint16_t address,
                                     const std::uint32_t, const std::uint32_t)
{
    CHECK(address == 0x78U);
    return state.ready_status;
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef*, const std::uint16_t address,
                                       std::uint8_t* data, const std::uint16_t count,
                                       const std::uint32_t)
{
    CHECK(address == 0x78U);
    state.transfers.push_back({data[0] == 0x40U, count, 0U, {data, data + count}});
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit_DMA(I2C_HandleTypeDef*, const std::uint16_t address,
                                           std::uint8_t* data, const std::uint16_t count)
{
    CHECK(address == 0x78U);
    state.transfers.push_back({data[0] == 0x40U, count, 0U, {data, data + count}});
    return beginTransfer();
}
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef* bus)
{
    return HAL_DMA_DeInit(bus->hdmatx);
}
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef*)
{
    CHECK(!state.dma_active);
    ++state.recoveries;
    if (state.fail_recovery) { return HAL_ERROR; }
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
    return HAL_OK;
}
HAL_StatusTypeDef OLED_SPI_Reinitialize()
{
    CHECK(!state.dma_active);
    CHECK(state.cs_high);
    ++state.recoveries;
    if (state.fail_recovery) { return HAL_ERROR; }
    spi_registers = SPI_TypeDef{};
    spi_registers.SR = SPI_FLAG_TXE;
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);
    return HAL_OK;
}
void fake_i2c_force_reset() { CHECK(!state.dma_active); }
void fake_i2c_release_reset() {}
void fake_yield_from_isr(const BaseType_t) { CHECK(state.ipsr != 0U); }
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage)
{
    storage->token = 0;
    state.semaphore = storage;
    return storage;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, const TickType_t ticks)
{
    if (semaphore->token != 0) { semaphore->token = 0; return pdTRUE; }
    if (ticks == 0U) { return pdFALSE; }
    if (state.pending && state.pending_outcome != Outcome::timeout) {
        completePending(true);
        if (semaphore->token != 0) { semaphore->token = 0; return pdTRUE; }
    }
    state.tick += ticks;
    return pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    CHECK(state.ipsr == 0U);
    ++state.task_gives;
    semaphore->token = 1;
    return pdTRUE;
}
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t* woken)
{
    CHECK(state.ipsr != 0U);
    ++state.isr_gives;
    semaphore->token = 1;
    *woken = pdTRUE;
    return pdTRUE;
}
BaseType_t xTaskGetSchedulerState() { return state.scheduler; }
void vTaskDelay(const TickType_t ticks)
{
    state.tick += ticks;
    if (state.wire_remaining != 0U) {
        CHECK(!state.cs_high);
        --state.wire_remaining;
        spi_registers.SR = static_cast<std::uint32_t>(SPI_FLAG_TXE) |
            (state.wire_remaining != 0U ? static_cast<std::uint32_t>(SPI_FLAG_BSY) : 0U);
    }
}
} // extern "C"

int main()
{
    try {
        testTransferBoundaries();
        testFailuresAndRecovery();
        testPreparationAndInvalidInputs();
        testBusyTimeoutOrSynchronousCallback();
        testTickWraparoundOrMissingDevice();
        testControllerCacheAndRecovery();
        testControllerDataFailureBackoff();
        std::cout << (GAMEBOX_OLED_SPI ? "SPI" : "I2C")
                  << " transport/controller tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << (GAMEBOX_OLED_SPI ? "SPI" : "I2C") << ": " << exception.what() << '\n';
        return 1;
    }
}
