# 按键事件规范

## 采样模型

InputTask 每 5 ms 只读取一次 `GPIOB->IDR`，将 8 个低有效物理引脚映射为一个逻辑 bitmask。所有按钮使用相同的纯状态机；不使用 EXTI，也没有 ISR 消抖和业务回调。

每轮采样从真实采样时间重新安排下一次唤醒，不补读已经错过的 GPIO 历史。如果任务处理超过 5 ms，丢弃过期节拍并至少阻塞 1 tick；长时间总线恢复或调试暂停后不会连续读取同一电平来伪造过去的采样。InputTask 优先级 4，高于 UI 的 2；UI 也在每帧结束保证阻塞，即使画面未变而跳过 DMA。

默认时序：

| 参数 | 值 |
|---|---:|
| 扫描周期 | 5 ms |
| 稳定消抖 | 20 ms |
| 双击窗口 | 280 ms |
| 长按阈值 | 650 ms |
| 长按后首次重复 | 110 ms |
| 后续重复周期 | 90 ms |

所有时间比较均使用无符号差值或有符号 deadline 比较，支持 32-bit tick 回绕。

## 事件语义

- `Pressed`：物理按下稳定 20 ms 后立刻发送；
- `Released`：物理释放稳定 20 ms 后发送，携带本次稳定按住时长；
- `Click`：短按释放后，等待双击窗口结束且未出现第二次短按；
- `DoubleClick`：同一按钮在窗口内完成第二次短按；它与 Click 互斥；
- `LongPress`：稳定按住达到 650 ms，只发送一次；释放后不会再发送 Click；
- `Repeat`：LongPress 后按固定节拍发送，供菜单滚动或数值调整。

UI 方向导航消费 `Pressed + Repeat`。菜单、秒表、倒计时和校时中的 Enter/Jump 确认消费消抖后的 `Pressed`，不等待双击窗口；菜单切页时的确认保护会吞掉同一次按压后续的 Released/Click/DoubleClick/hold 事件，但允许下一次真实按压产生的新 `Pressed` 立即执行。Func 的菜单说明、全局快捷方式以及 Piano 的 Back 音符仍使用 Click/DoubleClick/LongPress 分类。

## 发布订阅与队列

InputService 同时提供两条有界输出路径：

- UI 消费一个固定 32 项的主队列；
- 实现 `ButtonEventObserver` 的服务可在调度器启动前注册，当前 UART 诊断是一个订阅者。

订阅回调在 InputTask 上下文同步执行，所以接口契约要求它只能做固定时间、非阻塞操作。`UartDmaService` 的回调只尝试写入自己的 16 项静态队列；字符串格式化、等待串口和 DMA 错误处理均在低优先级 UART 任务执行。这是 Observer/Publish–Subscribe 与 Producer–Consumer 的组合，不会让 InputService 依赖 USART。

UI 在每帧开始最多消费 32 项主队列事件，防止生产者不断补充事件而使本轮无限延长。SPI 的目标周期为 5 ms，I2C 为 33 ms；每个事件使用实际消费时刻更新 UI，Pressed 的投递延迟由该时刻减去消抖事件时间戳计算。任一队列满时都丢弃最旧事件、保留最新事件并增加独立计数；System 页面可查看输入和 UART 的丢弃数。订阅表容量固定为 3，不支持运行期退订，因为所有服务与固件等寿命。

`InputService::sampleCount()` 与 `maximumScanGapMs()` 可判断输入任务是否持续获得执行机会；`UiService::processedEventCount()` 与 `maximumPressAgeMs()` 可区分采样停顿和消费延迟。渲染端另提供 `renderedFrameCount()` 与 `maximumRenderTimeMs()`。这些指标无需串口输出即可通过 SWD 读取；所有时间量均为毫秒，渲染耗时不包含随后等待 OLED DMA 的时间，也不应将未实测的目标周期当成物理响应保证。

主机测试覆盖机械抖动、单击延迟、双击互斥、长按/Repeat、释放后的 Click 抑制、`uint32_t` 回绕，以及即时确认保护下的快速连续按压。菜单测试还保证六个游戏入口完整且目标均被标记为游戏页。
