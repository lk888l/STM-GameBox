# ADR 0004：为 STM32F1 RTC 建立持久化日期锚点

状态：Accepted

STM32F1 RTC 硬件只有 32-bit 秒计数器；STM32 HAL 把年、月、日保存在 `RTC_HandleTypeDef` 的 SRAM 字段中。MCU 复位后秒计数和 backup domain 可以继续运行，但 HAL 日期会重置为 2000-01-01。直接调用 `HAL_RTC_SetDate` 还会丢弃计数器中累计的整天，无法作为恢复步骤。

`RtcCalendar` 因此在任何 HAL 日期读取前完成恢复：读取 DR4–DR6 中带 magic/checkword 的日期锚点，稳定读取原始 RTC counter，把整天数加入锚点，然后用“日期 + 当日秒数”重新规范化 HAL 状态。运行时日期变化后只在编码值改变时写 backup register，避免每帧写入。

编解码、闰年、月份长度、跨年和 2099 上界由纯 `CalendarCodec` 实现并在主机测试。可表示范围与 HAL 一致，为 2000-01-01 至 2099-12-31；越界时饱和到上限。
