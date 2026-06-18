# ESP-SparkBot LCD Blink 教学文档

## 0. 项目结构与代码结构说明

这是一个标准的 ESP-IDF 工程，工程根目录是 `LCD_blink`。ESP-IDF 编译时会从根目录的 `CMakeLists.txt` 开始加载项目，再进入 `main` 组件编译本实验的应用代码。

### 0.1 项目目录结构

```text
LCD_blink/
├── CMakeLists.txt              # ESP-IDF 工程入口，声明项目名 lcd_blink
├── LCD_BLINK_LESSON.md         # 本教学文档
├── README.md                   # 项目简要说明
├── pytest_lcd_blink.py         # ESP-IDF pytest 测试脚本，用于检查固件产物
├── sdkconfig                   # 当前工程配置，通常由 idf.py menuconfig 生成
├── sdkconfig.defaults          # 默认配置，设置 LCD 闪烁周期
├── sdkconfig.defaults.esp32s3  # ESP32-S3 目标芯片的默认配置
├── main/
│   ├── CMakeLists.txt          # 注册 main 组件及其依赖
│   ├── Kconfig.projbuild       # menuconfig 中的 LCD Blink 配置项
│   └── lcd_blink_main.c        # 本实验的主程序代码
├── managed_components/         # ESP-IDF 组件管理器下载的依赖组件目录
└── build/                      # 编译输出目录，由 idf.py build 生成
```

其中最重要的代码文件是 `main/lcd_blink_main.c`。学习本实验时，优先阅读这个文件即可。

### 0.2 ESP-IDF 构建结构

顶层 `CMakeLists.txt` 负责声明这是一个 ESP-IDF 工程：

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lcd_blink)
```

`main/CMakeLists.txt` 负责把 `lcd_blink_main.c` 注册成应用组件，并声明它需要使用 LCD、GPIO、SPI 驱动：

```cmake
idf_component_register(SRCS "lcd_blink_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd esp_driver_gpio esp_driver_spi)
```

因此，编译流程可以理解为：

1. `idf.py build` 读取根目录 `CMakeLists.txt`。
2. ESP-IDF 加载 `main` 组件。
3. `main/CMakeLists.txt` 编译 `lcd_blink_main.c`。
4. 最终生成 `build/lcd_blink.bin` 等固件文件。

### 0.3 主程序代码结构

`main/lcd_blink_main.c` 的代码可以按功能分为几部分：

| 代码位置 | 作用 |
| --- | --- |
| 头文件包含 | 引入 FreeRTOS、GPIO、SPI、ESP LCD 驱动等 API |
| LCD 引脚宏定义 | 定义 MOSI、CLK、CS、DC、RST、BL 等硬件连接 |
| LCD 参数宏定义 | 定义 SPI 主机、SPI 模式、像素时钟、分辨率、颜色格式等 |
| 全局对象 | 保存 LCD 面板句柄、传输完成信号量、DMA 行缓冲区 |
| `lcd_color_trans_done_cb()` | SPI 颜色数据传输完成后的回调函数 |
| `lcd_wait_flush_done()` | 等待一次 LCD 刷屏传输完成 |
| `lcd_fill_color()` | 按行分块把整块 LCD 填充为指定颜色 |
| `lcd_backlight_set()` | 设置背光 GPIO 高低电平 |
| `lcd_backlight_init()` | 初始化 LCD 背光 GPIO |
| `lcd_init()` | 初始化背光、SPI 总线、LCD IO、ST7789 驱动，并填充白屏 |
| `app_main()` | ESP-IDF 应用入口，循环控制背光亮灭 |

整体调用关系如下：

```text
app_main()
└── lcd_init()
    ├── lcd_backlight_init()
    │   └── lcd_backlight_set(true)
    ├── spi_bus_initialize()
    ├── esp_lcd_new_panel_io_spi()
    ├── esp_lcd_new_panel_st7789()
    ├── esp_lcd_panel_reset()
    ├── esp_lcd_panel_init()
    ├── esp_lcd_panel_disp_on_off()
    └── lcd_fill_color(LCD_RGB565_WHITE)
        ├── esp_lcd_panel_draw_bitmap()
        └── lcd_wait_flush_done()

while (1)
├── lcd_backlight_set(true)
├── vTaskDelay(...)
├── lcd_backlight_set(false)
└── vTaskDelay(...)
```

也就是说，本工程的核心逻辑是：先完成 LCD 初始化并显示白色画面，再在 `app_main()` 的无限循环中反复切换背光 GPIO46 的电平，从而实现 LCD 明暗闪烁。

## 1. 实验目标

本实验使用 ESP-SparkBot 头部硬件上的 LCD 屏幕，实现一个简单的 `LCD Blink` 效果：

- ESP32-S3 启动后初始化 LCD 屏幕。
- 将 LCD 屏幕内容填充为白色。
- 通过控制 LCD 背光引脚，让屏幕周期性变亮、变暗。

这个实验的重点不是复杂图形显示，而是让学生理解：

- ESP32-S3 如何通过 SPI 驱动 LCD。
- LCD 常见控制引脚分别有什么作用。
- 程序如何初始化 LCD，并控制背光闪烁。

## 2. 使用的芯片和屏幕

### 主控芯片

本项目使用的主控芯片是：

| 项目 | 说明 |
| --- | --- |
| 芯片型号 | ESP32-S3 |
| CPU | Xtensa LX7 双核 |
| 本项目目标 | `esp32s3` |
| 开发框架 | ESP-IDF v5.4.1 |

ESP32-S3 支持 GPIO、SPI、I2C、UART、Wi-Fi、Bluetooth LE 等外设。本实验主要使用 GPIO 和 SPI。

### LCD 屏幕

ESP-SparkBot 头部使用的是一块 SPI LCD 屏幕：

| 项目 | 说明 |
| --- | --- |
| LCD 控制器 | ST7789 |
| 分辨率 | 240 x 240 |
| 颜色格式 | RGB565，16 bit |
| 通信接口 | SPI |
| 背光控制 | GPIO 高低电平控制 |

## 3. LCD 引脚连接

本项目使用 ESP-SparkBot 官方源码中的 LCD 引脚定义。当前能工作的代码使用 `SPI2_HOST`、`spi_mode = 0`、`40 MHz`。

| LCD 信号 | ESP32-S3 GPIO | 作用 |
| --- | ---: | --- |
| MOSI | GPIO47 | SPI 数据输出，ESP32-S3 向 LCD 发送命令和像素数据 |
| SCLK / CLK | GPIO21 | SPI 时钟信号 |
| CS | GPIO44 | SPI 片选信号，选择 LCD 设备 |
| DC | GPIO43 | 数据/命令选择，高低电平区分当前发送的是命令还是数据 |
| RST | 未连接，代码中为 `-1` | LCD 复位脚，本硬件没有使用独立 GPIO 控制 |
| BL / Backlight | GPIO46 | LCD 背光控制，高电平亮，低电平暗 |

代码中的对应定义在 [main/lcd_blink_main.c](main/lcd_blink_main.c)：

```c
#define LCD_MOSI_GPIO             47
#define LCD_CLK_GPIO              21
#define LCD_CS_GPIO               44
#define LCD_DC_GPIO               43
#define LCD_RST_GPIO              (-1)
#define LCD_BL_GPIO               46

#define LCD_HOST                  SPI2_HOST
#define LCD_SPI_MODE              0
#define LCD_PIXEL_CLOCK_HZ        (40 * 1000 * 1000)
```

## 4. LCD Blink 的硬件逻辑

这个实验中的“Blink”不是让屏幕内容不断变化，而是让 LCD 背光明暗变化。

硬件层面可以理解为两部分：

1. LCD 显示内容

   ESP32-S3 通过 SPI 把像素数据发送给 ST7789 控制器。当前程序启动后会把整块屏幕填充为白色。

2. LCD 背光亮灭

   LCD 是否“看起来亮”，主要取决于背光。程序通过 GPIO46 输出高低电平控制背光：

   ```c
   gpio_set_level(LCD_BL_GPIO, 1); // 背光亮
   gpio_set_level(LCD_BL_GPIO, 0); // 背光暗
   ```

因此，本实验的现象是：

- 屏幕内容保持白色。
- 背光每隔一段时间打开和关闭。
- 视觉上表现为 LCD 明暗闪烁。

## 5. 代码逻辑说明

程序入口是 `app_main()`。

### 5.1 主流程

```c
void app_main(void)
{
    ESP_LOGI(TAG, "LCD backlight blink started");
    lcd_init();

    while (1) {
        ESP_LOGI(TAG, "LCD backlight on");
        lcd_backlight_set(true);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LCD_BLINK_PERIOD_MS));

        ESP_LOGI(TAG, "LCD backlight off");
        lcd_backlight_set(false);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LCD_BLINK_PERIOD_MS));
    }
}
```

主流程分为两步：

1. 调用 `lcd_init()` 初始化 LCD。
2. 在无限循环中周期性开关背光。

闪烁周期由 `CONFIG_LCD_BLINK_PERIOD_MS` 控制，当前默认是 1000 ms。

### 5.2 初始化背光 GPIO

```c
static void lcd_backlight_init(void)
{
    const gpio_config_t backlight_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_BL_GPIO,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight_config));
    lcd_backlight_set(true);
}
```

这里把 GPIO46 配置为输出模式，并默认打开背光。

### 5.3 初始化 SPI 总线

```c
const spi_bus_config_t bus_config = {
    .mosi_io_num = LCD_MOSI_GPIO,
    .miso_io_num = GPIO_NUM_NC,
    .sclk_io_num = LCD_CLK_GPIO,
    .quadwp_io_num = GPIO_NUM_NC,
    .quadhd_io_num = GPIO_NUM_NC,
    .max_transfer_sz = LCD_H_RES * LCD_FLUSH_LINES * sizeof(uint16_t),
};
ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));
```

这里配置 SPI 的 MOSI 和时钟引脚。LCD 只需要 ESP32-S3 向屏幕发送数据，所以没有使用 MISO。

`max_transfer_sz` 表示单次 SPI 传输的最大数据量。当前代码每次传输 10 行像素，避免一次传输整屏占用太多 DMA 缓冲。

### 5.4 创建 LCD SPI IO

```c
const esp_lcd_panel_io_spi_config_t io_config = {
    .cs_gpio_num = LCD_CS_GPIO,
    .dc_gpio_num = LCD_DC_GPIO,
    .spi_mode = LCD_SPI_MODE,
    .pclk_hz = LCD_PIXEL_CLOCK_HZ,
    .trans_queue_depth = 10,
    .on_color_trans_done = lcd_color_trans_done_cb,
    .user_ctx = s_flush_done_sem,
    .lcd_cmd_bits = LCD_CMD_BITS,
    .lcd_param_bits = LCD_PARAM_BITS,
};
ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));
```

这一步告诉 ESP-IDF：

- LCD 的 CS 引脚是 GPIO44。
- LCD 的 DC 引脚是 GPIO43。
- SPI 模式是 mode 0。
- SPI 时钟是 40 MHz。

### 5.5 创建 ST7789 LCD 驱动

```c
const esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = LCD_RST_GPIO,
    .color_space = ESP_LCD_COLOR_SPACE_RGB,
    .bits_per_pixel = 16,
};
ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel));
```

这里创建 ST7789 驱动对象。之后程序就可以通过 `esp_lcd_panel_draw_bitmap()` 向屏幕写入像素数据。

### 5.6 复位、初始化、打开显示

```c
ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
```

这些步骤依次完成：

- 复位 LCD 控制器。
- 初始化 LCD 控制器。
- 设置颜色反转。
- 打开 LCD 显示。

### 5.7 填充屏幕为白色

```c
lcd_fill_color(LCD_RGB565_WHITE);
```

`lcd_fill_color()` 会把 240 x 240 的屏幕填充为指定 RGB565 颜色。当前使用白色 `0xFFFF`。

函数内部每次只发送 10 行：

```c
for (int y = 0; y < LCD_V_RES; y += LCD_FLUSH_LINES) {
    const int lines = (LCD_V_RES - y) < LCD_FLUSH_LINES ? (LCD_V_RES - y) : LCD_FLUSH_LINES;
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + lines, s_line_buffer));
    lcd_wait_flush_done();
}
```

这样做的好处是：

- 节省内存。
- 每次 DMA 传输的数据量更小。
- 通过信号量等待传输完成，避免下一次写缓冲时 SPI 还没传完。

## 6. 课堂讲解重点

可以按下面顺序给学生讲：

1. ESP32-S3 是主控芯片，LCD 是外设。
2. LCD 使用 SPI 通信，MOSI 和 CLK 是最核心的 SPI 信号。
3. CS 用于选择 LCD，DC 用于区分命令和数据。
4. 背光 BL 是单独的 GPIO，不属于 SPI 通信。
5. 程序先初始化 LCD，让屏幕能显示内容。
6. 再通过 GPIO46 控制背光，实现明暗闪烁。
7. LCD 内容和 LCD 背光是两个不同概念：屏幕内容可以不变，但背光开关会让屏幕看起来亮灭。

## 7. 实验现象

烧录程序后，串口会看到类似日志：

```text
LCD backlight blink started
Initialize LCD SPI bus
Install LCD panel IO
Install ST7789 LCD panel driver
Reset and initialize LCD
Fill LCD white
LCD backlight on
LCD backlight off
```

硬件现象：

- LCD 屏幕先变成白色。
- 背光周期性变亮、变暗。
- 默认每 1 秒切换一次状态。

## 8. 可修改实验

### 修改闪烁速度

可以修改 `CONFIG_LCD_BLINK_PERIOD_MS`，例如：

```c
CONFIG_LCD_BLINK_PERIOD_MS=500
```

表示每 500 ms 切换一次背光状态。

### 修改屏幕底色

当前底色是白色：

```c
#define LCD_RGB565_WHITE 0xFFFF
```

也可以换成其他 RGB565 颜色，例如：

```c
#define LCD_RGB565_RED   0xF800
#define LCD_RGB565_GREEN 0x07E0
#define LCD_RGB565_BLUE  0x001F
```

然后调用：

```c
lcd_fill_color(LCD_RGB565_RED);
```

## 9. 小结

本实验完成了一个最小 LCD 控制程序：

- 使用 ESP32-S3 作为主控。
- 使用 SPI 初始化 ST7789 LCD。
- 使用 GPIO46 控制 LCD 背光。
- 通过 FreeRTOS 延时函数实现周期性闪烁。

这个实验适合作为学生学习 ESP32-S3 GPIO、SPI 和 LCD 外设驱动的入门案例。
