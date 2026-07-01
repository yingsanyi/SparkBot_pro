# SparkBot Web Car Control

这个项目用于让手机或电脑直接连接 SparkBot 小车自己的 WiFi 热点，然后打开网页控制小车。项目是 AP-only 模式，不配置路由器 WiFi，也不需要让小车接入家庭或教室路由器。

## 使用流程

1. 烧录固件到 SparkBot 头部 ESP32-S3。
2. 小车启动后会创建自己的热点，热点名称格式为：

   ```text
   SparkBot-Car-<SoftAP MAC>
   ```

   例如：

   ```text
   SparkBot-Car-AABBCCDDEEFF
   ```

3. LCD 启动后会先显示：

   ```text
   JOIN AP
   SparkBot-Car-AABBCCDDEEFF
   ```

   这里显示的是当前这台小车的完整热点名。热点名带 MAC 后缀，主要是为了多台小车同时上电时不容易连错。

4. 用手机或电脑打开 WiFi 列表，连接 LCD 上显示的完整热点名。

   默认热点密码：

   ```text
   12345678
   ```

5. 手机或电脑连上小车热点后，LCD 会切换显示：

   ```text
   192.168.4.1
   OPEN WEB
   ```

6. 在浏览器中打开：

   ```text
   http://192.168.4.1
   ```

7. 网页打开后即可控制小车前进、后退、左转、右转、停止，并可切换底盘灯光模式。

手机提示“此网络无法访问互联网”是正常现象，因为这个热点只用于连接小车，不提供外网。

## LCD 提示含义

| 场景 | LCD 显示 | 用户应该做什么 |
| --- | --- | --- |
| 小车启动并创建热点 | `JOIN AP / SparkBot-Car-...` | 在手机或电脑 WiFi 列表中连接这个热点 |
| 手机或电脑已连上热点 | `192.168.4.1 / OPEN WEB` | 浏览器打开 `http://192.168.4.1` |
| 网页正在控制小车移动 | `MOVING / WEB CONTROL` | 正常操控 |
| 松开摇杆或点击停止 | `READY / OPEN WEB` | 小车已停止，网页仍可继续控制 |
| 长时间没有控制命令 | `STOP / TIMEOUT` | 自动停车保护已触发 |
| 切换灯光模式 | `LIGHT / MODE n` | 灯光命令已发送到底盘 |
| 触发舞蹈动作 | `DANCE / CHASSIS` | 舞蹈命令已发送到底盘 |

## 网页功能

打开 `http://192.168.4.1` 后，网页会显示当前小车热点名和访问地址，方便确认自己连接的是正确的小车。

网页包含这些控制项：

| 区域 | 功能 |
| --- | --- |
| 连接提示 | 显示小车热点名、控制网页地址、已连接设备数量 |
| 运动控制 | 摇杆、方向键、速度滑杆、停止按钮 |
| 灯光模式 | 切换底盘 RGB 灯效 |
| Trim | 微调左右电机修正值 |
| Dance | 发送底盘舞蹈命令 |

网页优先使用 WebSocket 实时发送控制命令；如果 WebSocket 不可用，会回退到 HTTP API。

## 重要说明

- 这个项目不再提供路由器 WiFi 配网界面。
- 小车不会连接家庭或教室路由器。
- 手机或电脑必须先连接小车热点，才能打开 `http://192.168.4.1` 控制小车。
- 多台小车同时使用时，请以 LCD 上显示的完整热点名为准。
- 断开网页、关闭浏览器、离开小车热点或长时间没有运动命令时，固件会自动发送停止命令。

## 默认配置

| 配置项 | 默认值 | 说明 |
| --- | --- | --- |
| `SPARKBOT_WEB_CAR_AP_SSID_PREFIX` | `SparkBot-Car` | 小车热点名前缀，固件会自动追加 `-<SoftAP MAC>` |
| `SPARKBOT_WEB_CAR_AP_PASSWORD` | `12345678` | 小车热点密码 |
| `SPARKBOT_WEB_CAR_AP_CHANNEL` | `6` | SoftAP 信道 |
| `SPARKBOT_WEB_CAR_HTTP_PORT` | `80` | Web 控制页面端口 |
| `SPARKBOT_WEB_CAR_COMMAND_PERIOD_MS` | `100` | 运动命令重复发送周期 |
| `SPARKBOT_WEB_CAR_DEADMAN_MS` | `650` | 没有新运动命令时的自动停车超时 |

如果修改了 `SPARKBOT_WEB_CAR_HTTP_PORT`，LCD 会显示带端口的访问地址，例如：

```text
192.168.4.1:8080
OPEN WEB
```

浏览器需要打开：

```text
http://192.168.4.1:8080
```

## Web API

| API | 方法 | 用途 |
| --- | --- | --- |
| `/` | GET | 返回网页控制界面 |
| `/api/status` | GET | 返回热点名、访问地址、连接设备数和小车状态 |
| `/ws` | WebSocket | 实时运动、灯光、舞蹈、修正命令 |
| `/api/move` | POST | 表单参数 `x=0.0&y=1.0&speed=70` |
| `/api/stop` | POST | 立即停止 |
| `/api/light` | POST | 表单参数 `mode=3` |
| `/api/dance` | POST | 表单参数 `mode=1` |
| `/api/correction` | POST | 表单参数 `value=0.02` |

`GET /api/status` 示例：

```json
{
  "ok": true,
  "ap_ssid": "SparkBot-Car-AABBCCDDEEFF",
  "ap_ip": "192.168.4.1",
  "http_port": 80,
  "clients": 1,
  "x": 0.0,
  "y": 0.0,
  "moving": false,
  "light_mode": 3,
  "dance_mode": 0,
  "motion_seq": 12,
  "last_command": "x0.00 y0.00"
}
```

## UART 底盘命令

网页命令由头部 ESP32-S3 解析后，通过 UART 发送到底盘 ESP32-C2。头部主控不直接 PWM 控电机。

| 命令 | 示例 | 含义 |
| --- | --- | --- |
| `x<float> y<float>` | `x0.00 y1.00` | 运动控制，`x` 控制转向，`y` 控制前进/后退 |
| `w<number>` | `w3` | 切换 RGB 灯效 |
| `d<number>` | `d1` | 触发舞蹈动作 |
| `c<float>` | `c0.02` | 调整左右电机速度修正量 |

常用运动示例：

| 命令 | 动作 |
| --- | --- |
| `x0.00 y1.00` | 前进 |
| `x0.00 y-1.00` | 后退 |
| `x-1.00 y0.00` | 左转 |
| `x1.00 y0.00` | 右转 |
| `x0.00 y0.00` | 停止 |

## 文件结构

| 文件 | 作用 |
| --- | --- |
| `main/sparkbot_web_car_control_main.c` | AP-only 热点、LCD 提示、HTTP API、WebSocket、安全停车逻辑 |
| `main/index.html` | 内嵌网页控制界面 |
| `main/Kconfig.projbuild` | 热点名、密码、端口、控制周期等配置 |
| `components/tracked_chassis_control/` | 头部 ESP32-S3 到底盘 ESP32-C2 的 UART 控制组件 |
| `components/lcd_face_ui/` | LCD 初始化和状态提示显示 |
| `sdkconfig.defaults` | ESP32-S3、SoftAP、WebSocket 等默认配置 |

## 编译和烧录

推荐使用本机 ESP-IDF v5.5.4 环境。

```powershell
cd E:\practiceWeek\codes\SparkBot_pro\sparkbot_web_car_control

$env:IDF_TOOLS_PATH = 'C:\Espressif\tools'
$env:IDF_PATH = 'D:\espidf\v5.5.4\esp-idf'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v5.5.4\venv'
$env:PATH = 'C:\Espressif\tools\cmake\3.30.2\bin;' +
            'C:\Espressif\tools\ninja\1.12.1;' +
            'C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;' +
            'C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;' +
            'C:\Espressif\tools\python\v5.5.4\venv\Scripts;' +
            $env:PATH

& 'C:\Espressif\tools\python\v5.5.4\venv\Scripts\python.exe' `
  'D:\espidf\v5.5.4\esp-idf\tools\idf.py' set-target esp32s3

& 'C:\Espressif\tools\python\v5.5.4\venv\Scripts\python.exe' `
  'D:\espidf\v5.5.4\esp-idf\tools\idf.py' build
```

烧录和串口监视：

```powershell
& 'C:\Espressif\tools\python\v5.5.4\venv\Scripts\python.exe' `
  'D:\espidf\v5.5.4\esp-idf\tools\idf.py' -p COM5 flash monitor
```

把 `COM5` 换成实际串口号。

## 验收清单

- 烧录后 LCD 显示 `JOIN AP / SparkBot-Car-...`。
- 手机或电脑 WiFi 列表中能看到同名热点。
- 连接热点后 LCD 显示 `192.168.4.1 / OPEN WEB`。
- 浏览器能打开 `http://192.168.4.1`。
- 网页顶部显示的热点名和 LCD 显示一致。
- 网页不再出现路由器 WiFi 配置、扫描 WiFi、保存 WiFi 密码等入口。
- 摇杆和方向键可以控制小车运动。
- 松开摇杆或点击停止后，小车停止。
- 灯光按钮可以切换底盘灯效。
- 关闭网页或断开小车热点后，小车会自动停止。

## 常见问题

### 找不到小车热点

不要只找固定的 `SparkBot-Car`。实际热点名会带 MAC 后缀，例如 `SparkBot-Car-AABBCCDDEEFF`，请以 LCD 上显示的完整名称为准。

### 打不开 `192.168.4.1`

先确认手机或电脑已经连接到 LCD 上显示的小车热点。手机提示“无互联网连接”时不要切走网络，这个热点本来就只用于控制小车。

### 网页能打开，但小车不动

优先检查头部 ESP32-S3 和底盘 ESP32-C2 是否通过 4P 磁吸接口正确连接，底盘是否上电，底盘固件是否支持 UART 命令。如果串口里能看到 `UART -> x... y...`，说明头部主控已经发出命令，问题更可能在底盘侧。

### 控制有延迟

手机尽量靠近小车，减少同一环境里同时开启的热点数量。也可以在 `menuconfig` 中把 `SPARKBOT_WEB_CAR_COMMAND_PERIOD_MS` 从 `100` 调小到 `80`。