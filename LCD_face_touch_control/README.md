# ESP-SparkBot 触摸切换 LCD 表情

这个项目基于 `LCD_face_voice_control` 精简而来，保留 240x240 SPI LCD 表情绘制组件，去掉 ESP-SR 语音识别和音频链路，改为使用头部主控板上的 3 个电容触摸按键切换表情。

## 操作方式

| 触摸键 | 通道 | 功能 |
| --- | --- | --- |
| 触摸键 1 | `TOUCH_PAD_NUM1` | 回到待机表情 |
| 触摸键 2 | `TOUCH_PAD_NUM2` | 上一个表情 |
| 触摸键 3 | `TOUCH_PAD_NUM3` | 下一个表情 |

触摸键映射沿用 `factory_demo_v1` 的逻辑：1 是确认/主键，2 是上一页，3 是下一页。

## 表情顺序

```text
IDLE -> WINK -> HAPPY -> WOW -> SLEEPY -> ANGRY
```

按触摸键 3 会向后循环，按触摸键 2 会向前循环。

## 硬件引脚

### LCD

| 功能 | GPIO |
| --- | --- |
| LCD MOSI | GPIO47 |
| LCD CLK | GPIO21 |
| LCD CS | GPIO44 |
| LCD DC | GPIO43 |
| LCD RST | `GPIO_NUM_NC` |
| LCD 背光 | GPIO46 |

### 触摸按键

| 功能 | 通道 |
| --- | --- |
| 触摸按键 1 | `TOUCH_PAD_NUM1` |
| 触摸按键 2 | `TOUCH_PAD_NUM2` |
| 触摸按键 3 | `TOUCH_PAD_NUM3` |

触摸灵敏度参考 `factory_demo_v1/components/esp_sparkbot_bsp/esp_sparkbot_bsp.c`：

| 通道 | 灵敏度 |
| --- | --- |
| `TOUCH_PAD_NUM1` | `0.035F` |
| `TOUCH_PAD_NUM2` | `0.08F` |
| `TOUCH_PAD_NUM3` | `0.08F` |

## 编译和烧录

```powershell
cd E:\practiceWeek\codes\SparkBot_pro\LCD_face_touch_control
idf.py set-target esp32s3
idf.py build flash monitor
```

如果已经配置过目标芯片：

```powershell
idf.py build
idf.py flash monitor
```

## 主要源码入口

| 文件 | 作用 |
| --- | --- |
| `main/lcd_face_touch_main.c` | 初始化 LCD 和触摸按键，处理上一个/下一个表情事件 |
| `components/lcd_face_ui/lcd_face_ui.c` | 初始化 ST7789 LCD，绘制不同表情 |
| `components/lcd_face_ui/include/lcd_face_ui.h` | 表情枚举和显示接口 |

触摸部分使用 `touch_element/touch_button.h`：先安装 touch element 和 touch button，再为 `TOUCH_PAD_NUM1/2/3` 创建按钮，订阅按下/释放/长按事件，并用回调把按下事件送到 FreeRTOS 队列。LCD 刷新在独立任务中执行，避免在触摸回调里直接做较重的屏幕绘制。
