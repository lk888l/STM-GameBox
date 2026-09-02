# 代码审查记录

审查范围包括手写 C++ 产品层、CubeMX/HAL 与 FreeRTOS 边界、DMA/IRQ 映射、游戏状态机、静态内存、错误路径、构建配置和主机测试。旧固件仅用于确认游戏类别、按键和 PCB 资源，没有复制其阻塞式程序结构。

## 已处理问题

| 领域 | 原问题或风险 | 当前处理 |
|---|---|---|
| OLED | 运行期 blocking I2C 占用 CPU | 差分页传输改为 I2C1 TX DMA；任务睡眠等待，有超时、abort、错误计数 |
| 蜂鸣器 | TIM3 音频频率中断持续占用 CPU；PA12 不是 TIM3 输出通道 | TIM3_UP circular DMA 交替写 GPIOA BSRR，关闭 TIM3 IRQ |
| 串口 | 无独立所有权，诊断可能阻塞输入 | Observer 发布订阅 + 固定队列 + 低优先级 USART1 TX DMA 任务 |
| ADC | 单点轮询采样熵较弱 | DMA 收集 16 样本，与 UID/tick 混合；5 ms 失败回退 |
| 输入 | 容易出现 ISR 消抖、重复读取和页面耦合 | 一次 GPIOB IDR 快照；纯状态机生成统一事件；显式过载计数 |
| 游戏 | 旧式 blocking loop 无法与 UI/RTOS 共存 | 六个入口全部重写为 deadline 驱动状态模型；连续输入与离散事件分离 |
| 掉帧 | 大时间跳变后游戏可能永久变慢或无限补算 | 每个模型设置有限 catch-up，超限时重新锚定 deadline |
| Tetris | 16-bit 分数长局会回绕 | 分数改为 32-bit，棋盘继续用紧凑 16-bit 行掩码 |
| ISR 共享状态 | 普通全局服务指针依赖编译器和初始化时序 | I2C/UART 活动服务指针改为无锁原子，回调只加载一次 |
| ADC 回调标志 | `volatile` 不能表达 C++ ISR/前台同步 | 完成/错误标志改为无锁原子并使用 release/acquire |
| 启动顺序 | 先创建显示信号量会让 FreeRTOS 在调度器前屏蔽 IRQ，ADC 与 HAL tick 随后卡死 | 两阶段启动，并用 PRIMASK 护栏阻止调度器前的失效 DMA/超时等待 |
| 内存 | 隐式堆、队列无限增长和任务栈不透明 | FreeRTOS 全静态；固定容量；post-link 无堆审计；System 页显示最小栈余量 |
| RTC | F1 HAL 日期在 MCU 复位后丢失 | Backup Register 日期锚点、校验和启动规范化 |
| 菜单 | 功能清单与实际入口容易再次不一致 | constexpr 六游戏表，并增加入口数量、顺序和 isGame 分类测试 |

## 审查后保留的设计

- SSD1306 初始化仍在调度器启动前使用少量有界 blocking HAL；此时还没有可被阻塞的应用任务，改成异步状态机只会增加启动复杂度；
- 按键继续以 5 ms 高优先级任务轮询。机械按键没有适合的 DMA 请求，一次读取 GPIOB IDR 已经优于八个 EXTI 消抖路径；
- UART RX 暂不启用 DMA，因为还没有输入协议。若增加 CLI，使用 circular DMA + IDLE line，并给解析器独立固定队列；
- TIM3 DMA Channel 3 不开启完成 IRQ。循环边沿流无需逐周期通知 CPU；
- Piano 不建立独立游戏模型，当前状态只是最近音符，放在 UI 与 Audio 边界更简单。

## 验证结果

- Arm GNU Debug 与 Release 均在 `-Werror` 下链接成功；
- post-build 无堆符号检查通过；
- 本机测试覆盖按键、tick 回绕、Tween、六游戏菜单、五个游戏模型、Canvas、设置、日历和模块回滚；
- OLED 菜单 BMP 预览已生成并人工检查布局；
- ELF 符号表确认 DMA1 Channel 1/4/6、I2C1、USART1 和 HAL 完成回调由强符号实现，TIM3 handler 保持未启用；
- ST-LINK Utility CLI 已通过 100 kHz SWD + connect-under-reset 写入 Release HEX，读回校验 OK；
- 真机 HOTPLUG 检查确认核心处于 FreeRTOS 空闲 Sleep、HAL tick 持续推进、调度器已运行且共有 5 个任务；
- ADC DMA 完成=1/错误=0；OLED prepared/shadow=1、DMA TX=16、错误=0、超时=0；USART1 TX DMA 完成=1、错误=0、丢弃=0、HAL 状态 READY；
- ST-Link VCP 的 COM15 没有收到 PA9 数据，MCU 端寄存器与服务计数已证明发送完成，因此当前板上很可能没有把 VCP RX 与 PA9 相连；
- 自动改变 PB14 内部 pull direction 未能压过板上外部上拉，IDR 始终为释放态；测试后 ODR 已完整恢复。未把输入改成推挽输出，物理按键、游戏交互和按键触发的蜂鸣器仍需人工验收。

## 后续风险与建议

1. 人工操作八个物理按键，按硬件文档执行六游戏和蜂鸣器 DMA 压力验收；
2. 真机运行 10–30 分钟后记录 System 页最小栈余量，再决定是否收缩任务栈；静态分析不能代替高水位数据；
3. 在产品化阶段加入 IWDG，并设计“启动、喂狗、故障记录、调试暂停”策略；
4. 若实际 OLED 总线可能被热插拔或从设备拉死，可补充 9 个 SCL 脉冲和 I2C 外设重初始化的总线恢复流程；
5. Debug Flash 已接近 90%，后续大图片/中文字库应做按需压缩或仅在 Release 纳入，同时保持 Debug 可链接；
6. 游戏随机源用于玩法足够，但不具备密码学安全性。
