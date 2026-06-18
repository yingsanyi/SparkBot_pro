# ESP-SparkBot 语音控制履带小车

这个项目运行在 ESP-SparkBot 的 ESP32-S3 主控头部板上。它使用板载麦克风做离线语音识别，识别到中文指令后，通过 UART 把简单的文本命令发送给履带底盘主控，从而控制小车前进、后退、转向、停止、灯光和跳舞。

项目当前已经去掉语音提示音，改成更适合现场演示的反馈方式：

- 唤醒后，屏幕表情切换到“询问/聆听”状态。
- 识别到指令后，屏幕眼睛变成“OK/执行”状态。
- 识别到指令后，底盘灯光快速闪一下作为确认反馈，不额外播报语音。
- 运动类指令会短时间执行，然后自动停止。

## 快速使用

1. 给 ESP-SparkBot 头部板和履带底盘接好磁吸接口，确保 5V 和 GND 接触正常。
2. 编译、烧录并打开串口监视器：

```powershell
cd E:\practiceWeek\codes\get_start\sparkbot_voice_chassis
idf.py build
idf.py flash monitor
```

3. 对着麦克风说唤醒词：`Hi 乐鑫`。
4. 屏幕出现聆听状态后，说下面表格里的中文指令。

## 语音指令

唤醒词：`Hi 乐鑫`

唤醒后可说的命令如下：

| 中文口令 | 注册到 MultiNet 的拼音 | 发给底盘的 UART 命令 | 效果 |
| --- | --- | --- | --- |
| 向前冲 | `xiang qian chong` | `x0.0 y1.0` | 前进一小段后停止 |
| 向后退 | `xiang hou tui` | `x0.0 y-1.0` | 后退一小段后停止 |
| 向左转 | `xiang zuo zhuan` | `x-1.0 y0.0` | 左转一小段后停止 |
| 向右转 | `xiang you zhuan` | `x1.0 y0.0` | 右转一小段后停止 |
| 停止运动 | `ting zhi yun dong` | `x0.0 y0.0` | 立即停止 |
| 闪光模式 | `shan guang mo shi` | `w3` | 切换到底盘闪光灯效 |
| 呼吸模式 | `hu xi mo shi` | `w5` | 切换到底盘呼吸灯效 |
| 常亮模式 | `chang liang mo shi` | `w2` | 切换到底盘常亮灯效 |
| 灯光秀 | `deng guang xiu` | `w7` | 切换到底盘灯光秀 |
| 跳个舞 | `tiao ge wu` | `d1` | 执行底盘内置跳舞动作 |

注意：运动命令里 `x` 和 `y` 中间必须有一个空格，例如 `x0.0 y1.0`。底盘参考代码使用 `sscanf(data, "x%f y%f", ...)` 解析命令，如果写成 `x0.0y1.0`，底盘可能无法识别。

## 硬件连接

ESP-SparkBot 头部板通过底部 4P 磁吸接口连接履带底盘。这个接口同时提供供电和 UART 通讯。

| 功能 | ESP-SparkBot 头部板 | 履带底盘侧 |
| --- | --- | --- |
| 电源 | 5V | 5V |
| 共地 | GND | GND |
| UART TX | GPIO38 | 接到底盘 RX |
| UART RX | GPIO48 | 接到底盘 TX |

UART 参数：

- 串口号：`UART_NUM_1`
- 波特率：`115200`
- 数据格式：`8N1`，也就是 8 位数据位、无校验、1 位停止位
- 流控：关闭

代码位置：

- 头部板 UART 发送：`components/tracked_chassis_control/tracked_chassis_control.c`
- 头部板 UART 引脚：`components/tracked_chassis_control/include/tracked_chassis_control.h`
- 参考底盘解析代码：`E:\practiceWeek\codes\esp_sparkbot-master\example\tank\c2_tracked_chassis\main\tracked_chassis.c`

## 其他板载引脚

### 屏幕

屏幕是 240x240 的 SPI LCD，当前项目不用 LVGL，而是直接用 `esp_lcd` 画表情。

| 功能 | GPIO |
| --- | --- |
| LCD MOSI | GPIO47 |
| LCD CLK | GPIO21 |
| LCD CS | GPIO44 |
| LCD DC | GPIO43 |
| LCD RST | 未使用，`GPIO_NUM_NC` |
| LCD 背光 | GPIO46 |

代码位置：`components/sparkbot_lcd/sparkbot_lcd.c`

### 音频和麦克风

语音识别使用 ES8311 音频 Codec，通过 I2C 配置 Codec，通过 I2S 读取麦克风音频。

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

代码位置：`components/sparkbot_audio/include/sparkbot_audio.h`

## 工作原理

可以把整个系统理解成 5 个步骤：

1. 麦克风采集声音  
   `audio_feed_task` 不断从 ES8311 读取 16 kHz、16 bit 的音频数据，然后喂给 ESP-SR 的 AFE 前端。

2. WakeNet 监听唤醒词  
   `audio_detect_task` 一直检测 `Hi 乐鑫`。没有唤醒时，只做唤醒词检测，不执行命令。

3. MultiNet 识别命令词  
   唤醒成功并确认声道后，程序关闭 WakeNet，进入 MultiNet 命令识别状态。当前命令词在 `main/voice_chassis_main.c` 的 `s_command_phrases` 里注册。

4. 事件队列转给控制任务  
   识别结果会通过 FreeRTOS 队列 `s_sr_event_queue` 发送给 `sr_event_handler_task`。这样语音识别任务和底盘控制任务互不直接阻塞。

5. UART 发送文本命令到底盘  
   `handle_voice_command()` 根据命令 ID 决定发送什么字符串，例如 `x0.0 y1.0`、`w3`、`d1`。底盘主控收到字符串后解析并控制电机或灯光。

## UI 和灯光反馈

LCD 状态：

| 状态 | 含义 |
| --- | --- |
| `BOOT` | 正在初始化 |
| `HI LEXIN` | 等待唤醒 |
| `ASK` | 已唤醒，等待口令 |
| `LISTEN` | 正在识别命令 |
| `OK` | 已识别并执行 |
| `TIMEOUT` | 唤醒后超时未识别 |
| `ERROR` | 命令异常 |

底盘灯光反馈：

- 开机默认发送 `w2`，让灯光常亮。
- 唤醒成功发送 `w3`，表示进入等待命令状态。
- 声道确认后发送 `w5`，表示开始听命令。
- 识别到命令后发送 `w3` 并等待约 `110 ms`，作为快速确认闪烁。
- 运动结束或超时后发送 `w2` 回到常亮。

## UART 协议说明

这个项目的通讯协议非常简单，都是 ASCII 文本。

### 运动命令

格式：

```text
x<左右转向> y<前后速度>
```

例子：

```text
x0.0 y1.0
x0.0 y-1.0
x-1.0 y0.0
x1.0 y0.0
x0.0 y0.0
```

含义：

- `x` 控制左右转向。
- `y` 控制前后运动。
- `x=0.0, y=1.0` 表示直行前进。
- `x=0.0, y=-1.0` 表示直行后退。
- `x=-1.0, y=0.0` 表示左转。
- `x=1.0, y=0.0` 表示右转。
- `x=0.0, y=0.0` 表示停止。

### 灯光命令

格式：

```text
w<灯光模式编号>
```

常用模式：

| 命令 | 含义 |
| --- | --- |
| `w2` | 常亮 |
| `w3` | 闪光 |
| `w5` | 呼吸 |
| `w7` | 灯光秀 |

### 跳舞命令

格式：

```text
d<舞蹈模式编号>
```

当前项目使用：

```text
d1
```

## 重要源码入口

| 文件 | 作用 |
| --- | --- |
| `main/voice_chassis_main.c` | 主流程：语音识别、命令注册、事件处理、调用底盘控制 |
| `components/tracked_chassis_control/tracked_chassis_control.c` | UART 初始化和命令发送 |
| `components/tracked_chassis_control/include/tracked_chassis_control.h` | UART 引脚定义 |
| `components/sparkbot_lcd/sparkbot_lcd.c` | LCD 表情 UI |
| `components/sparkbot_audio/sparkbot_audio.c` | 麦克风、Codec、I2S、I2C 初始化 |
| `main/Kconfig.projbuild` | 可配置参数，例如运动时长、麦克风增益、LCD 开关 |

## 常用配置

可以通过 `idf.py menuconfig` 修改，也可以直接看 `main/Kconfig.projbuild`。

| 配置项 | 默认值 | 说明 |
| --- | --- | --- |
| `CONFIG_SPARKBOT_VOICE_CHASSIS_MOVE_MS` | `550` | 前进/后退执行时长，单位 ms |
| `CONFIG_SPARKBOT_VOICE_CHASSIS_TURN_MS` | `650` | 左转/右转执行时长，单位 ms |
| `CONFIG_SPARKBOT_VOICE_CHASSIS_MIC_GAIN` | `35` | 麦克风输入增益 |
| `CONFIG_SPARKBOT_VOICE_CHASSIS_LCD_ENABLE` | `y` | 是否启用 LCD 表情 |
| `CONFIG_SPARKBOT_VOICE_CHASSIS_WAKE_PROMPT_ENABLE` | `n` | 是否启用唤醒提示音，当前建议关闭 |
| `CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_TIMEOUT_MS` | `5760` | 唤醒后等待命令识别的超时时间 |
| `CONFIG_SPARKBOT_VOICE_CHASSIS_MN_THRESHOLD_PERCENT` | `0` | MultiNet 阈值，0 表示使用模型默认值 |

## 如何用 vibe coding 继续开发

后续想改功能时，建议把需求描述成“改哪个口令、识别后发什么 UART、UI/灯光要怎么反馈”。这样 AI 更容易准确修改。

示例需求：

```text
请在 main/voice_chassis_main.c 里新增一个语音指令“原地转圈”，
拼音注册为 yuan di zhuan quan，
识别后发送 x1.0 y0.0 持续 1200ms，然后停止。
README 也同步更新。
```

再比如：

```text
请把识别成功后的灯光反馈改成 w3 快闪 80ms，不要影响运动命令和灯光模式命令本身。
```

修改语音命令时一般要动这几个地方：

1. `voice_command_t` 里新增枚举值。
2. `s_command_names` 里新增显示名称。
3. `s_command_phrases` 里新增拼音命令词。
4. `handle_voice_command()` 里新增执行逻辑。
5. `README.md` 的命令表同步更新。

## 常见问题

### 1. 能唤醒，但是底盘不动

先看串口日志里有没有类似：

```text
Voice command: turn left
Sent chassis command: x-1.0 y0.0
```

如果有，说明语音识别和头部板 UART 发送基本正常，下一步重点检查：

- 磁吸接口是否接触良好。
- 头部板 GND 是否和底盘 GND 共地。
- GPIO38/GPIO48 是否接反。
- 底盘固件是否烧录的是支持 UART 控制的版本。
- 运动命令是否带空格，例如 `x0.0 y1.0`。

### 2. 能唤醒，但命令识别率低

可以尝试：

- 靠近麦克风说话。
- 降低环境噪声。
- 在 `menuconfig` 里调整 `CONFIG_SPARKBOT_VOICE_CHASSIS_MIC_GAIN`。
- 使用更短、更清晰、和已有命令差异更大的中文口令。

### 3. 灯光命令一闪就被改回常亮

这个问题通常是代码里在所有命令后都统一发送了 `w2`。当前项目已经避免这种写法：只有运动结束、停止、超时时才回到 `w2`；灯光模式命令会保留用户指定的灯效。

### 4. 为什么不用语音提示音

提示音会占用扬声器/音频链路，也可能影响麦克风再次识别。当前版本用 LCD 表情和底盘灯光作为反馈，更稳定，也更适合连续语音控制。
