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
                 OLED front buffer ─changed?─> selected OLED backend
                                                  │ compile-time
                                ┌─────────────────┴────────────────┐
                           SPI1 TX DMA                        I²C1 DMA
                           full frame                        dirty regions
~~~

输入发布者不等待消费者。UI 队列容量为 32，UART 观察队列容量为 16；满时丢弃最旧事件并
保留最新物理状态，同时增加独立丢包计数。HELD_KEYS 是去抖后的位图，Air Raid 和 Pong
每帧读取它实现连续移动。

## 调度

- button_task：5 ms 周期，唯一拥有八个 GPIO Input；
- uart_task：唯一拥有 USART1 TX 和 DMA1 Channel 4；
- buzzer_task：唯一拥有 PA12，音符间主动 await，不忙等；
- storage_task：唯一拥有 Flash，750 ms 合并设置写请求；
- main：5 ms UI/Game tick（调度目标 200 FPS），拥有 RTC 和选中的 OLED 后端。

所有 Channel、Signal、任务槽、画布和游戏容器都有编译期容量。运行期没有 allocator。

Embassy 线程执行器采用协作式调度；`.await` 的 Future 如果立即返回 Ready，就不会
切换任务。UI 主循环因此在每轮末尾显式 `yield_now().await`，即使静态帧跳过 DMA、
计时器已经到期或输入队列仍有事件，也让其他硬件任务得到执行机会。

UI 取到 tick 后调用 `Ticker::reset()`；按键采样后调用 `reset_at(now)`。两者都舍弃过期
节拍，不补赶已经错过的帧或用当前 GPIO 电平伪造历史样本。5 ms 是目标间隔，实际扫描
仍受一次同步渲染和 Flash 操作影响，不是中断级硬实时保证。动画与游戏继续按经过时间推进。

`BUTTON_SAMPLES`、`MAX_BUTTON_SCAN_GAP_US`、`KEY_EVENTS_PROCESSED`、
`MAX_KEY_PRESS_AGE_MS`、`RENDERED_FRAMES`、`MAX_RENDER_TIME_US` 提供无需日志的 SWD
观测。最大按键年龄只统计 Pressed，排除 Click 的双击等待时间；扫描间隔和渲染峰值会
包含调试器暂停时间，因此测量期间应保持内核运行。自动回归工具每次从当前 ELF 解析地址。

ButtonBank 的 Click 使用物理释放时间作为诊断时间戳，但 App 另外接收当前处理时间。
因此双击窗口到期后才执行的 Func 单击、Piano Back 音符和 Toast 不会错误地提前
280 ms。两路队列都收到原始时间戳，UART 输出和 Input Lab 保持可诊断性。

所有 Enter/Jump 主要操作在消抖后的 Pressed 边沿立即执行，不等待 Click。主页打开前
会将进行中的轮播弹簧收敛到当前卡片，其他页面转换直接重定向。App 输入保护吞掉该次按键
随后的 Released/Click/DoubleClick，但会放行后续新的 Pressed，既避免级联误触又不限制快速连续确认。

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

### 分层与编译选择

显示链路分为三层：

1. `gamebox-core::UiRenderer`：只产生 1,024 字节 SSD1306 原生 page-layout 场景，不依赖总线。
2. `oled-driver`：共用 `Ssd1306`、`BufferedDisplay`、脏区算法与 `AsyncTransport` 契约；
   I²C 使用控制字节前缀，SPI 使用 D/C# 与 CS#，控制器命令本身相同。
3. `firmware/src/platform/oled`：`Oled` 统一提供 init、stage_frame、flush、contrast 和恢复；
   `spi.rs` / `i2c.rs` 独占具体外设、引脚、DMA、中断、超时和复位策略。

`oled-spi` 是默认 Cargo feature，`oled-i2c` 为另一选项。两者同时启用或均未启用时
`compile_error!` 阻止构建。切换时使用 `--no-default-features --features oled-i2c`，或
README 中的 Cargo 构建别名。只编译一份后端，静态绑定，无运行时探测、动态分发或堆分配。
不支持用一份固件同时驱动两块屏幕。

`main.rs` 仅在 composition root 按 feature 移交对应 `Resources`，其后的业务循环不分接口。
所有权类型保证两版分别只消费自己的引脚和 DMA；未选中的后端不绑定其中断。SPI 版释放
PB8/PB9，I²C 版不占用 PA4～PA8。USART TX 始终使用 DMA1 CH4，不与任一显示后端冲突。

### 带宽与刷新策略

`BufferedDisplay` 拥有另一份前帧缓冲并逐字节比较变化；相同的静态帧不产生显示事务。

Canvas 的横线、竖线和实心矩形先裁剪，再按页掩码处理字节；普通字体的一列最多写入
两个页字节，保留字形以外的背景。像素参考测试覆盖清除/设置/反色、屏幕边缘、跨页与
负坐标，保证这些性能优化不改变画面。

| 项目 | `oled-spi` | `oled-i2c` |
| --- | --- | --- |
| 引脚 | PA5 SCLK、PA7 SDIN、PA6 D/C#、PA4 CS#、PA8 RES# | PB8 SCL、PB9 SDA，I²C1 重映射 |
| 总线 | SPI1 Mode 0、MSB-first、只发送，9 MHz | I²C1 400 kHz、7-bit 地址 `0x3c` |
| DMA / IRQ | DMA1 CH3 | DMA1 CH6/7、I2C1 EV/ER |
| 变化帧 | `flush_full_async`：一个窗口 + 连续 1,024 字节数据 | `flush_async`：只传脏区 |
| 控制器扫描时钟 | `D5 F0`，最高振荡器设置 | `D5 80`，保留旧板默认设置 |
| 总线操作外层超时 | 20 ms | 80 ms；HAL 单事务超时 30 ms |
| 面板硬复位 | PA8 高-低-高 | 无 RES#，不操作任何替代引脚 |

SPI 在 PCLK2=72 MHz 下使用 `/8` 得到 9 MHz，保留此前已验证的高速配置；完整 1,024 字节
数据的纯总线时间约 0.91 ms。I²C 同样数据需要约 23 ms，尚未计入命令与起停开销，因此
不能套用 SPI 的 20 ms 截止时间；脏区刷新能明显减少局部变化的传输时间。

UI tick 为 5 ms；这是生产者调度目标，不是实测帧率，I²C 全屏发送尤其受带宽限制。
菜单弹簧按固定 8 ms 子步并根据实际单调时间补步，刷新率变化不会改变动画速度。
System 页 `OLED TX` 每次成功的非空 flush 加 1，而不是按 I²C 脏区数增加，不代表面板扫描帧率。

### 错误与恢复

init、contrast、flush、reinitialize 的总线操作经统一超时包装，返回 `Timeout` 或
`Driver` 错误。SPI 硬复位中的定时等待在该总线截止时间之外。失败路径：

1. 丢弃传输/DMA Future；SPI 的传输 guard 同时释放 CS#；
2. 仅复位当前后端的外设，恢复其时钟和传输配置；
3. 运行期以 50 ms 起、最高 2 s 指数退避后重连；启动阶段为 25 ms 起、最高 1 s；
4. SPI 用 PA8 硬复位面板，I²C 跳过没有的硬复位；
5. 重初始化控制器并全量重发当前前帧，随后重新应用用户亮度。

恢复方法需要 `&mut Oled`，不能在 DMA Future 仍借用显示对象时调用。PAC 寄存器操作只存在于
后端，业务核心和通用 OLED crate 均不接触 STM32 寄存器。

SPI 特别保留黑屏修复不变量：构造后先令 CS# 为高，再切换为单线发送并令 `SPE=0`；
首次传输与故障恢复均从这个状态开始。不得把准备过程移至 CS# 拉低之后，否则本板在
禁用 SPI 时出现的 SCLK 上升沿会破坏第一条命令。详见历史 SPI 实机报告。

SPI 没有 ACK，DMA 完成只证明发送路径完成，不能证明面板已点亮。I²C 复位外设也不能
保证救活被外部器件持续拉低的线路；实际接线、上拉、电源和肉眼显示仍需分别验收。

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

Dev 和 release 使用 opt-level=z、fat LTO、单 codegen unit，并关闭全局 overflow-checks
及 debug assertions；dev 保留完整调试符号，使直接 `cargo build` 也可装入 62 KiB 程序区。
这符合 Cortex-M release 的普通整数语义，同时避免给每个已证明有界的像素循环附加
检查。主机 test profile 显式启用这两类检查。

## 构建产物

`tools/xtask` 是工作区内的 Rust 主机工具，统一管理构建、导出、校验、测试和可选的按键
回归。`.cargo/config.toml` 用 `--target host-tuple` 选择本机平台，并使用独立的 `xtask`
profile。Linux、macOS、Windows 共用同一实现，不需要 shell 脚本或第三方任务管理器。

普通 `cargo build` 使用 Rust 默认的 Cortex-M 链接器 rust-lld，仅生成 ELF。`build.rs`
只设置链接脚本搜索路径，并跟踪 `memory.x`；不承担导出，因为
[Cargo build script 在包编译之前执行](https://doc.rust-lang.org/cargo/reference/build-scripts.html#life-cycle-of-a-build-script)。

`cargo xtask build` 启动固定目标和指定接口的 Cargo 构建，读取
[Cargo JSON 的 compiler-artifact 消息](https://doc.rust-lang.org/cargo/reference/external-tools.html#json-messages)
中的 `executable` 路径，并要求 Cargo 成功退出后才导出。缓存命中同样有该消息，因此
每次调用都能重新生成完整产物；不猜测 ELF 路径，也支持 `--target-dir`、`CARGO_TARGET_DIR`
及路径中的空格。命令通过 Rust `Command` 参数调用工具，不拼装 shell 命令。

LLVM 工具从当前 rustc 的 sysroot 和 host tuple 定位，版本随固定的 Rust 工具链管理。
`llvm-readobj` 提供 ELF32 ARM 加载段信息，检查 Flash、设置页和 SRAM 边界；
`llvm-objcopy` 导出 BIN/HEX。独立的 Intel HEX 解码器校验地址、记录校验和及与 BIN 的
逐字节一致性。JSON 清单包含接口、profile、feature、内存占用与三个文件的 SHA-256。

release 输出到 `artifacts/firmware/`，debug 输出到其 `debug/` 子目录，文件名始终包含
`spi` / `i2c`。转换与校验先在独立临时目录完成，再逐文件发布，清单最后更新。转换失败
保留此前产物并返回非零状态；发布期间被中断时，可通过 `cargo xtask verify` 检出不完整
或哈希不匹配的文件。检查与构建命令不连接探针。

`cargo xtask ci` 执行格式、Clippy、主机测试、四种 debug/release SPI/I²C 构建与产物回归，
覆盖缓存重建、空格路径、转换失败保留和损坏检测。GitHub Actions 在三个系统上运行同一
命令。`cargo xtask buttons` 是独立的可选硬件回归，要求明确指定 ST-Link 序列号和安装
OpenOCD；它使用 LLVM 符号解析及平台无关的 Tcl，不依赖 GNU nm 或 PowerShell。

## 扩展

新增功能时：

1. 先在 core 建立固定容量状态模型和主机测试；
2. 把入口加入静态菜单表；
3. 由 App 统一处理进入、退出和输入映射；
4. UI 只读 App 快照；
5. 只有硬件副作用才越过 effect 边界；
6. 新硬件由单一任务或 composition root 独占。

这让游戏逻辑、页面转场、输入时序和外设故障恢复可以分别验证。
