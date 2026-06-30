# ESP-SparkBot 五次课项目顺序与 Vibe Coding 提示词

本文档面向 ESP-SparkBot 单片机课程备课使用。`LCD_blink` 已作为第 0 次入门案例完成，后续 5 次课按每次 4 节课、每节 45 分钟设计。

这版文档的核心原则是：**不要只让 AI 知道“我要做什么”，还要让 AI 知道“这块板子的硬件事实、通信协议、软件边界、参数坑和验收方式”。** 对单片机开发来说，这些信息比“写一个项目”更重要。

## 1. 课堂总目标

学生通过 5 次课掌握一种可迁移的 Vibe Coding 单片机开发方法：

1. 能把硬件说明书喂给 AI：芯片、引脚、协议、电气限制。
2. 能把软件边界喂给 AI：ESP-IDF、组件依赖、工程结构、版本约束。
3. 能把需求拆成小颗粒：显示、输入、音频、语音、通信、联网。
4. 能审查 AI 是否跑偏：是否用错框架、猜错引脚、漏掉分区、浪费内存。
5. 能用真实现象修正 AI：编译错误、串口日志、硬件表现、参数调整。

## 2. Vibe Coding 单片机开发前置知识体系

```text
Vibe Coding 单片机开发的前置知识体系
│
├── 硬件层（喂给 AI 的“说明书”）
│   ├── 芯片型号（ESP32-S3）
│   ├── 引脚定义表
│   ├── 通信协议（SPI/I2C/UART/I2S/Touch/Wi-Fi）
│   └── 电气约束（3.3V/5V/电流/特殊引脚/共地）
│
├── 软件层（告诉 AI “用什么写”）
│   ├── 开发框架（本课程统一 ESP-IDF）
│   ├── 组件依赖（esp_lcd/esp_codec_dev/esp-sr/lvgl 等）
│   └── 工程结构（CMake/Kconfig/partition/assets）
│
└── 能力层（决定你能不能审查 AI 的输出）
    ├── 系统资源约束意识（RAM/Flash/PSRAM/分区）
    ├── 协议边界意识（SPI/I2C/I2S/UART 各干什么）
    └── 需求拆解能力（Prompt 颗粒度）
```

## 3. 每个项目都要喂给 AI 的固定模板

课堂中建议学生每次都先复制这个模板，再粘贴对应项目需求。

```text
你是一个严格遵守 ESP-IDF v5.x 的嵌入式开发助手。

我的硬件是 ESP-SparkBot，不是普通 ESP32 开发板。
请不要默认使用 Arduino、PlatformIO、TFT_eSPI。
如果我没有提供引脚、协议或组件版本，请先问我，不要编造。

请按下面顺序帮助我：
1. 先复述项目目标、硬件约束、通信协议和软件边界。
2. 再列出工程结构和需要修改的文件。
3. 再说明关键初始化流程和任务关系。
4. 再给出代码或修改方案。
5. 如果我提供编译错误，请围绕错误做最小修改，不要重写整个项目。
6. 如果我描述硬件现象，请按串口日志、供电/接线、引脚、协议、参数的顺序排查。

下面是本次项目需求：
【粘贴项目投喂卡】
```

## 4. ESP-SparkBot 硬件投喂卡

### 4.1 头部主控

| 模块 | 必须告诉 AI 的内容 |
| --- | --- |
| 主控芯片 | ESP32-S3 |
| 开发框架 | ESP-IDF v5.x，本目录材料多处按 v5.4.1 说明 |
| LCD | ST7789，240 x 240，RGB565，SPI |
| LCD 引脚 | MOSI GPIO47，SCLK GPIO21，CS GPIO44，DC GPIO43，RST 未连接，背光 GPIO46 |
| LCD 注意 | 背光 GPIO46 高电平亮；颜色是像素数据，背光是开关，二者不是一回事 |
| I2C | SCL GPIO5，SDA GPIO4，用于配置 ES8311、摄像头等低速控制接口 |
| I2S 音频 | MCLK GPIO45，SCLK/BCLK GPIO39，LRCK/WS GPIO41，DOUT GPIO42，DIN/DSIN GPIO40 |
| 触摸输入 | ESP32-S3 内置 Touch Sensor，`TOUCH_PAD_NUM1/2/3`，不是 GPIO/ADC/I2C 触摸模块 |
| BOOT 按键 | GPIO0，低电平有效，也常用于清除配置或进入下载相关流程 |
| ADC 按键 | GPIO1 / ADC1 CH0，多个普通按键可能通过分压共用一个 ADC |
| 摄像头 | OV2640，XCLK GPIO15，PCLK GPIO13，VSYNC GPIO6，HSYNC/HREF GPIO7，D0-D7 为 GPIO11/9/8/10/12/18/17/16 |

### 4.2 底盘通信

| 模块 | 必须告诉 AI 的内容 |
| --- | --- |
| 头部到底盘 | UART1，TX GPIO38，RX GPIO48，115200，8N1，必须共地 |
| 磁吸接口 | 5V、GPIO48、GPIO38、GND |
| 运动命令 | `x<float> y<float>`，例如 `x0.0 y1.0`，中间必须有空格 |
| 灯光命令 | `w<number>`，例如 `w3` 闪光、`w5` 呼吸、`w2` 常亮 |
| 舞蹈命令 | `d<number>`，例如 `d1` |
| 安全要求 | 运动命令执行一小段时间后要自动发送 `x0.0 y0.0` 停止 |

### 4.3 电气与资源约束

| 类型 | 约束 |
| --- | --- |
| 电平 | ESP32-S3 GPIO 是 3.3V 逻辑，不要把 5V 直接接 GPIO |
| 共地 | UART、外设通信必须共地 |
| GPIO46 | 在 LCD 项目里是背光；部分音频示例可能把它当功放控制，组合项目要查冲突 |
| RAM | 240 x 240 x 2 byte = 115200 byte，LCD 纯色刷新优先用行缓冲 |
| PSRAM | 语音识别、摄像头、录音缓存、复杂 UI 常需要 PSRAM |
| 分区 | ESP-SR 语音模型需要 `model` 分区；天气图标等资源可能用 mmap assets |

## 5. 常见英文缩写速查

| 缩写 | 含义 | 课堂解释 |
| --- | --- | --- |
| GPIO | General Purpose Input/Output | 通用输入输出引脚 |
| SPI | Serial Peripheral Interface | LCD 像素数据高速传输 |
| I2C | Inter-Integrated Circuit | 低速配置总线，例如配置 ES8311 |
| I2S | Inter-IC Sound | 连续音频数据总线 |
| UART | Universal Asynchronous Receiver/Transmitter | 头部和底盘之间发字符串命令 |
| LCD | Liquid Crystal Display | 液晶显示屏 |
| ST7789 | LCD 控制器型号 | 接收 SPI 像素数据并驱动 LCD |
| RGB565 | 16 bit 颜色格式 | R 5 bit，G 6 bit，B 5 bit |
| RGB888 | 24 bit 颜色格式 | R/G/B 各 8 bit |
| DMA | Direct Memory Access | 外设直接搬运内存数据，LCD/I2S 常用 |
| PCM | Pulse Code Modulation | 原始数字音频数据 |
| WAV | Waveform Audio File | 常见音频文件，内部通常包 PCM |
| AFE | Audio Front-End | 语音识别前端处理 |
| WakeNet | ESP-SR 唤醒词模型 | 检测“Hi 乐鑫” |
| MultiNet | ESP-SR 命令词模型 | 识别具体中文口令 |
| LVGL | Lightweight and Versatile Graphics Library | 嵌入式 UI 库 |
| NVS | Non-Volatile Storage | 非易失存储，用于保存配置 |
| NTP/SNTP | 网络校时协议 | 天气站获取当前时间 |

## 6. 项目分组与课程路线

| 课次 | 主题 | 项目 | 核心能力 |
| --- | --- | --- | --- |
| 第 0 次 | 已完成入门 | `LCD_blink` | ESP-IDF、LCD SPI、GPIO46 背光、基础提示词 |
| 第 1 次 | LCD 内容显示 | `LCD_color_frequency`、`LCD_text_switch`、`LCD_text_scroll` n 选 1 | RGB565、字形、滚动、Kconfig、分块刷新 |
| 第 2 次 | UI 状态机与触摸 | `LCD_face_cycle`、`LCD_face_touch_control` n 选 1 | 状态机、事件队列、Touch Sensor |
| 第 3 次 | 音频播放与录音 | `speaker_hello`、`LCD_color_audio`、`LCD_color_speech`、`speaker_record_playback` n 选 1 | ES8311、I2C、I2S、WAV/PCM |
| 第 4 次 | 离线语音识别 | `LCD_face_voice_control`、`sparkbot_voice_chassis` n 选 1 | ESP-SR、WakeNet、MultiNet、模型分区 |
| 第 5 次 | 联网与整车综合 | `LCD_weather`、`sparkbot_voice_chassis`、`tank/sparkbot_motion_control` n 选 1 | Wi-Fi、HTTP、LVGL、UART、Web 控制、摄像头 |

### 6.1 递进式改进链

每个提示词都尽量保留“我已经完成过某个基础项目，现在要在它基础上升级”的句式。这样 AI 更容易沿用已有工程结构、硬件参数和学生已经掌握的知识，而不是重新发散成一个完全陌生的项目。

| 新项目 | 建议告诉 AI 的基础项目 | 本次升级点 |
| --- | --- | --- |
| `LCD_color_frequency` | `LCD_blink` | 从背光闪烁升级为颜色 + 频率模式表 |
| `LCD_text_switch` | `LCD_blink` | 从纯色/背光升级为文字像素绘制 |
| `LCD_text_scroll` | `LCD_text_switch` | 从静态文字升级为滚动动画 |
| `LCD_face_cycle` | `LCD_blink` 或 `LCD_text_scroll` | 从简单图形升级为 UI 状态机 |
| `LCD_face_touch_control` | `LCD_face_cycle` | 从定时切换升级为触摸事件控制 |
| `speaker_hello` | `LCD_blink` 的 ESP-IDF 工程经验 | 从显示外设切换到音频外设最小闭环 |
| `LCD_color_audio` | `LCD_color_frequency` + `speaker_hello` | 把 LCD 显示和音频播放联动 |
| `LCD_color_speech` | `LCD_text_switch` + `speaker_hello` | 把中文显示和 WAV 语音资源联动 |
| `speaker_record_playback` | `speaker_hello` | 从音频播放升级为录音 + 回放 |
| `LCD_face_voice_control` | `LCD_face_cycle` + 音频采集经验 | 从 UI 状态机升级为语音事件控制 |
| `sparkbot_voice_chassis` | `LCD_face_voice_control` + UART 底盘协议 | 从语音控制 LCD 升级为语音控制底盘 |
| `LCD_weather` | LCD 显示项目 | 从本地显示升级为 Wi-Fi + HTTP + LVGL |
| `tank/sparkbot_motion_control` | `sparkbot_voice_chassis` + `LCD_weather/Web` 经验 | 从单功能项目升级为整车综合 |

## 7. 第 1 次课：LCD 内容显示与参数化

### 7.1 课堂目标

从 `LCD_blink` 过渡到“屏幕可以表达信息”。学生要知道 LCD 项目必须说清 LCD 控制器、SPI 引脚、颜色格式、刷新策略和内存约束。

### 7.2 4 节课安排

| 节次 | 内容 |
| --- | --- |
| 第 1 节 | 复盘 `LCD_blink`：ST7789、SPI、GPIO46、RGB565、行缓冲 |
| 第 2 节 | 学生 n 选 1，先让 AI 复述硬件和软件边界 |
| 第 3 节 | 生成工程、构建、修复 CMake/Kconfig/API 报错 |
| 第 4 节 | 烧录验证，讲清“颜色数据”和“背光闪烁”的区别 |

### 7.3 共同坑点

| 坑 | 正确做法 |
| --- | --- |
| AI 写成 Arduino/TFT_eSPI | 明确 ESP-IDF、`app_main()`、`esp_lcd` |
| AI 猜 LCD 引脚 | 固定 GPIO47/21/44/43/46 |
| AI 用整屏 framebuffer | 优先用 10 行左右的 DMA 行缓冲 |
| AI 不等 SPI 传输完成 | 用回调/信号量等待 `draw_bitmap` 完成 |
| AI 混用 SPI host/频率 | 以当前项目源码为准；不要随意从官方 BSP 拿 SPI3/80MHz 替换 |
| AI 把背光闪烁写成颜色轮播 | 颜色由像素数据控制，背光由 GPIO46 控制 |

### 7.4 项目 A：`LCD_color_frequency` 投喂卡

```text
我已经完成过 ESP-SparkBot 的 LCD_blink 入门项目，现在要在它的基础上复现一个 LCD_color_frequency 项目。

目标现象：
- ESP-SparkBot 头部 LCD 循环显示红、绿、蓝、黄、紫等纯色。
- 每种颜色对应一个背光闪烁频率，例如 1Hz、2Hz、4Hz、6Hz、8Hz。
- 串口打印当前模式名、RGB565 颜色值、频率和半周期。

硬件说明书：
- 芯片：ESP32-S3。
- LCD：ST7789，240x240，RGB565。
- SPI LCD 引脚：MOSI GPIO47，SCLK GPIO21，CS GPIO44，DC GPIO43，RST 未连接。
- 背光：GPIO46，高电平亮，低电平灭。

通信与参数：
- SPI 负责传输像素数据。
- GPIO46 只负责背光开关。
- 8 bit RGB 要转换成 16 bit RGB565。
- 半周期公式：half_period_ms = 1000 / (2 * frequency_hz)。
- frequency_hz 为 0 时要特殊处理，不能除以 0。
- 设置最小半周期，避免闪烁过快导致任务循环过密。

软件边界：
- ESP-IDF v5.x。
- 不使用 Arduino、PlatformIO、TFT_eSPI、LVGL。
- main 依赖 esp_lcd、esp_driver_gpio、esp_driver_spi。
- 提供 Kconfig：颜色保持时间、最小半周期。
- 用 DMA 行缓冲分块刷屏，不要申请 240x240 整屏大数组。

验收：
- `idf.py build` 成功。
- 烧录后 LCD 颜色循环变化。
- 背光按不同频率亮灭。
- 串口能看到模式名、颜色、频率、半周期。

请先输出工程结构、模式表设计、RGB565 转换、频率换算和内存占用估算，再生成代码。
```

### 7.5 项目 B：`LCD_text_switch` 投喂卡

```text
我已经完成过 ESP-SparkBot 的 LCD_blink 入门项目，现在要在它的基础上复现一个 LCD_text_switch 项目。

目标现象：
- LCD 中央循环显示 HelloWorld 和 你好世界。
- 文字切换周期可配置，默认 1000ms。

硬件说明书：
- ESP32-S3。
- ST7789 LCD，240x240，RGB565。
- SPI 引脚：MOSI GPIO47，SCLK GPIO21，CS GPIO44，DC GPIO43，背光 GPIO46。

通信与参数：
- SPI 发送像素数据。
- 英文字母可以使用简单点阵字体。
- 中文“你好世界”使用固定字形表，不运行时加载字体文件。
- 注意文字宽度、高度、居中坐标和越界。

软件边界：
- ESP-IDF，不使用 Arduino/TFT_eSPI/PlatformIO。
- 不引入完整 LVGL，继续使用 `esp_lcd_panel_draw_bitmap()`。
- 提供 Kconfig：文字切换周期。
- 控制 RAM，占用小块缓冲即可。

坑点：
- AI 容易把中文显示写成在线字体或文件系统加载字体，不适合最小项目。
- AI 容易忽略中文点阵宽度，导致文字不居中或越界。

验收：
- 串口显示当前文字状态。
- LCD 能看到英文和中文交替出现。

请先输出字形数据结构、文字宽度计算方法和刷新策略，再生成工程。
```

### 7.6 项目 C：`LCD_text_scroll` 投喂卡

```text
我已经完成过 LCD_text_switch 项目，现在要在它的基础上复现一个 LCD_text_scroll 项目。

目标现象：
- LCD 显示一行滚动文字：HelloWorld， 你好世界！
- 文字从右向左滚动，超出屏幕后循环。

硬件说明书：
- ESP32-S3。
- ST7789，240x240，RGB565。
- SPI 引脚：GPIO47/21/44/43，背光 GPIO46。

通信与参数：
- SPI 刷新 LCD 像素。
- Kconfig 提供帧间隔 CONFIG_LCD_TEXT_SCROLL_FRAME_MS，默认 40ms。
- Kconfig 提供每帧移动像素 CONFIG_LCD_TEXT_SCROLL_PIXELS_PER_FRAME，默认 2px。
- 需要计算整句文字总宽度、当前 x 偏移、屏幕裁剪区域。

软件边界：
- ESP-IDF。
- 不使用 Arduino/TFT_eSPI/PlatformIO。
- 不使用 LVGL。
- 使用小缓冲或分块刷新，避免整屏大缓冲。

坑点：
- 不要让 x 坐标越界造成数组访问错误。
- 帧间隔太小会占用 CPU，太大滚动不流畅。
- 中文字形、英文点阵和标点宽度要统一计算。

验收：
- 文字平滑从右向左滚动。
- 串口打印文字总宽度、帧间隔、像素步进。

请先输出滚动坐标模型和边界处理，再生成代码。
```

## 8. 第 2 次课：UI 状态机与触摸事件

### 8.1 课堂目标

从“画固定内容”升级到“状态驱动 UI”。学生要掌握状态枚举、状态表、事件队列、回调中不能做重任务。

### 8.2 4 节课安排

| 节次 | 内容 |
| --- | --- |
| 第 1 节 | 讲状态机：状态、事件、转移、渲染函数 |
| 第 2 节 | 复现 `LCD_face_cycle`：定时切换表情 |
| 第 3 节 | 复现或扩展 `LCD_face_touch_control`：触摸事件控制表情 |
| 第 4 节 | 代码审查：状态和绘制是否解耦，回调是否只发事件 |

### 8.3 共同坑点

| 坑 | 正确做法 |
| --- | --- |
| AI 把表情做成图片资源 | 本项目用绘制参数生成表情 |
| AI 在触摸回调里直接刷 LCD | 回调只发 FreeRTOS 队列，业务任务刷屏 |
| AI 把触摸写成 GPIO 按键 | 明确是 ESP32-S3 Touch Sensor |
| AI 把触摸写成 I2C 触摸芯片 | 明确无外接触摸芯片 |
| AI 忘记 LCD 硬件参数 | 每次仍要喂 ST7789/SPI/GPIO |

### 8.4 项目 A：`LCD_face_cycle` 投喂卡

```text
我已经完成过 ESP-SparkBot 的 LCD_blink / LCD_text_scroll 这类基础 LCD 显示项目，现在要在它们的基础上复现一个 LCD_face_cycle 项目。

目标现象：
- LCD 自动循环显示 BOOT、IDLE、WINK、HAPPY、WOW、SLEEPY、ANGRY。
- 每隔默认 1500ms 切换一次。

硬件说明书：
- ESP32-S3。
- ST7789 LCD，240x240，RGB565。
- SPI 引脚：MOSI GPIO47，CLK GPIO21，CS GPIO44，DC GPIO43，RST 未使用，背光 GPIO46。

通信与参数：
- SPI 负责 LCD 像素刷新。
- 表情不是图片，而是眼睛、眉毛、嘴巴、颜色等绘制参数。
- 每次刷新按若干行分块，控制 RAM 占用。

软件边界：
- ESP-IDF。
- 不使用 Arduino/TFT_eSPI/PlatformIO。
- 不使用 LVGL。
- 将 LCD 初始化和表情绘制封装到 `components/lcd_face_ui`。
- main 只负责状态切换和调用 `lcd_face_ui_show_scene()`。
- Kconfig 提供表情切换周期。

验收：
- 串口打印当前 scene。
- LCD 按顺序切换表情。
- 新增一个表情时只需改枚举和绘制参数，不大改主循环。

请先输出状态机设计、组件结构和绘制参数表，再生成代码。
```

### 8.5 项目 B：`LCD_face_touch_control` 投喂卡

```text
我已经完成过 LCD_face_cycle 项目，现在要在它的基础上复现一个 LCD_face_touch_control 项目。

目标现象：
- LCD 显示表情。
- 触摸键 1 回到待机/确认。
- 触摸键 2 切换到上一个表情。
- 触摸键 3 切换到下一个表情。

硬件说明书：
- ESP32-S3。
- LCD：ST7789，240x240，RGB565，MOSI GPIO47，CLK GPIO21，CS GPIO44，DC GPIO43，背光 GPIO46。
- 触摸：ESP32-S3 内置电容触摸 Touch Sensor。
- 触摸通道：`TOUCH_PAD_NUM1`、`TOUCH_PAD_NUM2`、`TOUCH_PAD_NUM3`。
- 触摸铜箔已经在板上连好，不需要额外接线。
- 不要写成普通 GPIO 按键、ADC 分压按键或 I2C 触摸芯片。

通信与参数：
- 触摸没有 SPI/I2C 外部协议，使用芯片内部 Touch Sensor 外设。
- 软件使用 `touch_element/touch_button`。
- 事件至少处理按下；可以订阅释放和长按。
- 参考灵敏度：`TOUCH_PAD_NUM1` 为 `0.035F`，`TOUCH_PAD_NUM2/3` 为 `0.08F`。

软件边界：
- ESP-IDF。
- 依赖 `touch_element`、`esp_driver_touch_sens`、`lcd_face_ui`。
- 推荐初始化顺序：install touch element -> install touch button -> create 3 buttons -> subscribe events -> start touch element。
- 触摸回调只把键值和事件类型发送到 FreeRTOS 队列。
- 业务任务从队列取事件，再调用 `lcd_face_ui_show_scene()`。

坑点：
- 不要在触摸回调里做 LCD 刷屏。
- 不要把长按事件写成死循环。
- 触摸太敏感或不灵敏时优先调整 sensitivity，不要先改 UI 逻辑。

验收：
- 串口打印触摸键编号和事件类型。
- 触摸 1/2/3 能稳定切换表情。

请先输出触摸事件模型、队列数据结构、状态切换流程，再生成代码。
```

## 9. 第 3 次课：音频播放、录音与多模态联动

### 9.1 课堂目标

让学生知道音频链路和 LCD 链路完全不同：**I2C 配置 ES8311，I2S 传输 PCM 音频。** 本次课不追求复杂语音识别，重点是音频数据如何进入和离开芯片。

### 9.2 4 节课安排

| 节次 | 内容 |
| --- | --- |
| 第 1 节 | 讲 ES8311、I2C、I2S、WAV、PCM、采样率、位宽 |
| 第 2 节 | 学生 n 选 1：播放、颜色音调、颜色语音、录音回放 |
| 第 3 节 | 生成/整理 `components/sparkbot_audio`，修复音频依赖 |
| 第 4 节 | 验证声音、音量、爆音、录音增益、缓存大小 |

### 9.3 共同坑点

| 坑 | 正确做法 |
| --- | --- |
| AI 把 I2C 当成音频数据线 | I2C 只配置 ES8311，I2S 才传 PCM |
| AI 漏 I2S MCLK | ES8311 通常需要 MCLK |
| AI 把 WAV 直接写进 I2S | 先解析 WAV 头，找到 PCM 数据 |
| AI 忽略采样率/位宽/声道 | codec open 参数要和 PCM 数据一致 |
| AI 录音时不关喇叭 | 录音时静音/关功放，避免回授 |
| AI 忽略缓存大小 | 16kHz、16bit、单声道、5s 约 160000 byte |
| GPIO46 冲突 | LCD 项目是背光，部分音频项目可能作功放控制；组合时要检查 |

### 9.4 项目 A：`speaker_hello` 投喂卡

```text
我已经完成过 ESP-SparkBot 的 LCD_blink 入门项目，已经熟悉 ESP-IDF 工程结构；现在要从显示外设切换到音频外设，复现一个 speaker_hello 项目。

目标现象：
- 上电后初始化 ES8311。
- 播放嵌入固件的 assets/nihao.wav。
- 可配置音量和重复播放间隔。

硬件说明书：
- ESP32-S3。
- 音频 Codec：ES8311。
- I2C 配置线：SCL GPIO5，SDA GPIO4。
- I2S 音频线：MCLK GPIO45，SCLK/BCLK GPIO39，LRCK/WS GPIO41，DOUT GPIO42，DSIN GPIO40。
- 本项目只播放，不需要使用 DSIN 录音。

通信与参数：
- I2C 负责配置 ES8311 寄存器。
- I2S DOUT 负责把 PCM 音频从 ESP32-S3 送到 ES8311。
- WAV 文件需要解析头部，找到 PCM 数据、采样率、声道、位宽。
- `assets/nihao.wav` 通过 `EMBED_FILES` 嵌入固件。

软件边界：
- ESP-IDF。
- 不使用 Arduino。
- 音频初始化封装到 `components/sparkbot_audio`。
- main 负责加载 WAV、打开 codec、写入 PCM。
- 依赖 `esp_codec_dev`、I2C/I2S 驱动等必要组件。

验收：
- 串口打印 WAV 参数。
- 喇叭能播放“你好”。
- 音量和重复间隔可通过 Kconfig 修改。

请先输出 ES8311/I2C/I2S 音频链路、WAV 解析流程和工程结构，再生成代码。
```

### 9.5 项目 B：`LCD_color_audio` 投喂卡

```text
我已经完成过 LCD_color_frequency 和 speaker_hello，现在要在这两个基础项目上复现一个 LCD_color_audio 项目。

目标现象：
- LCD 循环显示不同纯色。
- 每种颜色同时播放一个对应的正弦波音调。

硬件说明书：
- LCD：ST7789，240x240，RGB565，MOSI GPIO47，SCLK GPIO21，CS GPIO44，DC GPIO43，背光 GPIO46。
- 音频：ES8311，I2C GPIO5/GPIO4，I2S MCLK GPIO45，SCLK GPIO39，LRCK GPIO41，DOUT GPIO42，DSIN GPIO40。

通信与参数：
- LCD 通过 SPI 刷像素。
- 音频通过 I2C 配置 ES8311，通过 I2S 写 PCM。
- 正弦波可以运行时生成 PCM。
- 需要指定采样率、位宽、声道、音调频率、缓冲区大小。

软件边界：
- ESP-IDF。
- 不使用 Arduino/PlatformIO/LVGL。
- LCD 和音频拆分为清晰函数或组件。
- 串口打印当前颜色和音调频率。

坑点：
- 不要把颜色频率 Hz 和音频频率 Hz 混淆。
- 音频 buffer 太小可能断续，太大浪费 RAM。
- LCD 刷新和音频播放不要互相长时间阻塞。

验收：
- 每种颜色出现时能听到对应音调。
- 串口日志和实际颜色/音调一致。

请先给出任务拆解、资源占用估算、音频生成公式，再生成代码。
```

### 9.6 项目 C：`LCD_color_speech` 投喂卡

```text
我已经完成过 LCD_text_switch 和 speaker_hello，现在要在中文显示和音频播放的基础上复现一个 LCD_color_speech 项目。

目标现象：
- LCD 循环显示红色、绿色、蓝色、黄色、紫色。
- 屏幕中央显示中文颜色名。
- 喇叭播放对应颜色名的中文语音。

硬件说明书：
- LCD：ST7789，240x240，GPIO47/21/44/43/46。
- 音频：ES8311，I2C GPIO5/GPIO4，I2S GPIO45/39/41/42/40。

通信与参数：
- 中文字形预生成到 C include 文件。
- 中文语音预生成成 `assets/*.wav`，通过 `EMBED_FILES` 嵌入固件。
- 不在设备端联网生成 TTS。
- WAV 播放前解析采样率、声道、位宽。

软件边界：
- ESP-IDF。
- 不使用在线 TTS。
- Kconfig：颜色停留时间 `CONFIG_LCD_COLOR_SPEECH_HOLD_MS`，音量 `CONFIG_LCD_COLOR_SPEECH_VOLUME`。
- README 说明如何重新生成字形和语音资源。

坑点：
- AI 可能尝试联网 TTS，要明确不联网。
- AI 可能忘记在 CMake 中嵌入 WAV。
- 中文字形和语音文件是编译期资源，不是运行时下载。

验收：
- LCD 显示颜色和中文名。
- 喇叭播放对应颜色名。
- 资源文件缺失时构建应给出清晰错误。

请先输出资源清单、CMake 嵌入方式、主流程和参数表，再生成代码。
```

### 9.7 项目 D：`speaker_record_playback` 投喂卡

```text
我已经完成过 speaker_hello 音频播放项目，现在要在它的基础上复现一个 speaker_record_playback 项目。

目标现象：
1. 播放“开始录音”提示音。
2. 麦克风录制默认 5 秒。
3. 播放“录音结束”提示音。
4. 回放刚才录到的声音。

硬件说明书：
- ESP32-S3。
- ES8311 Codec。
- I2C：SCL GPIO5，SDA GPIO4。
- I2S 双向：MCLK GPIO45，SCLK GPIO39，LRCK GPIO41，DOUT GPIO42，DSIN GPIO40。
- 如果使用功放控制，必须检查 GPIO46 是否和 LCD 背光冲突；本项目不需要同时使用 LCD。

通信与参数：
- I2S TX 用于播放。
- I2S RX 用于录音。
- 默认录音参数建议：16kHz、16bit、单声道、5s。
- 缓存估算：16000 * 2 byte * 1 channel * 5 = 160000 byte。
- 录音时静音或关闭功放，减少喇叭声音被重新录入。

软件边界：
- ESP-IDF。
- 音频初始化、播放、录音封装到 `components/sparkbot_audio`。
- Kconfig：录音秒数、播放音量、麦克风增益。
- 大录音缓存必要时使用 PSRAM 或 `heap_caps_malloc`。

坑点：
- 麦克风声音小先调 gain，不要先改 I2S 引脚。
- 爆音先检查采样率/位宽/声道是否一致。
- 回放没有声音时检查 DSIN GPIO40、麦克风模拟输入和增益。

验收：
- 串口打印开始录音、录音结束、回放。
- 能听到自己录下的声音。

请先计算缓存大小，说明 I2S TX/RX 任务流程，再生成工程结构和代码。
```

## 10. 第 4 次课：离线语音识别与事件控制

### 10.1 课堂目标

从“播放音频”升级到“理解口令”。学生要理解 ESP-SR 链路：麦克风采集 -> AFE -> WakeNet -> MultiNet -> 事件队列 -> 业务控制。

### 10.2 4 节课安排

| 节次 | 内容 |
| --- | --- |
| 第 1 节 | 讲 ESP-SR、唤醒词、命令词、拼音注册、模型分区 |
| 第 2 节 | 复现 `LCD_face_voice_control`：语音切换表情 |
| 第 3 节 | 进阶 `sparkbot_voice_chassis`：语音转 UART 命令 |
| 第 4 节 | 调参和排错：增益、阈值、超时、模型分区、串口日志 |

### 10.3 共同坑点

| 坑 | 正确做法 |
| --- | --- |
| AI 用云端语音识别 | 明确使用 ESP-SR 离线识别 |
| AI 漏模型分区 | `partitions.csv` 需要 `model` 分区 |
| AI 漏 PSRAM 配置 | 语音模型和 AFE 常需要 PSRAM |
| AI 把唤醒词和命令词混在一起 | WakeNet 先唤醒，MultiNet 再识别命令 |
| AI 直接在识别任务里控制业务 | 用 FreeRTOS 队列转给 handler |
| AI 忘记拼音注册 | MultiNet 中文命令通常注册拼音 |
| 音频提示影响识别 | 课堂演示优先用 LCD/灯光反馈，少用提示音 |

### 10.4 项目 A：`LCD_face_voice_control` 投喂卡

```text
我已经完成过 LCD_face_cycle，也完成过基础音频播放/采集项目，现在要在 UI 状态机和音频采集的基础上复现一个 LCD_face_voice_control 项目。

目标现象：
- 上电后 LCD 显示等待唤醒。
- 说“Hi 乐鑫”后进入听命令状态。
- 说“开心表情、惊讶表情、困了表情、生气表情、眨眼表情、待机表情、下一个表情”等口令后切换 LCD 表情。
- 不联网，不使用云端语音服务。

硬件说明书：
- LCD：ST7789，240x240，MOSI GPIO47，CLK GPIO21，CS GPIO44，DC GPIO43，背光 GPIO46。
- 音频：ES8311，I2C GPIO5/GPIO4，I2S MCLK GPIO45，SCLK GPIO39，LRCK GPIO41，DOUT GPIO42，DSIN GPIO40。

通信与参数：
- I2C 配置 ES8311。
- I2S 从麦克风读取 16kHz、16bit 音频。
- WakeNet 识别唤醒词 Hi 乐鑫。
- MultiNet 识别命令词。
- 命令词使用拼音注册，例如 `kai xin biao qing`。

软件边界：
- ESP-IDF。
- 引入 `esp-sr`、`esp-dsp`、`esp_codec_dev`。
- 自定义 `partitions.csv`，给语音模型留 `model` 分区。
- sdkconfig 打开 WakeNet、MultiNet、ES8311、PSRAM 相关配置。
- 任务拆分：
  1. `audio_feed_task` 读取麦克风并喂给 AFE。
  2. `audio_detect_task` 处理 WakeNet/MultiNet。
  3. `sr_event_handler_task` 从队列取事件并切换 LCD。

坑点：
- 识别任务不要直接刷屏。
- 新增命令要同时改枚举、命令名、拼音表、处理函数、README。
- 唤醒后命令超时要回到等待状态。

验收：
- 串口打印 Speech detection started。
- 能看到 wake、listen、detected 等状态日志。
- 识别成功后 LCD 切换对应表情。

请先输出系统架构、任务关系、命令表、分区和 sdkconfig 需求，再生成代码。
```

### 10.5 项目 B：`sparkbot_voice_chassis` 投喂卡

```text
我已经完成过 LCD_face_voice_control，并已经理解底盘 UART 命令格式；现在要在语音识别和 UART 控制的基础上复现一个 sparkbot_voice_chassis 项目。

目标现象：
- 说“Hi 乐鑫”唤醒。
- 识别运动、灯光、跳舞口令。
- 通过 UART1 向底盘发送 ASCII 命令。
- LCD 显示 WAITING、LISTEN、OK、TIMEOUT、ERROR 等状态。

硬件说明书：
- 音频：ES8311，I2C GPIO5/GPIO4，I2S GPIO45/39/41/42/40。
- LCD：ST7789，GPIO47/21/44/43/46。
- 底盘 UART：UART_NUM_1，TX GPIO38，RX GPIO48，115200，8N1，共地。
- 4P 磁吸接口：5V、GPIO48、GPIO38、GND。

通信与命令：
- 向前冲：`xiang qian chong` -> `x0.0 y1.0`
- 向后退：`xiang hou tui` -> `x0.0 y-1.0`
- 向左转：`xiang zuo zhuan` -> `x-1.0 y0.0`
- 向右转：`xiang you zhuan` -> `x1.0 y0.0`
- 停止运动：`ting zhi yun dong` -> `x0.0 y0.0`
- 闪光模式：`shan guang mo shi` -> `w3`
- 呼吸模式：`hu xi mo shi` -> `w5`
- 常亮模式：`chang liang mo shi` -> `w2`
- 灯光秀：`deng guang xiu` -> `w7`
- 跳个舞：`tiao ge wu` -> `d1`
- 注意运动命令 `x` 和 `y` 中间必须有空格。

软件边界：
- ESP-IDF。
- ESP-SR 离线识别，不联网。
- 识别事件通过队列交给控制任务。
- 运动命令执行短时间后自动停止。
- 灯光模式命令不要被统一恢复常亮覆盖。

坑点：
- 能识别但底盘不动，优先查磁吸接口、共地、UART TX/RX、底盘固件。
- 串口有 `Sent chassis command` 不代表底盘一定收到。
- 灯光命令不要被运动结束逻辑覆盖。

验收：
- 串口打印识别到的命令和发送的 UART 字符串。
- 底盘能前后左右、停止、切换灯效。

请先输出语音识别到 UART 控制的完整事件流，再给出工程或修改方案。
```

## 11. 第 5 次课：联网与整车综合应用

### 11.1 课堂目标

让学生做最终产品原型。这个阶段不要求所有人做同一个项目，而是按兴趣 n 选 1：天气站、语音底盘、Web 遥控整车。

### 11.2 4 节课安排

| 节次 | 内容 |
| --- | --- |
| 第 1 节 | 选题和架构评审：外设、协议、依赖、分区、风险 |
| 第 2 节 | 配置 Wi-Fi/API/分区/资源，生成或整理代码 |
| 第 3 节 | 构建烧录，按串口日志定位网络和硬件问题 |
| 第 4 节 | 最终展示：功能、提示词、故障记录、代码讲解 |

### 11.3 共同坑点

| 坑 | 正确做法 |
| --- | --- |
| AI 把联网密钥写死 | 用 menuconfig 或 NVS，避免硬编码敏感信息 |
| AI 混入不需要的语音/摄像头模块 | 天气站只保留天气功能 |
| AI 重写完整复杂项目 | 先读现有工程结构，再做最小复现 |
| AI 忽略资源分区 | 图片、模型、网页资源可能需要分区或文件系统 |
| AI 把 AP/STA 模式混淆 | 天气站多用 STA 联网，Web 控制常用 AP 让手机连接 |

### 11.4 项目 A：`LCD_weather` 投喂卡

```text
我已经完成过多个 LCD 显示项目，现在要在 LCD UI 的基础上复现一个 LCD_weather 天气站项目。

目标现象：
- ESP-SparkBot LCD 显示天气主页。
- Wi-Fi 联网后自动定位当前位置。
- 请求和风天气实时天气。
- 显示天气图标、天气状态、温度、地区、时间、电量、Wi-Fi 状态。
- 每 30 分钟自动刷新。
- BOOT 长按清除 NVS 并重启。

硬件说明书：
- ESP32-S3。
- LCD：ST7789，240x240，MOSI GPIO47，CLK GPIO21，CS GPIO44，DC GPIO43，背光 GPIO46。
- BOOT：GPIO0。
- 电量显示复用项目/BSP 中已有逻辑，不要猜 ADC 引脚。

通信与参数：
- Wi-Fi STA 连接路由器。
- HTTP 请求和风天气 API。
- SNTP/NTP 自动校时。
- LVGL 构建 UI。
- 天气图标等资源可用 mmap assets 或项目现有资源方案。

软件边界：
- ESP-IDF。
- Wi-Fi SSID/Password 用 `protocol_examples_common` 配置。
- QWeather Key/API Host 用 menuconfig 配置。
- 不加入语音识别、TTS、LLM、ES8311、I2S、摄像头等无关模块。

坑点：
- 网络失败先看 Wi-Fi 日志，再看 API key，再看 HTTP 返回。
- 不应出现 AFE_SR、wakenet、ES8311、I2S、MODEL_LOADER 日志。
- API key 不要直接提交到公开仓库。

验收：
- 串口显示 Wi-Fi connected、weather event、天气数据。
- LCD 显示天气 UI 并定时刷新。

请先输出模块划分：Wi-Fi、SNTP、Weather Service、Display/UI、Power，再生成代码或修改方案。
```

### 11.5 项目 B：最终展示版 `sparkbot_voice_chassis` 投喂卡

```text
我已经完成过 sparkbot_voice_chassis，现在要在它的基础上优化成课堂最终展示版。

目标现象：
- 离线语音控制底盘。
- LCD 状态清晰。
- 运动命令自动停止。
- 灯光命令保持用户选择，不被自动恢复覆盖。
- 新增“原地转圈”：`yuan di zhuan quan` -> `x1.0 y0.0` 持续 1200ms 后停止。

硬件说明书：
- ESP32-S3 头部板。
- 音频 ES8311：I2C GPIO5/GPIO4，I2S GPIO45/39/41/42/40。
- LCD ST7789：GPIO47/21/44/43/46。
- UART1 到底盘：TX GPIO38，RX GPIO48，115200，8N1，共地。

软件边界：
- 保持 ESP-SR 离线识别。
- 修改命令枚举、命令名、拼音表、处理函数、README。
- 只做最小修改，不重写全项目。

验收：
- 新命令能识别。
- 底盘原地转圈约 1200ms 后停止。
- README 命令表同步。

请先指出需要修改哪些文件和哪些表，再给出最小补丁方案。
```

### 11.6 项目 C：`tank/sparkbot_motion_control` 投喂卡

```text
我已经完成过 sparkbot_voice_chassis，也理解了 LCD、音频、UART、Wi-Fi/Web 的基础项目；现在要在这些基础上理解并复现 tank/sparkbot_motion_control 综合项目。

目标现象：
- ESP32-S3 头部板作为小车主控。
- 通过 UART 控制履带底盘。
- 支持 Hi 乐鑫语音控制。
- 支持手机/电脑连接 ESP-SparkBot Wi-Fi AP。
- 浏览器打开 192.168.4.1 后通过 Web 控制界面操作小车。
- 如果摄像头启用，支持图传或拍照。

硬件说明书：
- 底盘 UART：TX GPIO38，RX GPIO48，115200，8N1，共地。
- LCD：ST7789，GPIO47/21/44/43/46。
- 音频：ES8311，I2C GPIO5/GPIO4，I2S GPIO45/39/41/42/40。
- 摄像头 OV2640：XCLK GPIO15，PCLK GPIO13，VSYNC GPIO6，HSYNC GPIO7，D0-D7 GPIO11/9/8/10/12/18/17/16。

通信与模块：
- 语音：ESP-SR 离线识别。
- 底盘：UART ASCII 命令。
- Web：Wi-Fi AP + WebServer/WebSocket。
- 摄像头：OV2640 图像采集，图像缓冲常需要 PSRAM。
- 前端资源：`web_remote_control`、`spiffs` 或项目现有资源方案。

软件边界：
- 先阅读已有工程结构，不要让 AI 重写整个项目。
- 分清 main、components、web_remote_control、spiffs、managed_components。
- 若做最小复现，优先保留 UART 底盘控制和 Web 控制，摄像头/语音按时间选做。

坑点：
- 综合项目依赖多，先确认能 build 原项目。
- AP 模式和 STA 联网不要混淆。
- 网页资源、摄像头资源、语音模型都可能影响分区。
- 能打开网页不代表 UART 命令能到底盘，仍要查串口和磁吸接口。

验收：
- 手机能连接 AP 并打开 192.168.4.1。
- 网页控制能让底盘运动。
- 串口能看到 Web/语音转换成 UART 命令。

请按“模块 / 关键文件 / 运行流程 / 可能失败点 / 最小复现路线”的格式输出，不要直接重写项目。
```

## 12. 学生调试提示词模板

### 12.1 编译错误

```text
我在 ESP-IDF v5.x 中运行 idf.py build 出现错误。
请只根据下面错误做最小修复，不要改变项目目标，不要重写整个项目。

项目目标：
【简述目标】

硬件和协议：
【粘贴本项目投喂卡中的硬件说明书】

错误日志：
【粘贴完整错误】

请按“错误位置 / 根本原因 / 需要修改的文件 / 最小修改代码”输出。
```

### 12.2 硬件现象不对

```text
程序已经 build 和 flash，但硬件现象不符合预期。
请按单片机调试顺序排查：供电和接线 -> 串口日志 -> 引脚定义 -> 通信协议 -> 参数设置 -> 代码逻辑。

预期现象：
【写清楚】

实际现象：
【写清楚，例如屏幕不亮、颜色不对、无声音、触摸无反应、底盘不动】

串口日志：
【粘贴关键日志】

硬件说明：
【粘贴本项目投喂卡】

请给出优先级排查清单，每一步说明应该观察什么、改什么、不要改什么。
```

### 12.3 让 AI 做代码审查

```text
请审查这个 ESP-SparkBot ESP-IDF 项目，重点不是代码风格，而是嵌入式风险。

请检查：
1. 是否误用 Arduino、PlatformIO、TFT_eSPI。
2. 是否用错 ESP-SparkBot 引脚。
3. 是否混淆 SPI/I2C/I2S/UART/Touch。
4. 是否缺少 CMake 组件依赖。
5. 是否存在过大的栈数组或整屏 framebuffer。
6. 是否在 ISR/回调里做了耗时操作。
7. 是否没有等待 DMA/SPI/I2S 传输完成。
8. 是否遗漏 Kconfig、partition、assets 嵌入。
9. 是否有硬编码 Wi-Fi/API key。
10. README 是否和真实行为一致。

请按“问题 / 风险 / 修改建议 / 涉及文件”输出。
```

## 13. 学生提交要求

| 提交物 | 要求 |
| --- | --- |
| 可运行项目 | 能 `idf.py build`，并能在硬件上展示目标现象 |
| 提示词记录 | 至少保留 3 轮：需求投喂、编译报错修复、硬件现象排查 |
| 硬件说明 | 能说清芯片、引脚、协议、电气限制 |
| 软件说明 | 能说清入口文件、组件依赖、Kconfig、分区或资源 |
| 故障记录 | 至少记录 1 个 AI 跑偏或硬件调试问题 |
| 展示 | 现场演示或视频证明硬件现象真实发生 |

## 14. 教师评价建议

| 维度 | 占比 | 观察点 |
| --- | --- | --- |
| 硬件投喂准确性 | 20% | 芯片、引脚、协议、电气限制是否说清 |
| 软件边界清晰度 | 20% | 是否坚持 ESP-IDF，组件/工程结构是否合理 |
| 可运行结果 | 30% | 是否成功构建、烧录、演示目标现象 |
| Vibe Coding 过程 | 20% | 是否逐步拆需求、粘贴真实报错、纠正 AI 跑偏 |
| 课堂表达 | 10% | 是否能解释原理，而不是只说“AI 写的” |

## 15. 最终给学生的一句话

Vibe Coding 做单片机，不是“让 AI 猜代码”，而是“把硬件事实、通信协议、软件边界、参数坑和真实现象持续喂给 AI”。你喂得越像工程师，AI 越像工程助手。
