# LCD 屏幕灯光颜色和频率进阶项目

本项目基于 `LCD_blink` 改进，目标是在 ESP-SparkBot 的 240x240 ST7789
LCD 上实现两个可编程效果：

- 改变屏幕显示颜色：通过 SPI 向 ST7789 写入 RGB565 像素数据。
- 改变灯光闪烁频率：通过 GPIO46 周期性开关 LCD 背光。

项目目录：

```text
E:\practiceWeek\codes\get_start\LCD_color_frequency
```

## 一、从基础项目到进阶项目的变化

原 `LCD_blink` 的核心动作是：

1. 初始化 LCD SPI 总线和 ST7789 面板。
2. 把 LCD 填成固定颜色。
3. 使用 `gpio_set_level()` 周期性打开、关闭 GPIO46 背光。

进阶项目保留了第 1 步，扩展了第 2、3 步：

1. 新增 RGB565 颜色转换宏，可以用常见的 8 位 RGB 值定义颜色。
2. 新增 `lcd_light_mode_t` 模式表，每个模式同时保存颜色和频率。
3. 新增 `frequency_to_half_period_ms()`，把 Hz 频率转换为背光开关延时。
4. 新增 `lcd_run_light_mode()`，先填充屏幕颜色，再按指定频率闪烁背光。

## 二、关键进阶代码

### 1. RGB888 转 RGB565

ST7789 在本项目中使用 16 bit RGB565 格式。普通 RGB 颜色每个通道是
8 bit，而 RGB565 分别使用红色 5 bit、绿色 6 bit、蓝色 5 bit。

```c
#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
```

转换关系：

- 红色：保留高 5 位，放到 bit15-bit11。
- 绿色：保留高 6 位，放到 bit10-bit5。
- 蓝色：保留高 5 位，放到 bit4-bit0。

这样就可以写 `RGB565(255, 0, 0)` 表示红色，而不用手动计算
`0xF800`。

### 2. 颜色和频率模式表

进阶项目用结构体把颜色和闪烁频率绑定在一起：

```c
typedef struct {
    const char *name;
    uint16_t color;
    uint16_t frequency_hz;
} lcd_light_mode_t;

static const lcd_light_mode_t s_light_modes[] = {
    { "red-1hz", RGB565(255, 0, 0), 1 },
    { "green-2hz", RGB565(0, 255, 0), 2 },
    { "blue-4hz", RGB565(0, 64, 255), 4 },
    { "yellow-6hz", RGB565(255, 210, 0), 6 },
    { "purple-8hz", RGB565(160, 32, 240), 8 },
};
```

如果要增加新的颜色和频率，只需要在表中增加一行。例如：

```c
{ "cyan-3hz", RGB565(0, 255, 255), 3 },
```

### 3. 频率换算为半周期

背光闪烁的一次完整周期包括“亮”和“灭”两个阶段，所以半周期公式是：

```text
半周期 ms = 1000 / (2 * 频率 Hz)
```

代码实现：

```c
static uint32_t frequency_to_half_period_ms(uint16_t frequency_hz)
{
    if (frequency_hz == 0) {
        return CONFIG_LCD_COLOR_HOLD_MS;
    }

    uint32_t half_period_ms = 1000UL / (2UL * frequency_hz);
    if (half_period_ms < CONFIG_LCD_MIN_HALF_PERIOD_MS) {
        half_period_ms = CONFIG_LCD_MIN_HALF_PERIOD_MS;
    }
    return half_period_ms;
}
```

例如：

| 频率 | 完整周期 | 半周期 | 背光状态 |
| ---- | -------- | ------ | -------- |
| 1 Hz | 1000 ms | 500 ms | 亮 500 ms，灭 500 ms |
| 2 Hz | 500 ms | 250 ms | 亮 250 ms，灭 250 ms |
| 4 Hz | 250 ms | 125 ms | 亮 125 ms，灭 125 ms |

`CONFIG_LCD_MIN_HALF_PERIOD_MS` 用来限制最小半周期，避免任务循环过快。

### 4. 执行一个灯光模式

`lcd_run_light_mode()` 先把 LCD 填成当前模式的颜色，再按该模式的频率
切换背光：

```c
static void lcd_run_light_mode(const lcd_light_mode_t *mode)
{
    const uint32_t half_period_ms = frequency_to_half_period_ms(mode->frequency_hz);
    uint32_t elapsed_ms = 0;
    bool backlight_on = true;

    ESP_LOGI(TAG, "mode=%s color=0x%04x frequency=%uHz half_period=%lums",
             mode->name, mode->color, mode->frequency_hz, half_period_ms);

    lcd_fill_color(mode->color);

    while (elapsed_ms < CONFIG_LCD_COLOR_HOLD_MS) {
        lcd_backlight_set(backlight_on);
        backlight_on = !backlight_on;
        vTaskDelay(pdMS_TO_TICKS(half_period_ms));
        elapsed_ms += half_period_ms;
    }

    lcd_backlight_set(true);
}
```

这里的颜色变化和频率变化是两个不同层面的控制：

- `lcd_fill_color(mode->color)` 控制 LCD 显示内容。
- `lcd_backlight_set(backlight_on)` 控制背光开关。

所以看到的效果是：屏幕先变成指定颜色，然后这个颜色按指定频率闪烁。

### 5. 主循环

主循环不断遍历模式表：

```c
void app_main(void)
{
    ESP_LOGI(TAG, "LCD color and frequency control started");
    lcd_init();

    while (1) {
        for (int i = 0; i < (int)(sizeof(s_light_modes) / sizeof(s_light_modes[0])); i++) {
            lcd_run_light_mode(&s_light_modes[i]);
        }
    }
}
```

每个模式默认保持 `4000 ms`。可以在 `idf.py menuconfig` 中修改：

```text
LCD Color Frequency Configuration
  Color hold time in ms
  Minimum backlight half-period in ms
```

## 三、LCD 填色原理

LCD 的显示内容通过 `esp_lcd_panel_draw_bitmap()` 写入。因为整屏是
240x240，如果一次性申请完整帧缓冲区，需要：

```text
240 * 240 * 2 byte = 115200 byte
```

为了节省内存，项目只创建 10 行的 DMA 缓冲区：

```c
static DMA_ATTR uint16_t s_line_buffer[LCD_H_RES * LCD_FLUSH_LINES];
```

每次先把这 10 行填成同一种颜色，再分 24 次刷完整个屏幕：

```c
for (int y = 0; y < LCD_V_RES; y += LCD_FLUSH_LINES) {
    const int lines = (LCD_V_RES - y) < LCD_FLUSH_LINES ? (LCD_V_RES - y) : LCD_FLUSH_LINES;
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + lines, s_line_buffer));
    lcd_wait_flush_done();
}
```

`lcd_wait_flush_done()` 等待 SPI 传输完成，避免下一次绘制覆盖还没有传完
的 DMA 缓冲区。

## 四、背光频率控制原理

背光不是通过 LCD 像素数据控制的，而是由 GPIO46 控制：

```c
static void lcd_backlight_set(bool on)
{
    ESP_ERROR_CHECK(gpio_set_level(LCD_BL_GPIO, on ? 1 : 0));
}
```

当 GPIO46 输出高电平时背光点亮，输出低电平时背光关闭。程序用
`vTaskDelay()` 控制每次亮、灭之间的时间间隔，因此可以得到不同的
闪烁频率。

这种方式的特点：

- 实现简单，适合理解 GPIO 输出和 FreeRTOS 延时。
- 不需要额外 PWM 配置。
- 频率精度受 FreeRTOS tick 和任务调度影响，适合可见闪烁演示。

如果后续要做更平滑的亮度变化，可以把 GPIO 开关升级为 LEDC PWM，
用占空比控制亮度，用定时器控制颜色切换。

## 五、编译和烧录

进入项目目录：

```powershell
cd E:\practiceWeek\codes\get_start\LCD_color_frequency
```

设置目标芯片：

```powershell
idf.py set-target esp32s3
```

编译、烧录并打开串口监视器：

```powershell
idf.py -p COMx flash monitor
```

把 `COMx` 替换成实际串口号。

## 六、预期现象

LCD 会循环显示下面几种模式：

| 模式 | 屏幕颜色 | 背光频率 |
| ---- | -------- | -------- |
| red-1hz | 红色 | 1 Hz |
| green-2hz | 绿色 | 2 Hz |
| blue-4hz | 蓝色 | 4 Hz |
| yellow-6hz | 黄色 | 6 Hz |
| purple-8hz | 紫色 | 8 Hz |

串口日志会输出当前模式、RGB565 颜色值、频率和半周期。
