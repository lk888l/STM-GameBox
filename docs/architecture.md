# 架构与设计决策

## 依赖方向

`gamebox-core` 不依赖 Embassy、STM32 PAC、RTT 或 Flash 驱动。固件层可以调用核心，核心
永远不能反向访问硬件。这个约束让相同的按键、菜单、游戏和持久化规则在 Windows/Linux
主机测试与 Cortex-M3 上执行同一份代码。

运行期数据流：

```text
GPIO inputs ──1 kHz──> ButtonBank FSM ──Channel──> App FSM ──> UiRenderer
                                                      │             │
                                                      │             v
                                                      │       scene framebuffer
                                                      │             │ blit/diff
                                                      │             v
                                                      │       front framebuffer
                                                      │             │ I²C DMA
                                                      │             v
                                                      │          SSD1306
                                                      ├──Signal──> Flash journal owner
                                                      └──Channel─> buzzer owner
```

## 模式的使用边界

- **State**：`ButtonState`、`MenuModel`、`App`、`Snake` 和计时工具都是显式有限状态机。
  状态转移以值和事件为输入，不依赖隐藏全局变量。
- **Command**：不可变 `MenuEntry` 把中文标签与 `MenuAction` 配对。增加入口不会增加一套
  页面控制流。
- **Actor / Active Object**：GPIO 扫描、蜂鸣器和 Flash 各有唯一异步所有者。共享服务只
  暴露消息，不暴露外设可变引用。
- **Journal**：设置采用两个擦除页的 copy-on-write。CRC 验证后的新页才成为活动页。
- **Double Buffer**：UI 每帧完整画到 scene；再按字节复制到 front。这样“先 clear 再画回
  相同像素”不会制造错误脏区，驱动可执行精确局部刷新。
- **Strategy**：动画速度与三种光标外观是紧凑枚举策略，静态分发，不使用 `dyn Trait`。

没有为了“使用模式”而制造抽象。I²C 地址、时钟和引脚是简单板级常量；只在确实存在可
替换行为、所有权边界或状态复杂度时引入模式。

## 调度与背压

- 按键采样任务永不等待业务队列；32 深度队列满时增加 `DROPPED_KEY_EVENTS`，而不是破坏
  1 ms 采样周期。
- Flash 请求用最新值 `Signal`：新快照覆盖尚未写入的旧快照。750 ms 后再次取最新值，
  实现写合并。
- 蜂鸣器是非关键容量 4 队列；满时允许丢弃反馈音，绝不阻塞交互。
- 主循环在按键事件和 16 ms 帧 tick 之间 `select`。静态画面不触发 I²C；只有状态改变、
  时间应用到期或动画未收敛时才重画。
- 每个 OLED Future 外包 80 ms、由 Embassy 定时器独立唤醒的硬截止时间；即使 F1 I²C
  因缺少中断永久 Pending，主应用也能取消它。取消后板级 adapter 只复位 I²C1，重写
  36 MHz/400 kHz 时序并应用 RM0008 SWRST 勘误规避；指数退避重初始化成功时全量重发。
- I²C1 的 PAC 访问只存在于上述恢复 adapter，且仅在外设 Future 已被丢弃、唯一所有者
  未进行传输时调用；业务层和 OLED 通用驱动均看不到芯片寄存器。

## 时间与回绕

所有业务时间是 `u32` 毫秒，约 49.7 天回绕。持续时间使用 `wrapping_sub`；截止时间比较
采用半范围序列数规则。不存在“运行 49 天后按键失灵”的普通减法错误。

## Flash 一致性

一次提交按以下顺序发生：

1. 选择非活动页；
2. 擦除该页；
3. 写入完整 32 字节记录（magic、schema、设置、最高分、generation、CRC32）；
4. 从 Flash 回读并完整解码；
5. generation 和 payload 均相等时，才在 RAM 中切换活动页。

在步骤 1–4 任意时刻掉电，旧活动页仍有效。启动时分别验证两页并按回绕 generation 选
最新记录；两页都无效才使用默认值。

## SRAM 与 Flash 原则

- 两份 1,024 字节图像数据是主要 SRAM 消耗；没有堆和可变长度容器。
- Snake 身体上限固定为 64 格，消息队列容量都在类型中。
- 中文字符按产品文案闭包生成；每增加一个字固定增加 32 字节。
- 发布配置 `opt-level = "z"`、fat LTO、单 codegen unit；链接器把应用上限定为 62 KiB。

## 扩展新应用

新增应用时保持以下边界：

1. 在 core 内建立可复制或有界存储的状态模型并写主机测试；
2. 将应用 ID 和菜单命令加入不可变表；
3. 由 `App` 统一处理模式进入/退出，应用不能调用另一个页面的循环；
4. UI 只读取 `App` 快照；
5. 确有硬件副作用时返回 effect，由固件 composition root 路由到唯一硬件 owner。

这可避免旧工程中游戏、菜单、延时和按键驱动互相调用造成的控制流耦合。
