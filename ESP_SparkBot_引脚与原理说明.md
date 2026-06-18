# ESP-SparkBot 引脚与原理说明

本文面向刚接触 ESP-SparkBot 的同学，说明整机由哪些部分组成、每个主要引脚负责什么、头部主控和底部履带小车之间如何通信，以及代码里为什么要这样控制。

资料主要来自：

- `E:\practiceWeek\codes\esp_sparkbot-master\example\tank\sparkbot_motion_control`
- `E:\practiceWeek\codes\esp_sparkbot-master\example\tank\c2_tracked_chassis`
- `E:\practiceWeek\codes\esp_sparkbot-master\example\factory_demo_v1\components\esp_sparkbot_bsp`

## 1. 整体结构

ESP-SparkBot 可以理解成上下两层：

| 部分 | 主控 | 主要负责 |
| --- | --- | --- |
| 头部主控 | ESP32-S3 | 屏幕显示、摄像头、语音识别、麦克风/音频、Wi-Fi/Web 控制、向底盘发送运动命令 |
| 底部小车 | ESP32-C2 | 接收头部主控的 UART 命令、驱动左右履带电机、控制 RGB 灯、检测电池状态 |

两层之间通过底部的 4P 磁吸接口连接：

| 磁吸接口 | 作用 |
| --- | --- |
| `5V` | 给另一块板或接口侧提供 5V 电源通路 |
| `GPIO48` | UART 信号线之一 |
| `GPIO38` | UART 信号线之一 |
| `GND` | 公共地 |

注意：UART 通信必须共地，所以 `GND` 和两根信号线一样重要。

## 2. 头部主控引脚

头部主控板上运行 `sparkbot_motion_control` 或出厂示例，主要外设包括 LCD、摄像头、音频、按键、触摸和底盘通信。

### 2.1 I2C 总线

| 功能 | GPIO | 说明 |
| --- | --- | --- |
| I2C SCL | `GPIO5` | I2C 时钟线 |
| I2C SDA | `GPIO4` | I2C 数据线 |

I2C 是一条共享总线，多个低速外设可以共用 `SCL` 和 `SDA`。代码中 `bsp_i2c_init()` 使用 400 kHz 速率初始化它。示例说明里提到 I2C 上挂有 IMU 和摄像头控制接口；音频 Codec 也通过 I2C 做寄存器配置。

### 2.2 LCD 显示屏

| 功能 | GPIO | 说明 |
| --- | --- | --- |
| LCD MOSI | `GPIO47` | SPI 数据输出，主控把像素数据发给屏幕 |
| LCD CLK | `GPIO21` | SPI 时钟 |
| LCD CS | `GPIO44` | SPI 片选 |
| LCD DC | `GPIO43` | 区分命令和数据 |
| LCD RST | `GPIO_NUM_NC` | 未使用独立复位脚 |
| LCD 背光 | `GPIO46` | 控制屏幕背光开关 |

屏幕控制器是 ST7789，分辨率 `240 x 240`，颜色格式 RGB565，也就是每个像素用 16 bit 表示颜色。代码里使用 `SPI3_HOST`，像素时钟为 `80 MHz`，并通过 LVGL 绘制界面。

原理可以这样理解：ESP32-S3 先用 SPI 把一帧图像数据传给 ST7789，ST7789 再负责把这些数据刷新到 LCD 像素上；`GPIO46` 只管背光亮不亮，不负责画面内容。

### 2.3 OV2640 摄像头

| 功能 | GPIO | 说明 |
| --- | --- | --- |
| XCLK | `GPIO15` | 主控给摄像头的工作时钟 |
| PCLK | `GPIO13` | 摄像头像素时钟 |
| VSYNC | `GPIO6` | 一帧图像同步信号 |
| HSYNC/HREF | `GPIO7` | 一行图像同步信号 |
| D0 | `GPIO11` | 图像数据 bit0 |
| D1 | `GPIO9` | 图像数据 bit1 |
| D2 | `GPIO8` | 图像数据 bit2 |
| D3 | `GPIO10` | 图像数据 bit3 |
| D4 | `GPIO12` | 图像数据 bit4 |
| D5 | `GPIO18` | 图像数据 bit5 |
| D6 | `GPIO17` | 图像数据 bit6 |
| D7 | `GPIO16` | 图像数据 bit7 |

摄像头使用并口输出图像数据，`D0` 到 `D7` 是 8 位数据线，`PCLK` 告诉主控什么时候采样数据，`VSYNC` 和 `HSYNC` 告诉主控一帧和一行的边界。

示例默认配置为 RGB565、`240 x 240`，帧缓冲放在 PSRAM 中。这样做的原因是图像数据量比普通传感器大很多，内部 SRAM 容量不适合长期放大块图像缓冲。

### 2.4 音频 I2S

| 功能 | GPIO | 说明 |
| --- | --- | --- |
| I2S SCLK/BCLK | `GPIO39` | 音频位时钟 |
| I2S MCLK | `GPIO45` | 音频主时钟 |
| I2S LCLK/WS | `GPIO41` | 左右声道或采样帧同步 |
| I2S DOUT | `GPIO42` | 主控输出到 ES8311 Codec |
| I2S DIN/DSIN | `GPIO40` | ES8311 Codec 输入到主控 |
| 功放控制 | `GPIO_NUM_NC` | 未使用独立功放控制脚 |

I2S 专门用于传输连续音频流。I2C 负责“配置 Codec 寄存器”，I2S 负责“真正搬运音频数据”。示例默认采样率 `16000 Hz`、16 bit，适合语音识别和简单语音播放。

### 2.5 按键、触摸和指示灯

| 功能 | GPIO / 通道 | 说明 |
| --- | --- | --- |
| BOOT 按键 | `GPIO0` | GPIO 按键，低电平有效，长按可用于重启 |
| 4 个普通按键 | `GPIO1` / ADC1 CH0 | 多个按键通过不同电阻分压接到同一个 ADC 引脚 |
| 绿色 LED | `GPIO3` | 板载指示灯 |
| 触摸按键 1 | `TOUCH_PAD_NUM1` | 电容触摸通道 |
| 触摸按键 2 | `TOUCH_PAD_NUM2` | 电容触摸通道 |
| 触摸按键 3 | `TOUCH_PAD_NUM3` | 电容触摸通道 |

4 个普通按键共用一个 ADC 引脚的原理是“电阻分压”。不同按键按下时，ADC 读到的电压不同，程序根据电压范围判断是 `MENU`、`PLAY`、`DOWN` 还是 `UP`。

## 3. 头部和底盘的 UART 通信

头部主控和底盘之间通过 UART1 通信，波特率 `115200`，8 位数据，无校验，1 位停止位，无硬件流控。

### 3.1 头部主控侧

| 功能 | GPIO |
| --- | --- |
| UART TX | `GPIO38` |
| UART RX | `GPIO48` |

代码位置：

- `tank\sparkbot_motion_control\components\tracked_chassis_control\include\tracked_chassis_control.h`
- `tank\sparkbot_motion_control\components\tracked_chassis_control\tracked_chassis_control.c`

头部主控负责把语音、网页摇杆或程序动作转换成字符串命令，然后通过 UART 发给底盘。

### 3.2 底盘 ESP32-C2 侧

| 功能 | GPIO |
| --- | --- |
| UART TX | `GPIO10` |
| UART RX | `GPIO18` |

代码位置：

- `tank\c2_tracked_chassis\main\tracked_chassis.c`

底盘负责接收命令、解析命令，然后执行电机和灯光动作。

### 3.3 为什么 TX/RX 要交叉

UART 的 `TX` 是发送，`RX` 是接收。两块板通信时，一边的发送必须接到另一边的接收：

```text
头部 GPIO38 TX  -->  底盘 GPIO18 RX
头部 GPIO48 RX  <--  底盘 GPIO10 TX
GND             <--> GND
```

磁吸接口上标的是头部主控侧的 `GPIO48` 和 `GPIO38`。底盘 PCB 内部会把这两根线接到 ESP32-C2 的 UART 引脚上。

## 4. UART 命令格式

底盘程序支持几类字符串命令。

| 命令格式 | 例子 | 作用 |
| --- | --- | --- |
| `x<float> y<float>` | `x0.50 y1.00` | 控制运动方向和速度 |
| `w<number>` | `w3` | 切换 RGB 灯效模式 |
| `c<float>` | `c-1.0` | 调整左右电机速度系数，用于校准跑偏 |
| `d<number>` | `d1` | 触发跳舞动作 |

如果底盘一段时间没有收到有效运动命令，程序会自动执行 `spark_bot_motion_control(0, 0)` 停止电机。这是一个安全设计，避免通信中断后小车继续乱跑。

## 5. 底部履带小车引脚

底盘由 ESP32-C2 控制，包括两个 N20 减速电机、前后 RGB 灯板、4P 磁吸接口、锂电池和履带结构。

### 5.1 电机 PWM 引脚

| 电机/通道 | LEDC 通道 | GPIO | 说明 |
| --- | --- | --- | --- |
| 左电机 A | `LEDC_CHANNEL_0` | `GPIO4` | 左电机一个方向 |
| 左电机 B | `LEDC_CHANNEL_1` | `GPIO5` | 左电机反方向 |
| 右电机 A | `LEDC_CHANNEL_2` | `GPIO6` | 右电机一个方向 |
| 右电机 B | `LEDC_CHANNEL_3` | `GPIO7` | 右电机反方向 |

代码使用 LEDC 输出 PWM：

- PWM 频率：`4000 Hz`
- 分辨率：13 bit
- 最大占空比按 `8192` 计算

一个直流电机要能正转和反转，通常需要 H 桥驱动。程序给每个电机准备 A、B 两个控制输入：

| A 通道 | B 通道 | 电机状态 |
| --- | --- | --- |
| PWM | 0 | 正转 |
| 0 | PWM | 反转 |
| 0 | 0 | 停止 |

PWM 占空比越大，电机获得的平均能量越大，看起来速度就越快。程序里的速度范围是 `-100` 到 `100`，正负号表示方向，绝对值表示速度大小。

### 5.2 RGB 灯

| 功能 | GPIO | 说明 |
| --- | --- | --- |
| WS2812 数据线 | `GPIO0` | 控制 6 颗 WS2812 RGB 灯 |

WS2812 是串行 RGB 灯，一根数据线就能控制多颗灯。程序配置了 `WS2812_STRIPS_NUM = 6`，支持充电呼吸、低电量闪烁、常亮、闪烁、流水、灯光秀、休眠等模式。

### 5.3 电池检测

| 功能 | GPIO / ADC | 说明 |
| --- | --- | --- |
| 电池电压 ADC | `ADC_CHANNEL_3` | 读取分压后的电池电压 |
| 充电状态检测 | `GPIO2` | 低电平表示正在充电 |

电池电压不能直接随便接到 ADC，所以底盘电路会先把电池电压分压。代码中读取 ADC 后乘以 2，说明硬件分压大致是 1:1，然后用 `3300 mV` 到 `4200 mV` 估算电量百分比。

当检测到正在充电时，RGB 灯切换到充电呼吸模式；当电量低于约 20% 时，切换到低电量提示模式。

## 6. 小车运动控制原理

底盘是左右两条履带，控制方法和坦克类似：

- 左右履带同速前进：小车直行前进
- 左右履带同速后退：小车直行后退
- 左履带快、右履带慢：向右转
- 右履带快、左履带慢：向左转
- 左右履带方向相反：原地转向

底盘代码的核心公式是：

```c
base_speed = y * MOTOR_SPEED_MAX;
turn_adjust = x * MOTOR_SPEED_MAX;

left_speed = base_speed + turn_adjust;
right_speed = base_speed - turn_adjust;
```

这里：

- `y` 表示前进/后退，正数前进，负数后退。
- `x` 表示左转/右转调整。
- `MOTOR_SPEED_MAX` 是 100。

举例：

| 输入 | 左履带 | 右履带 | 效果 |
| --- | --- | --- | --- |
| `x0 y1` | 100 | 100 | 全速前进 |
| `x0 y-1` | -100 | -100 | 全速后退 |
| `x1 y0` | 100 | -100 | 原地转向 |
| `x0.5 y0.5` | 100 | 0 | 一侧走、一侧停，转弯前进 |

如果发现小车总是偏向一边，可以用 `c<float>` 命令调整左右电机系数。这个设计是因为两个 N20 电机、履带松紧和地面摩擦不可能完全一致。

## 7. 软件流程

### 7.1 头部主控流程

```text
开机
  -> 初始化 NVS
  -> 初始化 I2C
  -> 初始化屏幕动画/LVGL
  -> 初始化 BOOT 按键和触摸按键
  -> 初始化 OV2640 摄像头
  -> 初始化语音识别和音频
  -> 启动 Wi-Fi AP 和 WebServer
  -> 根据语音或网页控制生成 UART 命令
  -> 发送到底盘
```

头部更像“人机交互和决策层”：它负责听、看、显示和联网，然后把结果翻译成底盘能听懂的简单命令。

### 7.2 底盘流程

```text
开机
  -> 初始化电机 PWM
  -> 初始化 WS2812 RGB 灯
  -> 启动电池电压检测任务
  -> 启动 UART 接收任务
  -> 收到命令后解析
  -> 控制电机、灯光或跳舞动作
```

底盘更像“执行层”：它不负责复杂 UI 或语音识别，只负责把收到的命令稳定地执行出来。

## 8. 初学者容易混淆的点

### 8.1 GPIO 编号不等于排针位置

`GPIO38`、`GPIO48` 是芯片内部的 GPIO 编号，不代表接口上的第 38 个或第 48 个物理脚。看接线时要以原理图、PCB 标注和 BSP 代码为准。

### 8.2 LCD、摄像头、音频不是同一种通信

| 外设 | 通信方式 | 特点 |
| --- | --- | --- |
| LCD | SPI | 主控高速发送显示数据 |
| 摄像头 | 并口 + I2C 控制 | 并口传图像，I2C 配参数 |
| 音频 Codec | I2S + I2C 控制 | I2S 传声音，I2C 配参数 |
| 底盘 | UART | 两根信号线传字符串命令 |
| RGB 灯 | WS2812 单线协议 | 一根数据线串联控制多颗灯 |
| 按键 | GPIO 或 ADC | GPIO 判断高低电平，ADC 判断不同电压 |

### 8.3 5V 和 GPIO 不能混用

`5V` 是电源，`GPIO38/GPIO48` 是信号。不要把 5V 直接接到普通 GPIO 上，否则可能损坏芯片。磁吸接口里的 5V、信号、GND 各有自己的用途。

### 8.4 修改引脚要同时改硬件和软件

如果只在代码里把 `GPIO38` 改成别的 GPIO，但 PCB 实际没有接过去，通信不会成功。引脚修改必须同时满足：

- 芯片支持这个 GPIO 的目标功能；
- PCB 线路确实连接到了该外设；
- 代码里的宏定义和初始化参数一致；
- 没有和其他外设占用同一个引脚。

## 9. 快速查表

### 9.1 头部主控主要引脚

| 模块 | 引脚 |
| --- | --- |
| I2C | `SCL GPIO5`, `SDA GPIO4` |
| LCD SPI | `MOSI GPIO47`, `CLK GPIO21`, `CS GPIO44`, `DC GPIO43`, `BACKLIGHT GPIO46` |
| 摄像头 | `XCLK GPIO15`, `PCLK GPIO13`, `VSYNC GPIO6`, `HSYNC GPIO7`, `D0-D7 GPIO11/9/8/10/12/18/17/16` |
| 音频 I2S | `SCLK GPIO39`, `MCLK GPIO45`, `LCLK GPIO41`, `DOUT GPIO42`, `DIN GPIO40` |
| 按键 | `BOOT GPIO0`, `ADC 按键 GPIO1` |
| LED | `GPIO3` |
| 到底盘 UART | `TX GPIO38`, `RX GPIO48` |

### 9.2 底盘 ESP32-C2 主要引脚

| 模块 | 引脚 |
| --- | --- |
| UART | `TX GPIO10`, `RX GPIO18` |
| 左电机 | `GPIO4`, `GPIO5` |
| 右电机 | `GPIO6`, `GPIO7` |
| WS2812 RGB | `GPIO0` |
| 电池检测 | `ADC_CHANNEL_3`, `GPIO2` |

## 10. 一句话总结

ESP-SparkBot 的头部主控负责“看、听、显示、联网和发命令”，底部小车负责“收命令、驱动电机、控制灯光和看电量”。两者通过 4P 磁吸接口连接，其中 `GPIO38/GPIO48` 组成 UART 通信，`5V/GND` 提供电源和公共参考。
