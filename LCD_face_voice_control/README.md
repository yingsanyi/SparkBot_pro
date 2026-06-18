# ESP-SparkBot 语音切换 LCD 表情

这个项目是在 `LCD_face_cycle` 的基础上继续扩展出来的：原来的项目会按固定时间自动轮播表情，这个项目改成使用板载麦克风做离线语音识别，听到中文口令后切换到指定的 LCD 表情。

它参考了 `sparkbot_voice_chassis` 的语音识别链路，但去掉了底盘 UART 控制，只保留：

- ESP-SR WakeNet 唤醒词检测
- ESP-SR MultiNet 中文命令词识别
- ES8311 Codec + I2S 麦克风采集
- 240x240 SPI LCD 表情渲染

整个过程不需要联网，也不依赖云端语音服务。

## 项目效果

1. 上电后 LCD 显示 `HI LEXIN / WAITING`。
2. 对着麦克风说唤醒词：`Hi 乐鑫`。
3. 屏幕显示 `ASK / COMMAND`，然后进入 `LISTEN / COMMAND`。
4. 继续说表情命令，例如 `开心表情`。
5. LCD 切换到对应表情，并保持这个表情，直到下一次唤醒和命令。

## 语音命令

唤醒词：`Hi 乐鑫`

唤醒后可说的命令如下：

| 中文口令 | 注册到 MultiNet 的拼音 | LCD 表情 |
| --- | --- | --- |
| 待机表情 | `dai ji biao qing` | `IDLE` |
| 开心表情 | `kai xin biao qing` | `HAPPY` |
| 惊讶表情 | `jing ya biao qing` | `WOW` |
| 困了表情 | `kun le biao qing` | `SLEEPY` |
| 生气表情 | `sheng qi biao qing` | `ANGRY` |
| 眨眼表情 | `zha yan biao qing` | `WINK` |
| 开机表情 | `kai ji biao qing` | `BOOT` |
| 下一个表情 | `xia yi ge biao qing` | 按内部表情表切到下一个 |

## 硬件引脚

### LCD

LCD 是 240x240 SPI 屏，驱动芯片按 ST7789 初始化。

| 功能 | GPIO |
| --- | --- |
| LCD MOSI | GPIO47 |
| LCD CLK | GPIO21 |
| LCD CS | GPIO44 |
| LCD DC | GPIO43 |
| LCD RST | 未使用，`GPIO_NUM_NC` |
| LCD 背光 | GPIO46 |

### 音频和麦克风

语音识别使用板载 ES8311 音频 Codec。ESP32-S3 通过 I2C 配置 ES8311，通过 I2S 从麦克风读取 16 kHz、16 bit 的音频数据。

| 功能 | GPIO |
| --- | --- |
| I2S MCLK | GPIO45 |
| I2S SCLK/BCLK | GPIO39 |
| I2S DOUT | GPIO42 |
| I2S LRCK/WS | GPIO41 |
| I2S DSIN | GPIO40 |
| I2C SCL | GPIO5 |
| I2C SDA | GPIO4 |
| 功放控制 | 未使用，`GPIO_NUM_NC` |

## 编译和烧录

```powershell
cd E:\practiceWeek\codes\SparkBot_pro\LCD_face_voice_control
idf.py set-target esp32s3
idf.py build flash monitor
```

如果已经配置过目标芯片，也可以直接：

```powershell
idf.py build
idf.py flash monitor
```

这个项目使用 `partitions.csv` 增加了 `model` 分区，用来存放 ESP-SR 的语音模型。`sdkconfig.defaults` 已经打开了：

- `CONFIG_USE_MULTINET`
- `CONFIG_SR_WN_WN9_HILEXIN`
- `CONFIG_SR_MN_CN_MULTINET7_QUANT`
- `CONFIG_CODEC_ES8311_SUPPORT`
- PSRAM 相关配置

## 工作原理

可以把整个项目理解成 4 层：

1. LCD 表情层  
   `components/lcd_face_ui` 负责初始化 SPI 总线、创建 ST7789 面板、点亮背光，并用 `esp_lcd_panel_draw_bitmap()` 分块刷新屏幕。每个表情不是图片文件，而是一组绘制参数，比如眼睛开合程度、瞳孔偏移、眉毛角度、嘴型和颜色。

2. 音频采集层  
   `components/sparkbot_audio` 负责初始化 I2C、I2S 和 ES8311 Codec。`audio_feed_task` 不断从麦克风读音频，把单声道麦克风数据扩展成 AFE 需要的双通道格式，再喂给 ESP-SR 的 AFE 前端。

3. 语音识别层  
   `audio_detect_task` 从 AFE 取出处理后的音频。没有唤醒时，它主要看 WakeNet 是否检测到 `Hi 乐鑫`。唤醒后程序关闭 WakeNet，把音频交给 MultiNet 识别命令词。识别结果不会直接改屏幕，而是封装成事件放进 FreeRTOS 队列。

4. 表情控制层  
   `sr_event_handler_task` 从队列取事件。唤醒时显示 `ASK`，声道确认后显示 `LISTEN`，命令识别成功后调用 `handle_voice_command()`。这个函数把命令 ID 映射成 `lcd_face_ui_show_scene()`，于是 LCD 切换到指定表情。

这样拆分后，语音识别任务、LCD 刷新任务和业务控制逻辑不会互相缠在一起。后续继续改功能时，通常只需要改命令表和 `handle_voice_command()`。

## 重要源码入口

| 文件 | 作用 |
| --- | --- |
| `main/lcd_face_voice_main.c` | 主流程：初始化 NVS/LCD/ESP-SR，注册语音命令，处理识别事件 |
| `main/Kconfig.projbuild` | 项目配置：麦克风增益、唤醒模式、命令超时、MultiNet 阈值 |
| `components/lcd_face_ui/lcd_face_ui.c` | LCD 初始化和表情绘制 |
| `components/lcd_face_ui/include/lcd_face_ui.h` | 表情枚举和显示接口 |
| `components/sparkbot_audio/sparkbot_audio.c` | ES8311、I2S、I2C 初始化和麦克风读取支持 |
| `CMakeLists.txt` | 引入 ESP-SR、ESP-DSP、esp_codec_dev 本地 managed components |

## 从基础项目继续 vibe coding

如果你只有 `LCD_face_cycle` 这样的基础 LCD 项目，要继续做成这个语音项目，可以按下面的顺序给 AI 提需求：

1. 先让 AI 保留 `lcd_face_ui` 组件，不要改 LCD 引脚和 ST7789 初始化。
2. 再让 AI 从语音参考项目复制或复用 `sparkbot_audio`，确保 ES8311、I2C、I2S 引脚一致。
3. 加入 ESP-SR 依赖，在顶层 `CMakeLists.txt` 里引入 `espressif__esp-sr`、`espressif__esp-dsp`、`espressif__esp_codec_dev`。
4. 使用自定义分区表，给语音模型留出 `model` 分区。
5. 在主程序里创建 3 个任务：`audio_feed_task`、`audio_detect_task`、`sr_event_handler_task`。
6. 把原来 `while` 循环自动切换表情的逻辑，改成 `handle_voice_command()` 里按命令切换表情。
7. 最后同步更新 README，写清楚口令、拼音、效果、改代码入口。

一个比较好用的 vibe coding 提示词示例：

```text
请基于 LCD_face_cycle 新建 LCD_face_voice_control。
保留 lcd_face_ui 组件和 LCD 引脚。
参考 sparkbot_voice_chassis 的 ESP-SR 语音识别流程，但不要控制底盘 UART。
唤醒词使用 Hi 乐鑫，命令包括 开心表情、惊讶表情、困了表情、生气表情、眨眼表情、待机表情、下一个表情。
识别成功后调用 lcd_face_ui_show_scene 切换表情，并保持当前表情。
README 用中文详细写硬件、原理、命令表和后续扩展方法。
```

## 增加新表情命令的方法

假设要新增 `难过表情`，一般要改 4 个地方：

1. 在 `voice_command_t` 里新增枚举，例如 `CMD_FACE_SAD`。
2. 在 `s_command_names` 里新增显示名，例如 `"sad"`。
3. 在 `s_command_phrases` 里新增拼音命令，例如 `{CMD_FACE_SAD, "nan guo biao qing"}`。
4. 在 `handle_voice_command()` 的 `switch` 里新增分支，调用对应的 `lcd_face_ui_show_scene()`。

如果 LCD 组件里还没有这个表情，则还要继续改：

1. 在 `components/lcd_face_ui/include/lcd_face_ui.h` 的 `lcd_face_scene_t` 里新增场景。
2. 在 `components/lcd_face_ui/lcd_face_ui.c` 的 `face_style_for_scene()` 里给新场景配置眼睛、眉毛和嘴型。

## 常用配置

可以通过 `idf.py menuconfig` 修改：

| 配置项 | 默认值 | 说明 |
| --- | --- | --- |
| `CONFIG_LCD_FACE_VOICE_MIC_GAIN` | `30` | 麦克风输入增益，环境吵时可以适当调高或调低 |
| `CONFIG_LCD_FACE_VOICE_WAKE_MODE_90` | `y` | 使用和参考语音项目一致的唤醒检测模式 |
| `CONFIG_LCD_FACE_VOICE_COMMAND_TIMEOUT_MS` | `5760` | 唤醒后等待命令词的超时时间 |
| `CONFIG_LCD_FACE_VOICE_MN_THRESHOLD_PERCENT` | `0` | MultiNet 阈值，0 表示使用模型默认值 |

## 常见问题

### 1. LCD 能亮，但语音没有反应

先看串口日志是否有：

```text
Speech detection started. Say wake word: Hi Lexin
```

如果没有，优先检查 ESP-SR 依赖、模型分区和 `sdkconfig.defaults`。如果有这行但唤醒不了，靠近麦克风说 `Hi 乐鑫`，并检查麦克风增益。

### 2. 能唤醒，但命令识别率低

可以尝试：

- 说完唤醒词后停顿半秒再说命令。
- 命令尽量按表格里的中文说，不要临时换说法。
- 降低环境噪声。
- 在 `menuconfig` 中调整 `CONFIG_LCD_FACE_VOICE_MIC_GAIN`。
- 给新增命令选更短、更清楚、互相差异更大的中文口令。

### 3. 识别成功后为什么不回到待机脸

这是有意设计。这个项目的目标是“语音切换不同表情”，所以识别成功后会保留目标表情。如果希望几秒后自动回到待机，可以在 `sr_event_handler_task` 的 `ESP_MN_STATE_DETECTED` 分支后面加延时，再调用：

```c
lcd_face_ui_show_scene(LCD_FACE_SCENE_IDLE, "HI LEXIN", "WAITING");
```

### 4. 为什么不用语音播报反馈

当前项目优先保持语音识别稳定。提示音会占用扬声器和音频链路，也可能影响麦克风再次识别。这里直接用 LCD 的 `ASK`、`LISTEN`、目标表情作为反馈，更适合连续调试和课堂演示。
