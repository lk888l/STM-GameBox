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
   └─ About
```

六个条目都已按新架构实现，不保留“开发中”占位项。Dino、Snake、Air Raid、Tetris 和 Pong 拥有独立、可主机测试的状态模型；Piano 只需要轻量 UI/Audio 状态。进入游戏后仍由统一 UI 任务更新和绘制，不会跳入阻塞式子循环。

旧菜单使用阻塞循环和“逐帧靠近目标”的动画，速度依赖循环执行时间。新 UI 是单一状态机，所有页面每帧由状态完整重绘；Canvas 和 shadow 的最终差异决定实际 I2C 流量。

## 视觉系统

- 顶部 11 px 反色标题栏，右侧三个状态点；
- 内容区域使用 1 px 轮廓、2–3 px 圆角和明确留白；
- 主页卡片包含 24×16 程序化图标、标题、分隔线和说明；
- 列表选择使用整行反色 pill，不再使用尾部箭头或爱心光标；
- 底部 10 px 始终显示当前页的操作提示；
- 弹出提示使用居中的反色 Toast，不创建模态阻塞循环；
- 字体和图形全部在 1-bit 画布中裁剪，越界绘制安全忽略。

当前产品字库使用 6×8 ASCII，以给游戏和功能留出 Flash。菜单文本由 constexpr 表集中定义；后续加入中文时应使用按需的 12×12/16×16 子集字形包，不要重新引入整套大字库。

## 动画

动画以 FreeRTOS 单调毫秒时间驱动，而不是帧计数。`Tween` 使用 Q15 fixed-point ease-out cubic：`1 - (1 - t)^3`，无需 FPU，且在丢帧后会直接追上正确位置。

| 动画 | Full | Reduced | Off |
|---|---:|---:|---:|
| 页面进入 | 220 ms | 120 ms | 0 |
| 页面返回 | 200 ms | 110 ms | 0 |
| 主页卡片 | 190 ms | 100 ms | 0 |
| 列表高亮 | 135 ms | 75 ms | 0 |

页面切换同时绘制旧页和新页：前进时旧页左出、新页右入；返回方向相反。Canvas 自带裁剪，因此不需要额外离屏缓冲。Reduced 保留空间关系但缩短运动；Off 直接跳到目标，满足减少动态效果的可访问性需求。

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
