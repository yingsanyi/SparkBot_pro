# lcd_blink

ESP-IDF v5.x 项目，用于复现 ESP-SparkBot 头部 LCD 背光闪烁实验。

## 硬件配置

| 参数 | 值 |
|------|-----|
| 芯片 | ESP32-S3 |
| LCD 控制器 | ST7789 |
| 分辨率 | 240 × 240 |
| 颜色格式 | RGB565 |
| SPI 主机 | SPI2_HOST |
| SPI 模式 | 0 |
| 像素时钟 | 40 MHz |
| MOSI | GPIO47 |
| SCLK | GPIO21 |
| CS | GPIO44 |
| DC | GPIO43 |
| RESET | 未连接（-1） |
| 背光 | GPIO46（高电平亮 / 低电平灭） |

## 构建与烧录

```bash
# 1. 设置目标芯片
idf.py set-target esp32s3

# 2. 编译
idf.py build

# 3. 烧录并查看日志
idf.py -p /dev/ttyUSB0 flash monitor
```

## Kconfig 配置

`CONFIG_LCD_BLINK_PERIOD_MS` — 背光闪烁周期（毫秒）

- 类型：int
- 范围：10 ~ 3600000
- 默认值：1000（即亮 500ms / 灭 500ms）

可通过 `idf.py menuconfig` → "LCD Blink Configuration" 修改。

## 运行现象

1. 上电后初始化 SPI 总线和 ST7789 LCD
2. LCD 整屏填充白色
3. 背光 GPIO46 周期性输出高/低电平，LED 交替亮灭
4. 串口输出各阶段初始化日志和背光状态日志
