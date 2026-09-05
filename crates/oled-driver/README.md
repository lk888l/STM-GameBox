# oled-driver

中文 | [English](README_en.md)

无堆分配、与平台无关的 SSD1306 `no_std` 驱动，支持：

- 阻塞式和异步 `embedded-hal` 传输，包含 I²C 与独立 D/C#、CS# 的 4 线 SPI；
- 6 种常见 SSD1306 面板尺寸；
- 逐 framebuffer 字节精确脏区刷新；
- 低 RAM 分页渲染；
- `embedded-graphics-core`；
- 保留 framebuffer 的故障重新初始化；
- 经校验且不可被改成非法状态的刷新策略。

它是父级 STM-GameBox workspace 的可移植显示驱动。固件通过
`firmware/src/platform/oled` 的编译期后端选择 I²C / SPI，平台引脚和 DMA 不进入本 crate。
完整设计、示例和验证方法见：

- [项目说明（中文）](../../README.md)
- [架构与不变量](../../docs/architecture.md)
- [硬件验证](../../docs/hardware-validation.md)


