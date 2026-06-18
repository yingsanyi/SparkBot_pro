# ESP-SparkBot LCD Face Cycle

这是 `LCD_blink` 的进阶版：仍然只做 ESP-SparkBot 头部 LCD 的显示实验，但不再只是背光闪烁，而是按固定间隔切换不同的 UI 表情。

## 这个项目做什么

- 初始化 ESP-SparkBot 的 240x240 SPI LCD。
- 通过 `esp_lcd` 直接绘制表情，不依赖 LVGL。
- 每隔一段时间自动切换一张新的表情。
- 不使用语音控制，也不依赖底盘或麦克风链路。

## 和 `LCD_blink` 的区别

`LCD_blink` 主要验证的是“LCD 能不能亮、背光能不能控”。  
这个项目进一步验证的是“LCD 能不能持续重绘内容、并且按状态机切换界面”。

所以这里保留了 LCD 初始化、SPI 总线、ST7789 面板、背光控制这些最基础的部分，又增加了：

- 表情渲染组件
- 场景表
- 定时切换逻辑

## 硬件

| 信号 | GPIO |
| --- | --- |
| LCD MOSI | GPIO47 |
| LCD CLK | GPIO21 |
| LCD CS | GPIO44 |
| LCD DC | GPIO43 |
| LCD RST | 未使用，`GPIO_NUM_NC` |
| LCD 背光 | GPIO46 |

## 使用方法

```powershell
cd E:\practiceWeek\codes\get_start\LCD_face_cycle
idf.py set-target esp32s3
idf.py build flash monitor
```

表情切换周期由 `menuconfig` 里的 `LCD Face Cycle Configuration` 控制，默认是 1500 ms。

## 表情序列

当前会循环显示这些界面：

- `BOOT`
- `IDLE`
- `WINK`
- `HAPPY`
- `WOW`
- `SLEEPY`
- `ANGRY`

你可以把它理解成一个很小的“UI 状态机”。

## 原理

这个项目的核心思路很简单：

1. 用 `spi_bus_initialize()` 建立 LCD 所在的 SPI 总线。
2. 用 `esp_lcd_new_panel_io_spi()` 和 `esp_lcd_new_panel_st7789()` 创建 LCD 面板。
3. 把屏幕拆成若干条小块，每次只刷新 10 行左右，减少 RAM 占用。
4. 每个表情只是一组绘制参数，比如眼睛开合、瞳孔偏移、眉毛角度、嘴型颜色。
5. 主循环按固定间隔调用一次渲染函数，于是屏幕就会自动换表情。

这里没有做复杂动画，原因是这个实验更想突出“状态驱动的 UI 变化”：

- 状态决定画什么
- 计时器决定什么时候换
- LCD 只负责把像素画出来

## 文件结构

| 文件 | 作用 |
| --- | --- |
| `main/lcd_face_cycle_main.c` | 主循环，负责定时切换表情 |
| `components/lcd_face_ui/lcd_face_ui.c` | LCD 初始化和表情绘制 |
| `main/Kconfig.projbuild` | 表情切换周期配置 |

## 扩展方法

如果想继续加新表情，一般只要做两件事：

1. 在 `lcd_face_scene_t` 里加一个新状态。
2. 在 `lcd_face_ui.c` 里给这个状态配一组绘制参数。

如果想让它更像动画，可以再把单个场景拆成两帧或三帧，按更短周期切换。
