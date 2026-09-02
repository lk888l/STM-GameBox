# GameBox F103 / Rust + Embassy

这是面向旧版 STM32F103C8T6 掌上游戏机 PCB 的产品级固件重构。它不是对旧 C
工程控制流的逐行翻译，而是保留已确认的硬件契约，用可测试的状态机和 Embassy
异步任务重新组织产品。

当前基线包含：全新中文动效菜单、贪吃蛇、秒表、倒计时、低刷新待机、蜂鸣器反馈、
持久化设置与最高分。旧固件中质量和交互重复度较低的恐龙、飞机、俄罗斯方块、乒乓球
和钢琴没有直接搬运；新增游戏只需实现独立应用状态并挂到表驱动菜单，不需要再创建嵌套
阻塞循环。

## 已实现的工程特性

- 全程 `no_std`、无堆分配、稳定版 Rust，固定编译器与 `Cargo.lock`。
- 8 个按键以 1 kHz 统一采样，每键拥有独立去抖/手势状态机；支持 `Pressed`、
  `Released`、单击、双击、长按和长按重复，所有时间计算允许 `u32` 回绕。
- OLED 使用 I²C1 400 kHz 和 DMA1 Channel 6/7；渲染采用场景缓冲 + 前台缓冲，驱动只
  发送真正改变的 SSD1306 页字节。每次操作另有 80 ms 应用层硬截止时间，超时后取消
  DMA、执行 STM32F1 I²C SWRST 勘误恢复，再指数退避重连并重发完整帧缓冲。
- 菜单使用表驱动 Command、分层导航状态机和 Q24.8 定点弹簧动画；无浮点、无阻塞
  回调、无页面函数互相调用。
- 末尾两个 1 KiB Flash 页组成乒乓日志：版本、完整快照、递增代数、CRC32、写后回读
  校验。只有新页验证成功后才切换活动页，旧页始终可恢复。
- 连续设置修改经 750 ms 静默窗口合并，避免每次按键都擦除 Flash。
- 业务核心与硬件适配分离；主机可直接测试按键边界、菜单、动画、存储编解码和游戏规则。
- 中文不是整库塞入 Flash。构建期工具从旧工程字模中抽取产品实际使用的 51 个 16×16
  字符，字模总计 1,632 字节；缺字会在生成或测试阶段明确失败。

## 硬件契约

| 功能 | 引脚/资源 | 策略 |
| --- | --- | --- |
| MCU | STM32F103C8T6 | 64 KiB Flash / 20 KiB SRAM |
| 时钟 | 8 MHz HSE | PLL ×9 = 72 MHz；APB1 = 36 MHz；ADC = 12 MHz |
| OLED SCL / SDA | PB8 / PB9 | I²C1 remap，400 kHz，外部上拉，DMA1 CH6/CH7 |
| 上 / 下 / 左 / 右 | PB7 / PB5 / PB6 / PB4 | 低电平按下，内部上拉 |
| 跳跃 / 功能 / 确认 / 返回 | PB12 / PB13 / PB14 / PB15 | 低电平按下，内部上拉 |
| 蜂鸣器 | PA12 | 独占异步任务产生无忙等方波 |
| 随机熵 | PA1 / ADC1 CH1 | 55.5 周期单次采样，沿用旧板用途 |
| 调试 | PA13 / PA14 | 保留 SWD；仅关闭 JTAG 以释放 PB4 |
| 设置页 A / B | Flash `0x0800_F800` / `0x0800_FC00` | 各 1 KiB，链接器不可见 |

旧代码只把 PA1 用作随机种子，没有电池分压比或原理图证据。因此本固件不会显示伪造的
电池电压。若硬件版本增加电池采样，应先在板级适配器中加入经过万用表校准的分压和
VDDA 估算，再向 `UiRenderer` 传入毫伏值。

PB8/PB9 的 STM32F1 I²C 开漏输出没有可靠的内部上拉选项，实物必须保留 OLED 模块或 PCB
上的外部上拉。默认 SSD1306 七位地址为 `0x3c`。

## 菜单与交互

菜单结构如下：

```text
主页
├── 游戏
│   └── 贪吃蛇
├── 工具
│   ├── 秒表
│   └── 倒计时
├── 设置
│   ├── 静音
│   ├── 动画速度：关 / 慢 / 快
│   ├── 光标风格：反相 / 矩形 / 爱心
│   └── 刷新时间
└── 关机（低刷新软件待机）
```

- 上/下：按下即移动；长按后自动重复。
- 左/右：返回/进入；在设置项上调整值。
- 确认单击：进入或执行。确认双击：快速启动上次应用。
- 功能键：从菜单直接进入设置。
- 返回单击：返回上一页；主页返回进入待机。返回双击：回主页；长按：随处待机。
- 贪吃蛇：方向键转向；确认单击暂停；结束后确认或跳跃重新开始；返回退出。
- 秒表：确认/跳跃单击启停；下键单击或长按复位；返回退出。
- 倒计时：上下每次调 60 秒，左右每次调 10 秒，长按可连续调整；确认/跳跃启停。

启动动画由中心扫描线、扩散圆环、中文标题上浮和进度线组成；任意键可跳过。菜单切页
使用有方向的整页滑入，选择器位置、宽度和滚动偏移各自使用定点阻尼弹簧。选择“动画关”
时所有运动立即吸附到目标，兼顾低动态偏好。

## 架构

```text
gamebox_f103_embassy/
├── crates/gamebox-core/       # 纯业务状态机、游戏、渲染、字库和主机测试
├── firmware/
│   └── src/
│       ├── main.rs            # composition root 与帧调度
│       ├── board.rs           # 唯一引脚/时钟/电气事实来源
│       ├── services.rs        # 有界 Channel、最新值 Signal、诊断计数
│       ├── tasks.rs           # 按键、蜂鸣器、Flash 单所有者任务
│       └── platform/storage.rs
├── tools/font-subset/         # 旧 C 字模 -> 最小 Rust 字符表
├── docs/                      # 设计说明和实机验收表
├── memory.x
└── .cargo/config.toml
```

业务层采用 Ports-and-Adapters 边界；菜单和应用使用 State/Command 模式；每个有状态硬件
由唯一任务所有，任务间通过有界消息通信，避免大范围 Mutex；渲染是纯视图；Flash 使用
Journal 模式。详细决策见 [架构说明](docs/architecture.md)。

## 构建与质量门禁

安装 Rust 与目标：

```powershell
rustup toolchain install 1.96.0 --profile minimal
rustup target add --toolchain 1.96.0 thumbv7m-none-eabi
```

在本目录执行：

```powershell
# 主机单元测试（工程默认 target 是 Cortex-M，故显式指定主机）
cargo test --locked -p gamebox-core --target x86_64-pc-windows-msvc
cargo test --locked -p font-subset --target x86_64-pc-windows-msvc

# 固件静态检查、Clippy 和体积优化构建
cargo check --locked -p gamebox-f103-firmware --bin gamebox-f103
cargo clippy --locked -p gamebox-f103-firmware --bin gamebox-f103 -- -D warnings
cargo build --locked --release -p gamebox-f103-firmware --bin gamebox-f103
```

连接 ST-Link 后，runner 已固定芯片型号：

```powershell
cargo run --release -p gamebox-f103-firmware --bin gamebox-f103
```

RTT 日志默认是 `info`。发布版链接区只有 62 KiB；若代码超过上限，链接直接失败，绝不会
覆盖设置日志。需要查看逐键手势、模式切换和 Flash 提交时，可临时构建诊断日志版：

```powershell
$env:DEFMT_LOG = 'debug'
cargo run --release -p gamebox-f103-firmware --bin gamebox-f103
Remove-Item Env:DEFMT_LOG
```

完成烧录后按 [实机验收清单](docs/hardware-validation.md) 逐项验证。本次连接设备的自动化
结果记录在 [2026-09-02 实机测试报告](docs/hardware-test-report-2026-09-02.md)。

当前 release 基线经 LLVM size 工具测得：Flash 实际地址占用 60,712 / 63,488 字节，距
设置页仍有 2,776 字节硬边界余量；静态 SRAM 占用 5,124 / 20,480 字节，余下 15,356
字节供栈和运行期使用。调试符号留在 ELF 中但不会烧入 MCU。

## 更新中文子集

先把所有产品中文文案加入 `crates/gamebox-core/assets/charset.txt`，再运行：

```powershell
cargo run --locked -p font-subset --target x86_64-pc-windows-msvc -- `
  "C:\kk_data\code\stm32\STM-GameBox\firmware\STM-GameBox_Handheld\OLED\OLED_Data_Hanzi.c" `
  "crates\gamebox-core\assets\charset.txt" `
  "crates\gamebox-core\src\ui\generated.rs"
```

生成表按 Unicode 排序并使用二分查找。运行 `gamebox-core` 测试会再次确认产品文案全部有
字模。旧表没有的字需要先补一份合法 16×16、32 字节字模；工具会拒绝静默回退。

## 已知边界

- 当前“关机”是软件待机，不是切断电源。因为所有旧按键都未接 EXTI 唤醒专用电路，任务
  仍以 1 kHz 扫描按键；CPU 在任务间由 Embassy `WFI` 休眠。
- STM32F1 的旧式 RTC 在当前 Embassy 芯片支持层中没有安全适配器，待机页暂时显示开机
  时长。若必须保留日历时钟，优先增加独立 RTC adapter 和备份域测试，不把 RTC 寄存器
  访问混进业务状态机。
- 2026-09-02 已在用户连接的实物上完成烧录校验、时钟/引脚寄存器、OLED DMA、全部八键
  手势、双页日志以及两种 I²C 故障注入测试。屏幕主缓冲也已从 SRAM 抓取并渲染检查。
  蜂鸣器实际响度、真实机械触点各 30 次、逻辑分析仪波形和受控掉电仍需人工台架验收，
  详见实机测试报告。
