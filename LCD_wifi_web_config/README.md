# ESP-SparkBot LCD WiFi Web Config

这个项目演示如何让 ESP32-S3 通过 Web 页面配置 WiFi。设备启动后会创建一个临时配置热点，手机或电脑连接这个热点后访问网页，就可以扫描附近 WiFi、填写 SSID 和密码、保存到 NVS，并让 ESP32-S3 连接到真正的路由器。

本项目不使用触摸输入，也不使用外接按键。重点放在 Web 页面、HTTP API、WiFi 状态机、NVS 参数保存和 LCD 状态反馈。

## 目标现象

- LCD 显示当前网络状态。
- 设备创建配置热点：`SparkBot-Config-<SoftAP MAC>`，例如 `SparkBot-Config-AABBCCDDEEFF`。
- 配置热点名称会使用硬件 SoftAP MAC 地址作为后缀，避免多台设备同时配网时连错。
- 刚启动配置热点时，LCD 会显示 `JOIN AP` 和完整热点名。
- 未配网时，LCD 会在热点名和配置地址之间轮流提示：`JOIN AP / SparkBot-Config-...`、`192.168.4.1 / OPEN WEB`。
- 手机或电脑连接该热点后，浏览器访问 `http://192.168.4.1`。
- 网页顶部提供“配网提示”，显示应连接的热点名和应打开的配置地址。
- 网页可以扫描附近 WiFi。
- 网页可以提交 SSID 和密码。
- 设备把 WiFi 配置保存到 NVS。
- 连接成功后，LCD 显示 `ONLINE` 和 STA IP。
- 连接失败后，LCD 显示 `FAILED`，网页可以看到失败原因。
- 网页支持清除已保存的 WiFi 配置。

## 学生重点学习什么

这个项目不是只做一个表单，而是让学生看到完整链路：

1. ESP32-S3 同时工作在 AP + STA 模式。
2. AP 模式提供一个临时局域网，用来访问配置网页。
3. 配置热点名由固定前缀加 SoftAP MAC 后缀组成，便于区分不同硬件。
4. HTTP Server 提供页面和 JSON API。
5. 前端页面通过 `fetch()` 调用 `/api/status`、`/api/scan`、`/api/wifi`。
6. 后端收到 SSID 和密码后，写入 NVS。
7. ESP32-S3 切到 STA 连接路由器。
8. WiFi 事件回调更新状态机。
9. LCD 只负责显示状态，不承担网络逻辑。

## 配网流程

```text
开机
  |
  v
初始化 NVS / LCD / WiFi / HTTP Server
  |
  v
启动 SoftAP: SparkBot-Config-<SoftAP MAC>
  |
  +-- LCD 立即提示: JOIN AP / SparkBot-Config-<SoftAP MAC>
  |
  v
读取 NVS 中保存的 SSID/密码
  |
  +-- 没有配置 --> CONFIG 状态，LCD 轮流提示热点名和 http://192.168.4.1，等待网页提交
  |
  +-- 有配置 ----> CONNECTING 状态，尝试连接路由器
                         |
                         +-- 成功 --> CONNECTED 状态，显示 STA IP
                         |
                         +-- 失败 --> 重试，超过次数后 FAILED
```

## LCD 提示

| 场景 | LCD 显示 | 含义 |
| --- | --- | --- |
| 配置热点刚启动 | `JOIN AP / SparkBot-Config-AABBCCDDEEFF` | 先让手机或电脑连接这个热点 |
| 未配网轮显 1 | `JOIN AP / SparkBot-Config-AABBCCDDEEFF` | 提醒用户连接正确设备，后缀来自硬件 SoftAP MAC |
| 未配网轮显 2 | `192.168.4.1 / OPEN WEB` | 连接热点后，在浏览器打开这个地址 |
| 手机或电脑接入配置热点 | `192.168.4.1 / OPEN WEB` | 已有人连上配置热点，再提示打开配置页 |
| 正在连接路由器 | `JOIN WIFI / <SSID>` 或 `RETRY / CONNECTING` | 正在连接用户填写的路由器 WiFi |
| 连接成功 | `ONLINE / <STA IP>` | 已经连上路由器，可以访问设备 STA IP |
| 连接失败 | `FAILED / CHECK PASS` | 通常是密码错误、信号差或路由器不可达 |

如果 `SPARKBOT_WEB_CONFIG_HTTP_PORT` 不是 `80`，LCD 和网页会显示带端口的地址，例如 `192.168.4.1:8080`，浏览器中访问 `http://192.168.4.1:8080`。

## Web 页面

网页首页顶部有“配网提示”：

- `先连接`：显示当前设备完整配置热点名，例如 `SparkBot-Config-AABBCCDDEEFF`。
- `再打开`：显示配置页 URL，例如 `http://192.168.4.1`。

页面还会显示设备状态、配置热点、配置地址、路由 SSID、路由地址、信号强度和最近状态。

## 状态机

| 状态 | LCD 显示 | 含义 |
| --- | --- | --- |
| `config` | `JOIN AP / SparkBot-Config-...` 和 `192.168.4.1 / OPEN WEB` 轮显 | 还没有可用 WiFi 配置，等待网页填写 |
| `connecting` | `JOIN WIFI` 或 `RETRY` | 正在连接路由器 |
| `connected` | `ONLINE / <IP>` | 已经连上路由器 |
| `failed` | `FAILED / CHECK PASS` | 连接失败，通常是密码错误、信号差或路由器不可达 |

## Web API

### `GET /`

返回配置网页。

### `GET /api/status`

返回设备当前网络状态。

示例：

```json
{
  "ok": true,
  "state": "connected",
  "configured": true,
  "ssid": "HomeWiFi",
  "sta_ip": "192.168.1.23",
  "ap_ssid": "SparkBot-Config-AABBCCDDEEFF",
  "ap_ip": "192.168.4.1",
  "http_port": 80,
  "rssi": -48,
  "retry": 0,
  "last_error": "OK"
}
```

### `GET /api/scan`

扫描附近 WiFi。

示例：

```json
{
  "ok": true,
  "aps": [
    {
      "ssid": "HomeWiFi",
      "rssi": -48,
      "auth": "WPA2",
      "channel": 6
    }
  ]
}
```

### `POST /api/wifi`

提交表单格式：

```text
ssid=HomeWiFi&password=12345678
```

成功返回：

```json
{
  "ok": true,
  "message": "connecting"
}
```

### `POST /api/forget`

清除 NVS 中保存的 WiFi 配置，并回到 `config` 状态。LCD 会重新开始提示配置热点名和配置地址。

## 文件结构

| 文件 | 作用 |
| --- | --- |
| `main/lcd_wifi_web_config_main.c` | 主程序：WiFi、HTTP API、NVS、状态机、LCD 状态提示 |
| `main/index.html` | Web 配网页面和配网提示 |
| `main/Kconfig.projbuild` | 可配置 SoftAP 名称前缀、密码、端口和重试次数 |
| `components/lcd_face_ui/` | LCD 初始化和表情绘制组件 |
| `sdkconfig.defaults` | ESP32-S3、Flash、PSRAM、SoftAP 等默认配置 |

## 硬件说明

| 模块 | 参数 |
| --- | --- |
| 主控 | ESP32-S3 |
| LCD | ST7789，240x240，RGB565 |
| LCD MOSI | GPIO47 |
| LCD CLK | GPIO21 |
| LCD CS | GPIO44 |
| LCD DC | GPIO43 |
| LCD 背光 | GPIO46 |

本项目只使用 LCD 和 ESP32-S3 内置 WiFi。

## 编译和烧录

```powershell
cd E:\practiceWeek\codes\SparkBot_pro\LCD_wifi_web_config
idf.py set-target esp32s3
idf.py build flash monitor
```

如果 ESP-IDF 环境还没有导入，可以先执行你的 ESP-IDF `export.ps1`，例如：

```powershell
. D:\espidf\v5.5.4\esp-idf\export.ps1
```

## 使用方法

1. 烧录后等待 LCD 显示 `JOIN AP / SparkBot-Config-...`。
2. 手机或电脑连接 LCD 上显示的完整 WiFi 热点，例如 `SparkBot-Config-AABBCCDDEEFF`。
3. 默认密码是 `12345678`。
4. LCD 会轮流显示或在设备接入热点后显示 `192.168.4.1 / OPEN WEB`。
5. 浏览器打开 `http://192.168.4.1`。
6. 页面顶部“配网提示”会再次显示当前热点名和配置地址。
7. 点击“扫描 WiFi”。
8. 选择或手动输入路由器 SSID。
9. 输入路由器密码。
10. 点击“保存并连接”。
11. 连接成功后，LCD 显示 `ONLINE` 和设备在路由器中的 IP。

连接成功后，也可以在同一个局域网里访问设备的 STA IP，例如：

```text
http://192.168.1.23
```

## 可配置项

运行：

```powershell
idf.py menuconfig
```

进入：

```text
SparkBot Web WiFi Config
```

可以修改：

| 配置项 | 默认值 | 说明 |
| --- | --- | --- |
| `SPARKBOT_WEB_CONFIG_AP_SSID` | `SparkBot-Config` | 配置热点名称前缀；固件运行时会追加 `-<SoftAP MAC>` 后缀 |
| `SPARKBOT_WEB_CONFIG_AP_PASSWORD` | `12345678` | 配置热点密码 |
| `SPARKBOT_WEB_CONFIG_AP_CHANNEL` | `6` | 配置热点信道 |
| `SPARKBOT_WEB_CONFIG_MAX_STA_RETRY` | `8` | STA 连接重试次数 |
| `SPARKBOT_WEB_CONFIG_HTTP_PORT` | `80` | HTTP 服务端口 |

注意：WiFi SSID 最长 32 字节。固件会优先保留 `-<SoftAP MAC>` 后缀，如果你把 `SPARKBOT_WEB_CONFIG_AP_SSID` 配得很长，前缀会被自动截断。

## 串口验收

串口中应该能看到类似日志：

```text
ESP-SparkBot LCD WiFi web config starting
Config AP started: SparkBot-Config-AABBCCDDEEFF, http://192.168.4.1
HTTP server started on port 80
No saved WiFi credentials, stay in config mode
Connecting to SSID: HomeWiFi
WiFi connected, STA IP: 192.168.1.23
```

验收时检查：

- LCD 一开始能显示 `JOIN AP` 和完整热点名 `SparkBot-Config-...`。
- LCD 在配置状态能轮流显示热点名和 `192.168.4.1 / OPEN WEB`。
- 手机或电脑能连接到 LCD 上显示的完整热点名。
- 能打开 `http://192.168.4.1`。
- 网页顶部“配网提示”显示的热点名和 LCD 一致。
- 网页能扫描 WiFi。
- 提交正确密码后可以连接路由器。
- 断电重启后会自动读取 NVS 并重新连接。
- 点击“清除配置”后会回到配置模式，并重新显示热点名和配置地址。

## 常见问题

### 打不开 `192.168.4.1`

先确认手机或电脑已经连接到 LCD 上显示的完整配置热点名，例如 `SparkBot-Config-AABBCCDDEEFF`。如果手机提示“此网络无法访问互联网”，不要切走，这正是配置热点的正常现象。

### 找不到 `SparkBot-Config`

现在热点名不再只是固定的 `SparkBot-Config`，而是带 SoftAP MAC 后缀，例如 `SparkBot-Config-AABBCCDDEEFF`。请以 LCD 显示的完整名称为准。

### 扫描不到 WiFi

确认路由器是 2.4 GHz WiFi。ESP32-S3 不能连接 5 GHz WiFi。

### 一直显示 `FAILED`

优先检查：

- SSID 是否选对。
- 密码是否正确。
- 路由器是否开启 2.4 GHz。
- 设备离路由器是否太远。
- 是否使用了企业认证 WiFi。这个项目面向普通家庭/教室 WPA/WPA2/WPA3-Personal 网络。

### 连接成功后为什么配置热点还在

这是为了教学和调试方便。设备保持 AP + STA 模式，学生可以同时观察配置网页和路由器连接状态。后续可以把它扩展成“连接成功后关闭 AP”。

## 适合继续扩展的课堂任务

- 增加 mDNS，让学生访问 `http://sparkbot.local`。
- 增加连接成功后自动关闭 SoftAP 的选项。
- 增加 Captive Portal，手机连接热点后自动弹出配置页。
- 增加二维码显示或二维码网页入口，让用户直接扫码打开 `http://192.168.4.1`。
- 增加“只保存成功连接的 WiFi”策略。
- 增加 WiFi 信号强度曲线或历史日志。
- 增加配置导出和恢复出厂设置 API。
- 增加简单鉴权，避免同一局域网中的其他人误改配置。

## 教学提醒

这个项目的核心不是 HTML 页面，而是状态流：

```text
网页输入 -> HTTP POST -> NVS 保存 -> esp_wifi_set_config()
        -> esp_wifi_connect() -> WiFi/IP 事件 -> 状态更新 -> LCD/Web 刷新
```

学生只要把这条链路读通，就基本掌握了 ESP32 Web 配网项目的骨架。