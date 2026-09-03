# 架构与设计决策

## 边界

gamebox-core 是硬件无关的 no_std 业务核心。它不依赖 Embassy、STM32 PAC、RTT、Flash
驱动或堆分配，负责：

- 八键去抖与 Pressed、Released、Click、DoubleClick、LongPress、Repeat 语义；
- 菜单、历史导航、动画、Toast、设置和硬件 effect；
- 六款游戏、秒表、倒计时、RTC 编辑流程；
- 2000–2099 Gregorian 日历转换；
- 128×64 原生 1-bit UI 渲染；
- 设置记录编解码、schema 迁移和 CRC。

firmware 只负责板级事实和 effect 执行：外设初始化、任务调度、RTC 寄存器、Flash 日志、
OLED 传输、UART 和蜂鸣器。依赖方向始终从 firmware 指向 core。

## 运行期数据流

~~~text
                 ┌───────────────> UART observer queue ─> USART1 TX DMA
GPIOB snapshot ─> ButtonBank
  every 5 ms     └───────────────> UI queue ─> App
                                                │
                          ┌─────────────────────┼─────────────────────┐
                          │                     │                     │
                          v                     v                     v
                     UiRenderer             Effects              Game tick
                          │              ┌──────┼──────┐          + held mask
                          v              v      v      v
                    scene[1024]       Flash    RTC   Buzzer
                          │
                          v
                 OLED front buffer ─diff─> I²C1 DMA
~~~

输入发布者不等待消费者。UI 队列容量为 32，UART 观察队列容量为 16；满时丢弃最旧事件并
保留最新物理状态，同时增加独立丢包计数。HELD_KEYS 是去抖后的位图，Air Raid 和 Pong
每帧读取它实现连续移动。

## 调度

- button_task：5 ms 周期，唯一拥有八个 GPIO Input；
- uart_task：唯一拥有 USART1 TX 和 DMA1 Channel 4；
- buzzer_task：唯一拥有 PA12，音符间主动 await，不忙等；
- storage_task：唯一拥有 Flash，750 ms 合并设置写请求；
- main：33 ms UI/Game tick，拥有 RTC 和 OLED/I²C DMA。

所有 Channel、Signal、任务槽、画布和游戏容器都有编译期容量。运行期没有 allocator。

ButtonBank 的 Click 使用物理释放时间作为诊断时间戳，但 App 另外接收当前处理时间。
因此双击窗口到期后才执行的秒表、倒计时和 Toast 不会错误地提前 280 ms。两路队列都
收到原始时间戳，UART 输出和 Input Lab 保持可诊断性。

## 状态与副作用

App 是唯一产品状态机。页面不能调用另一个页面的阻塞循环；打开页面只改变 View、历史
和动画目标。每帧 tick 推进游戏、弹簧与倒计时。

硬件副作用用值返回：

- Persist：向 Flash owner 提交完整 PersistentData；
- SetClock：在一次确认动作后写 RTC 32-bit 秒计数器；
- Tone：播放游戏或 Piano 音符。

普通 1350 Hz / 12 ms 界面反馈是独立的一次性请求，避免它和 Persist/SetClock/Tone
互相覆盖。触发点与功能基准一致，而不是根据任意按键类型猜测。

## RTC

STM32F103 使用 RTC v1，不在 Embassy 0.6 的公共高层 RTC API 覆盖范围内。安全 adapter
集中在 firmware/src/platform/rtc.rs：

1. Embassy RCC 配置 LSE 32.768 kHz 并保留匹配的 backup domain；
2. adapter 取得 RTC 与 BKP token 后才访问 PAC；
3. 备份标记 0x4742 无效时，把 prescaler 设为 32767；
4. RTC 计数器保存自 2000-01-01 起的绝对秒数；
5. CNTH/CNTL 采用高-低-高一致性读取，写入受 RTOFF/CNF 有界等待保护。

这样日期随 VBAT 继续自然推进，不需要每天另写备份寄存器。支持范围止于
2099-12-31 23:59:59。

## OLED

UiRenderer 每次把完整场景画入 1,024 字节原生 SSD1306 page-layout 缓冲。保留的
oled-driver 拥有另一份前帧缓冲并比较变化区域；静态帧不会产生显示数据事务。

每次 init、contrast、flush、reinitialize 外层都有 80 ms Embassy timeout。失败路径：

1. 丢弃 I²C/DMA Future；
2. 只复位 I²C1；
3. 重写 PCLK1=36 MHz 下的 400 kHz CCR/TRISE；
4. 执行 STM32F1 SWRST 勘误恢复；
5. 50 ms 起指数退避，最高 2 s；
6. 重初始化并全量发送当前前帧。

业务核心和通用 OLED crate 均不接触这些芯片寄存器。

## Flash 一致性

memory.x 只向链接器暴露前 62 KiB。末尾两个 1 KiB 页分别位于 0x0800_F800 和
0x0800_FC00。

一次提交：

1. 选择非活动页；
2. 擦除该页；
3. 写入 32 字节 schema-v2 完整快照；
4. 回读并重新验证 magic、schema、CRC、generation 和 payload；
5. 全部相等后才在 RAM 中切换活动页。

任一步骤掉电都保留上一活动页。generation 使用半范围序列数比较，允许 u32 回绕。
schema v1 会迁移旧 Rust 设置中的 sound、motion、raw contrast 和 Snake 最高分。

## 时间与数值

单调业务时间使用 u32 毫秒，约 49.7 天回绕。截止时间比较采用半范围规则；累计值根据
语义显式使用 wrapping、saturating 或 checked 操作。

Release 使用 opt-level=z、fat LTO、单 codegen unit，并关闭全局 overflow-checks。
这符合 Cortex-M release 的普通整数语义，同时避免给每个已证明有界的像素循环附加
检查。主机测试配置仍启用检查。

## 扩展

新增功能时：

1. 先在 core 建立固定容量状态模型和主机测试；
2. 把入口加入静态菜单表；
3. 由 App 统一处理进入、退出和输入映射；
4. UI 只读 App 快照；
5. 只有硬件副作用才越过 effect 边界；
6. 新硬件由单一任务或 composition root 独占。

这让游戏逻辑、页面转场、输入时序和外设故障恢复可以分别验证。
