# ESP-SparkBot Web Car Control

这个项目是一个轻量级 Web 小车控制教学工程：手机或电脑连接 ESP-SparkBot 创建的热点，浏览器打开控制页面，就可以控制履带小车运动，并切换底盘 RGB 灯光模式。

本项目刻意不启用摄像头、语音识别、触摸按键和 LVGL。课堂重点放在这一条链路上：

```text
网页摇杆/按钮 -> WebSocket 或 HTTP API -> ESP32-S3 程序解析命令
             -> UART(GPIO38/GPIO48) -> 底盘 ESP32-C2 -> 电机和 RGB 灯
```

## 目标现象

- ESP32-S3 启动后创建控制热点，名称类似 `SparkBot-Car-A1B2`。
- LCD 点亮并显示 Web 控制状态。
- 手机或电脑连接该热点后访问 `http://192.168.4.1`。
- 网页显示摇杆、方向键、速度滑杆、灯光模式按钮和状态信息。
- 拖动摇杆或按方向键时，小车前进、后退、左转、右转。
- 松开摇杆或方向键后，小车停止。
- 点击灯光模式按钮后，底盘 RGB 灯切换效果。
- 浏览器断开或长时间没有运动命令时，ESP32-S3 自动发送停止命令。

## LCD 状态反馈

本项目会初始化 ESP-SparkBot 头部的 ST7789 LCD，并打开背光。屏幕不是主要交互入口，只负责给学生一个现场状态反馈：

| 场景 | LCD 显示 |
| --- | --- |
| 开机初始化 | `WEB CAR / BOOT` |
| SoftAP 启动完成 | `OPEN WEB / 192.168.4.1` |
| 手机或电脑连上热点 | `PHONE / CONNECTED` |
| 小车正在运动 | `MOVING / WEB CONTROL` |
| 松手停止 | `READY / OPEN WEB` |
| 灯光模式切换 | `LIGHT / MODE n` |
| 跳舞命令 | `DANCE / CHASSIS` |
| 失联自动停止 | `STOP / TIMEOUT` |

## 硬件通信模型

ESP-SparkBot 是上下两层结构：

| 模块 | 作用 |
| --- | --- |
| 头部 ESP32-S3 | 提供 WiFi/Web 页面，解析网页命令，通过 UART 发给底盘 |
| 底盘 ESP32-C2 | 接收 UART 字符串命令，驱动左右履带电机和 WS2812 RGB 灯 |

头部主控和底盘通过 4P 磁吸接口通信：

| 头部主控侧 | 作用 |
| --- | --- |
| `GPIO38` | UART TX，发送命令到底盘 |
| `GPIO48` | UART RX，接收底盘返回数据，本项目暂不解析 |
| `5V` | 电源通路 |
| `GND` | 公共地 |

本项目不要把电机写成 ESP32-S3 直接 PWM 控制。电机和 RGB 灯由底盘 ESP32-C2 执行，头部主控只发送协议命令。

## UART 命令协议

底盘已经支持字符串命令：

| 命令 | 示例 | 含义 |
| --- | --- | --- |
| `x<float> y<float>` | `x0.00 y1.00` | 运动控制，`x` 控制转向，`y` 控制前进/后退 |
| `w<number>` | `w3` | 切换 RGB 灯效模式 |
| `d<number>` | `d1` | 触发跳舞动作 |
| `c<float>` | `c0.02` | 调整左右电机速度修正量 |

运动向量范围是 `-1.00` 到 `1.00`：

| 输入 | 现象 |
| --- | --- |
| `x0.00 y1.00` | 前进 |
| `x0.00 y-1.00` | 后退 |
| `x-1.00 y0.00` | 左转 |
| `x1.00 y0.00` | 右转 |
| `x0.00 y0.00` | 停止 |

## 软件流程

```text
app_main()
  |
  +-- 初始化 NVS
  +-- 初始化 LCD
  +-- 初始化到底盘的 UART1(GPIO38/GPIO48, 115200)
  +-- 启动 SoftAP
  +-- 启动 HTTP Server
  +-- 启动 motion_keepalive 任务

浏览器访问 /
  |
  +-- 下载内嵌 index.html
  +-- 建立 WebSocket: /ws
  +-- 摇杆持续发送 x/y
  +-- 灯光按钮发送 w 模式
```

## 安全停止设计

Web 控制小车时必须考虑“失联还继续跑”的风险。本项目有两层保护：

1. 前端在松开摇杆或方向键时发送 `stop`。
2. 后端 `motion_keepalive` 任务如果超过 `CONFIG_SPARKBOT_WEB_CAR_DEADMAN_MS` 没收到新运动命令，就发送 `x0.00 y0.00`。

默认超时时间是 `650 ms`，课堂上可以让学生观察调大或调小后的手感差异。

## WebSocket 消息

实时运动控制走 `/ws`：

| 消息 | 示例 | 后端动作 |
| --- | --- | --- |
| `x<float> y<float>` | `x0.50 y0.80` | 更新运动状态并发送到底盘 |
| `stop` | `stop` | 发送 `x0.00 y0.00` |
| `w<number>` | `w5` | 切换灯光 |
| `d<number>` | `d1` | 触发跳舞 |
| `c<float>` | `c-0.02` | 底盘跑偏修正 |
| `PING` | `PING` | 心跳消息，当前只用于保持连接活跃 |

## HTTP API

为了便于调试和教学，本项目同时提供 HTTP API。

| API | 方法 | 用途 |
| --- | --- | --- |
| `/` | GET | 返回控制网页 |
| `/api/status` | GET | 返回当前状态 |
| `/api/move` | POST | 表单参数 `x=0.0&y=1.0&speed=70` |
| `/api/stop` | POST | 立即停止 |
| `/api/light` | POST | 表单参数 `mode=3` |
| `/api/dance` | POST | 表单参数 `mode=1` |
| `/api/correction` | POST | 表单参数 `value=0.02` |

## 文件结构

| 文件 | 作用 |
| --- | --- |
| `main/sparkbot_web_car_control_main.c` | 主程序：SoftAP、HTTP Server、WebSocket、状态机、安全停止 |
| `main/index.html` | 内嵌网页：摇杆、方向键、速度滑杆、灯光模式按钮 |
| `main/Kconfig.projbuild` | 可配置热点名称、密码、端口、命令周期和超时时间 |
| `components/tracked_chassis_control/` | UART 底盘控制组件 |
| `components/lcd_face_ui/` | LCD 初始化、背光打开和表情状态显示 |
| `sdkconfig.defaults` | ESP32-S3、WiFi SoftAP、WebSocket 等默认配置 |

## 编译和烧录

在当前机器上，如果 ESP-IDF 环境还没有导入，先执行：

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'
```

这台机器上对应的 ESP-IDF 路径是 `D:\espidf\v5.5.4\esp-idf`，Python 环境是 `C:\Espressif\tools\python\v5.5.4\venv`。

然后编译：

```powershell
cd E:\practiceWeek\codes\SparkBot_pro\sparkbot_web_car_control
idf.py set-target esp32s3
idf.py build
```

烧录和串口监视：

```powershell
idf.py -p COM5 flash monitor
```

如果之前用过其他 ESP-IDF 版本构建，旧的 `build` 目录可能缓存了错误的 toolchain 路径。遇到 CMake 提示不同 IDF 版本混用时，运行：

```powershell
idf.py fullclean
idf.py build
```

## 使用方法

1. 确认 ESP-SparkBot 头部和底盘通过 4P 磁吸接口连接好。
2. 烧录本项目到头部 ESP32-S3。
3. 打开串口，等待看到 `SoftAP started` 和 `HTTP server started`。
4. 手机或电脑连接热点，名称类似 `SparkBot-Car-A1B2`。
5. 默认密码是 `12345678`。
6. 浏览器打开 `http://192.168.4.1`。
7. 拖动摇杆或按方向键控制运动。
8. 点击灯光模式按钮切换 RGB 灯效。

## 可配置项

运行：

```powershell
idf.py menuconfig
```

进入：

```text
SparkBot Web Car Control
```

可以修改：

| 配置项 | 默认值 | 说明 |
| --- | --- | --- |
| `SPARKBOT_WEB_CAR_AP_SSID_PREFIX` | `SparkBot-Car` | 控制热点前缀，程序会追加 MAC 后缀 |
| `SPARKBOT_WEB_CAR_AP_PASSWORD` | `12345678` | 控制热点密码 |
| `SPARKBOT_WEB_CAR_AP_CHANNEL` | `6` | SoftAP 信道 |
| `SPARKBOT_WEB_CAR_HTTP_PORT` | `80` | HTTP 服务端口 |
| `SPARKBOT_WEB_CAR_COMMAND_PERIOD_MS` | `100` | 运动命令重复发送周期 |
| `SPARKBOT_WEB_CAR_DEADMAN_MS` | `650` | 失联自动停止超时时间 |

## 串口验收

串口中应该能看到类似日志：

```text
ESP-SparkBot web car control starting
Chassis UART ready: TX GPIO38, RX GPIO48, 115200 baud
SoftAP started
HTTP server started on port 80
WebSocket connected
UART -> x0.00 y0.70
UART -> w3
```

验收清单：

- 手机能连接到 `SparkBot-Car-XXXX`。
- LCD 不再黑屏，开机后能看到 `WEB CAR`、`OPEN WEB` 等状态。
- 浏览器能打开 `http://192.168.4.1`。
- 页面顶部 WebSocket 状态显示 `ONLINE`。
- 拖动摇杆时串口持续打印 `UART -> x... y...`。
- 松开摇杆后串口打印 `UART -> x0.00 y0.00`。
- 小车能稳定前进、后退、左转、右转和停止。
- 点击灯光模式后串口打印 `UART -> w...`，底盘灯效变化。
- 断开浏览器或关闭手机 WiFi 后，小车会停止。

## 常见问题

### 能打开网页，但小车不动

优先检查：

- 头部和底盘是否通过 4P 磁吸接口正确连接。
- 底盘是否上电。
- 底盘 ESP32-C2 是否已经烧录支持 UART 命令的固件。
- 串口是否能看到 `UART -> x... y...`。如果能看到，说明头部主控已经发出命令，问题更可能在底盘侧。

### 网页显示 ONLINE，但控制有延迟

可以尝试：

- 手机靠近 ESP-SparkBot。
- 降低同一教室内同时开启的热点数量。
- 在 `menuconfig` 中把 `SPARKBOT_WEB_CAR_COMMAND_PERIOD_MS` 从 `100` 改成 `80`。

### 小车松手后还会滑一下

这是惯性和命令周期共同造成的。可以调小 `SPARKBOT_WEB_CAR_DEADMAN_MS`，也可以在前端松手时多发送一次 `stop`。本项目已经在前端和后端各做了一层停止保护。

## 课堂扩展任务

- 增加“低速/标准/高速”三档速度按钮。
- 在网页上显示最近 10 条 UART 命令日志。
- 增加简单鉴权，避免同一热点内其他人误操作。
- 把 HTTP API 改成 JSON 请求体，让学生练习 `cJSON` 解析。
- 让底盘通过 UART 回传电池电压，并显示在网页状态区。
- 增加自动巡航按钮，例如方形路线、原地旋转、灯光秀联动。

## 教学提醒

这节课最值得学生读懂的是“协议边界”：

```text
网页不直接控制电机
ESP32-S3 不直接输出电机 PWM
底盘 ESP32-C2 才是真正的执行层
```

头部主控负责把人的操作转换成简单、可验证的字符串命令。只要串口里能看到正确的 `x... y...` 和 `w...`，学生就能一步步定位问题发生在 Web、网络、UART 还是底盘执行层。
