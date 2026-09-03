# OLED 菜单与动画规范

## 信息架构

主页不是旧式纵向文字菜单，而是四张横向卡片：Games、Tools、Clock、Settings。卡片负责给出功能意图和简短说明；进入后才使用高密度列表。

```text
GAMEBOX
├─ GAMES
│  ├─ Dino
│  ├─ Snake
│  ├─ Air Raid
│  ├─ Tetris
│  ├─ Pong 2P
│  └─ Piano
├─ TOOLS
│  ├─ Stopwatch
│  ├─ Countdown
│  ├─ Input Lab
│  └─ System
├─ CLOCK
│  └─ Enter 校时：时 / 分 / 年 / 月 / 日
└─ SETTINGS
   ├─ Sound
   ├─ Motion: Full / Reduced / Off
   ├─ Brightness: Low / Med / High / Max
   ├─ Home Header: Time / Date / Pet / Title
   └─ About
```

六个条目都已按新架构实现，不保留“开发中”占位项。Dino、Snake、Air Raid、Tetris 和 Pong 拥有独立、可主机测试的状态模型；Piano 只需要轻量 UI/Audio 状态。进入游戏后仍由统一 UI 任务更新和绘制，不会跳入阻塞式子循环。

旧菜单使用阻塞循环和“逐帧靠近目标”的动画，速度依赖循环执行时间。新 UI 是单一状态机，所有页面每帧由状态完整重绘；Canvas 和 shadow 的最终差异决定实际 I2C 流量。

## 视觉系统

- 主页使用 124×62 的近全屏卡片，把品牌、`当前/总数`、24×16 程序化图标、标题、说明、分页点和操作提示全部收进同一表面；
- 主页左上顶栏默认显示 RTC 时间，也可在 Settings → Home Header 切换为日期、行走像素宠物或原 `GAMEBOX` 品牌字样；选择随其他设置一起写入 backup register；
- 主页不再叠加独立标题栏和底栏，原先空置的上下区域现在都承载信息；
- 分类列表使用 11 px 紧凑标题轨、3 个 16 px 高的内容行和右侧全高位置条，底边可露出下一项作为滚动提示；
- 列表选择使用随标题宽度变化的反相 pill，移动、宽度变化和列表滚动彼此独立；
- Clock、Stopwatch 等具体功能页继续使用紧凑标题/操作栏，游戏页优先把像素留给玩法；
- 弹出提示使用居中的反色 Toast，不创建模态阻塞循环；
- 字体和图形全部在 1-bit 画布中裁剪，越界绘制安全忽略。

当前产品字库使用 6×8 ASCII，以给游戏和功能留出 Flash。菜单文本由 constexpr 表集中定义；后续加入中文时应使用按需的 12×12/16×16 子集字形包，不要重新引入整套大字库。

## 动画

菜单动画移植自 `refactor/rust-embassy`：`Spring` 使用 Q24.8 定点位置、速度和目标值，不使用 FPU。Rust 固件每 16 ms 推进两次，C++ UI 每 33 ms 绘制一帧并推进四次，二者的动画时长基本一致，同时保留 OLED 的 30 FPS 总线预算。

| 档位 | stiffness | damping | 行为 |
|---|---:|---:|---|
| Full | 62 | 184 | 快速响应并保留轻微越界回弹 |
| Reduced | 34 | 206 | 更缓和、越界更小 |
| Off | — | — | 下一步直接吸附目标 |

页面位移、主页卡片位移、列表光标 Y、光标宽度和滚动 Y 都有独立弹簧。页面切换仍同时绘制旧页和新页：前进时新页从右侧回弹落位，返回时从左侧落位；主页按选择方向切卡。Canvas 自带裁剪，因此不需要额外离屏缓冲。动画只控制呈现，不锁定输入；切换期间的有效按键会立即更新目标页面或选择，不会被静默丢弃。

Clock 页以 2× 数字显示时间。Enter/Jump 进入编辑后，闪烁下划线标记当前字段；Left/Right 切换字段，Up/Down（含 Repeat）调整，逐项确认后原子提交日期锚点。Back 或 Func 取消，不会留下半更新状态。

## 游戏界面规则

- 每个游戏使用统一的 ready/playing/game-over 覆盖层；Enter 或相应动作键开局；
- 短按 Back 返回游戏列表，Piano 因占用八个键而改为长按 Back；
- Func 的全局长按/双击快捷方式在游戏中禁用，避免抢占 Tetris、Pong 和 Piano 操作；
- Air Raid 和 Pong 使用稳定按键 bitmask 实现连续移动，离散动作继续消费语义事件；
- 分数、生命、下一块或胜者信息放在顶部紧凑 HUD，主要游戏区保持 1-bit 高对比；
- 模型按时间 deadline 推进且有追帧上限，动画掉帧不会让玩法永久变慢，也不会无限补算。

## 帧率与总线预算

UI 目标周期 33 ms，约 30 FPS。SSD1306 在 400 kHz I2C 上全屏约需 24 ms，因此动画帧可完成全屏传输；静态页面由于 shadow 比较通常不产生数据传输。运行期页面通过 I2C TX DMA 发送，UI 任务等待信号量时会让出 CPU。输入任务独立以 5 ms 运行，不会被 OLED 总线传输影响消抖。

主页 RTC 只以 4 Hz 读取几个寄存器，秒数字实际每秒变化一次；shadow 比较后仅顶栏跨越的两个 OLED page 会发送，约占 400 kHz I2C 带宽的 0.6%。Pet 在 Full/Reduced 下分别以 4/2 FPS 移动，约占 2.4%/1.2% 总线带宽；Motion Off 时宠物静止。各模式仍沿用已有的 30 FPS 画布循环，不增加任务、堆或中断。
