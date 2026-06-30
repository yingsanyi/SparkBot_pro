# ESP-SparkBot LCD Blink：Vibe Coding 复现提示词指南

本文档用于指导学生通过 Vibe coding 复现当前 `LCD_blink` 项目。目标不是让学生一次性抄完代码，而是学会把硬件信息、工程结构、行为目标、报错信息逐步告诉 AI，让 AI 辅助完成一个可编译、可烧录、可观察的 ESP-IDF LCD 背光闪烁实验。

> 重要：当前源码的真实行为是“初始化 ST7789 LCD，先填充白色画面，然后通过 GPIO46 周期性打开/关闭 LCD 背光”。不要把它误做成红绿蓝全屏颜色轮播。`README.md` 中有“solid colors / red green blue”的描述，但当前 `main/lcd_blink_main.c` 以背光闪烁为准。

## 1. 项目复现目标

让 ESP-SparkBot 头部硬件上的 ESP32-S3 完成以下动作：

1. 使用 ESP-IDF 创建一个名为 `lcd_blink` 的工程。
2. 目标芯片设置为 `esp32s3`。
3. 初始化 240 x 240 的 ST7789 SPI LCD。
4. LCD 初始化成功后填充整屏白色。
5. 使用 GPIO46 控制背光，每隔 `CONFIG_LCD_BLINK_PERIOD_MS` 毫秒亮灭一次。
6. 通过 `idf.py build` 能成功生成 `build/lcd_blink.bin`。
7. 通过 `idf.py -p PORT flash monitor` 能在串口看到 LCD 初始化和背光开关日志。

## 2. 必须告诉 AI 的硬件和软件细节

学生在 Vibe coding 时，必须主动给 AI 这些信息，否则 AI 很容易生成通用 LCD 示例，导致引脚、SPI 模式或屏幕效果不一致。

| 项目 | 当前项目要求 |
| --- | --- |
| 开发框架 | ESP-IDF，当前环境使用 v5.4.1 |
| 目标芯片 | ESP32-S3 |
| LCD 控制器 | ST7789 |
| 分辨率 | 240 x 240 |
| 通信接口 | SPI |
| 颜色格式 | RGB565，16 bit per pixel |
| SPI Host | `SPI2_HOST` |
| SPI Mode | `0` |
| SPI Pixel Clock | `40 * 1000 * 1000` Hz |
| MOSI | GPIO47 |
| SCLK | GPIO21 |
| CS | GPIO44 |
| DC | GPIO43 |
| RESET | 未连接，代码中使用 `-1` |
| Backlight | GPIO46，高电平亮，低电平灭 |
| 刷屏策略 | 每次传输 10 行，使用 DMA 行缓冲区 |
| 闪烁周期配置 | `CONFIG_LCD_BLINK_PERIOD_MS`，默认 1000 ms |

## 3. 推荐的 Vibe Coding 流程

不要一开始就要求 AI “写完整项目”。建议按下面顺序推进，每一步都能更容易检查和修正。

1. 先让 AI 理解项目目标、硬件和最终现象。
2. 让 AI 生成 ESP-IDF 工程结构。
3. 让 AI 生成顶层 `CMakeLists.txt`、`main/CMakeLists.txt`、`main/Kconfig.projbuild`、`sdkconfig.defaults`。
4. 让 AI 生成 `main/lcd_blink_main.c`。
5. 让 AI 根据编译报错修复依赖、字段名和 API 使用。
6. 烧录后根据串口日志和屏幕现象排查硬件参数。
7. 最后让 AI 帮助补充 README 或课堂讲解。

## 4. 从零开始的完整提示词

### 提示词 1：让 AI 建立项目认知

```text
你是一个熟悉 ESP-IDF v5.x、ESP32-S3、ST7789 SPI LCD 的嵌入式开发助手。

我想复现一个 ESP-SparkBot 头部 LCD 背光闪烁实验，不是做网页，也不是 Arduino 工程。

项目目标：
- 使用 ESP-IDF 工程，项目名 lcd_blink。
- 目标芯片是 ESP32-S3。
- 初始化 ESP-SparkBot 头部的 240x240 ST7789 SPI LCD。
- LCD 初始化后填充整屏白色。
- 然后通过 GPIO46 周期性打开和关闭 LCD 背光，实现 LCD Blink 效果。
- 背光闪烁周期通过 Kconfig 配置项 CONFIG_LCD_BLINK_PERIOD_MS 控制，默认 1000 ms。

硬件参数：
- MOSI: GPIO47
- SCLK: GPIO21
- CS: GPIO44
- DC: GPIO43
- RST: 未连接，代码中使用 -1
- Backlight: GPIO46，高电平亮，低电平灭
- LCD 控制器：ST7789
- 分辨率：240 x 240
- 颜色格式：RGB565
- SPI host: SPI2_HOST
- SPI mode: 0
- SPI pixel clock: 40 MHz

请先不要直接写代码。请你先确认你理解的工程结构、主要文件、关键初始化步骤和最终运行现象。
```

### 提示词 2：生成工程文件结构

```text
请为这个 ESP-IDF 项目生成最小工程结构。

需要包含：
- CMakeLists.txt
- main/CMakeLists.txt
- main/Kconfig.projbuild
- main/lcd_blink_main.c
- sdkconfig.defaults
- 可选 README.md

要求：
- 顶层 project 名称是 lcd_blink。
- main 组件源码文件名是 lcd_blink_main.c。
- main 组件依赖 esp_lcd、esp_driver_gpio、esp_driver_spi。
- Kconfig 中提供 CONFIG_LCD_BLINK_PERIOD_MS，类型为 int，范围 10 到 3600000，默认 1000。
- sdkconfig.defaults 中写入 CONFIG_LCD_BLINK_PERIOD_MS=1000。

请先输出每个文件应该承担的职责，再输出文件内容。
```

### 提示词 3：生成顶层 CMakeLists.txt

```text
请生成 ESP-IDF 项目的顶层 CMakeLists.txt。

要求：
- 使用 ESP-IDF 标准写法。
- cmake_minimum_required(VERSION 3.16)
- include($ENV{IDF_PATH}/tools/cmake/project.cmake)
- project(lcd_blink)
- 不要加入 Arduino、PlatformIO 或第三方构建逻辑。
```

期望文件内容：

```cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lcd_blink)
```

### 提示词 4：生成 main/CMakeLists.txt

```text
请生成 main/CMakeLists.txt。

要求：
- 注册源码 lcd_blink_main.c。
- INCLUDE_DIRS 使用 "."。
- 依赖 ESP-IDF 组件 esp_lcd、esp_driver_gpio、esp_driver_spi。
- 使用 idf_component_register。
```

期望文件内容：

```cmake
idf_component_register(SRCS "lcd_blink_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd esp_driver_gpio esp_driver_spi)
```

### 提示词 5：生成 Kconfig 配置项

```text
请生成 main/Kconfig.projbuild，用于在 menuconfig 中配置 LCD 背光闪烁周期。

要求：
- 菜单名称为 LCD Blink Configuration。
- 配置项名称为 LCD_BLINK_PERIOD_MS。
- 类型 int。
- 显示名称可以是 LCD backlight blink period in ms。
- 范围 10 到 3600000。
- 默认 1000。
- help 中说明它控制背光开关之间的延时。
```

参考文件内容：

```kconfig
menu "LCD Blink Configuration"

    config LCD_BLINK_PERIOD_MS
        int "LCD backlight blink period in ms"
        range 10 3600000
        default 1000
        help
            Delay between LCD backlight on/off changes.

endmenu
```

### 提示词 6：生成核心 C 源码

```text
请生成 ESP-IDF v5.x 的 main/lcd_blink_main.c，用于 ESP-SparkBot ESP32-S3 头部 LCD 背光闪烁实验。

功能要求：
1. 只支持 ESP32-S3。如果不是 CONFIG_IDF_TARGET_ESP32S3，编译时报错。
2. 使用 ST7789 LCD 驱动。
3. LCD 引脚：
   - MOSI GPIO47
   - SCLK GPIO21
   - CS GPIO44
   - DC GPIO43
   - RST 未连接，使用 -1
   - Backlight GPIO46
4. SPI 配置：
   - LCD_HOST 使用 SPI2_HOST
   - SPI mode 为 0
   - pixel clock 为 40 MHz
5. LCD 参数：
   - 分辨率 240x240
   - RGB565
   - 命令位宽 8
   - 参数位宽 8
6. 初始化流程：
   - 创建二值信号量，用于等待 SPI 颜色数据传输完成。
   - 初始化背光 GPIO46 为输出，并默认打开背光。
   - 初始化 SPI bus，只用 MOSI 和 SCLK，不使用 MISO。
   - 创建 esp_lcd SPI panel IO，设置 CS、DC、spi_mode、pclk_hz、trans_queue_depth、on_color_trans_done 回调、user_ctx。
   - 创建 ST7789 panel driver。
   - reset、init、invert_color(true)、disp_on_off(true)。
   - 填充整屏白色。
7. 刷屏实现：
   - 定义 DMA_ATTR uint16_t 行缓冲区。
   - 每次发送 10 行。
   - 使用 esp_lcd_panel_draw_bitmap 分块填充 240x240 屏幕。
   - 每次 draw_bitmap 后等待 on_color_trans_done 回调释放信号量。
8. 主循环：
   - 打印日志 LCD backlight on。
   - GPIO46 输出高电平。
   - vTaskDelay(pdMS_TO_TICKS(CONFIG_LCD_BLINK_PERIOD_MS))。
   - 打印日志 LCD backlight off。
   - GPIO46 输出低电平。
   - 再延时相同时间。

代码要求：
- 包含必要头文件：FreeRTOS、semphr、task、driver/gpio.h、driver/spi_master.h、esp_lcd 相关头文件、esp_log.h、sdkconfig.h。
- 使用 ESP_ERROR_CHECK 检查 ESP-IDF API 返回值。
- 不要使用 Arduino API。
- 不要使用 LVGL。
- 不要做红绿蓝颜色轮播，本实验只需要白屏内容和背光闪烁。
- 添加简洁中文注释，方便课堂讲解。
```

### 提示词 7：让 AI 自检源码

```text
请检查刚才生成的 ESP-IDF LCD 背光闪烁代码，重点检查：

1. 是否错误使用了 Arduino、TFT_eSPI、LVGL 或 PlatformIO。
2. 是否遗漏 main/CMakeLists.txt 中的 REQUIRES esp_lcd esp_driver_gpio esp_driver_spi。
3. 是否把 SPI host 写成了 SPI3_HOST 或其他 host。
4. 是否把 SPI mode 写成了 2。
5. 是否把 pixel clock 写成了 80 MHz。
6. 是否把实验误写成红绿蓝全屏轮播。
7. 是否遗漏 GPIO46 背光控制。
8. 是否没有等待 esp_lcd_panel_draw_bitmap 的异步传输完成。
9. 是否使用了整屏 240x240 大缓冲区，导致内存占用过大。
10. 是否缺少 CONFIG_IDF_TARGET_ESP32S3 检查。

请按“问题 / 原因 / 修改建议”的格式给出检查结果，并给出修正后的完整文件。
```

### 提示词 8：根据编译错误修复

把真实报错复制给 AI，不要只说“编译失败”。推荐提示词：

```text
我在 ESP-IDF v5.4.1 中运行 idf.py build，出现下面的编译错误。请只根据这些错误修复当前 ESP-IDF 工程，不要改变项目目标。

项目目标仍然是：
- ESP32-S3
- ST7789 240x240 SPI LCD
- 初始化后填充白色
- GPIO46 背光闪烁
- 不使用 Arduino、LVGL、PlatformIO

报错如下：

【把完整报错粘贴在这里】

请指出：
1. 错误发生在哪个文件和哪一行。
2. 根本原因是什么。
3. 需要修改哪些文件。
4. 给出修正后的代码片段或完整文件。
```

### 提示词 9：根据现象排查烧录后的问题

```text
程序已经成功 build 和 flash，但硬件现象不符合预期。请作为 ESP32-S3 + ST7789 LCD 调试助手，根据下面信息排查。

预期现象：
- 串口打印 LCD backlight blink started。
- 串口打印 Initialize LCD SPI bus、Install LCD panel IO、Install ST7789 LCD panel driver、Reset and initialize LCD、Fill LCD white。
- 屏幕先显示白色。
- 背光每隔 1000 ms 亮灭一次。

实际现象：
【描述实际现象，例如完全不亮、只亮不闪、闪但没有白屏、颜色异常、串口重启等】

当前硬件参数：
- MOSI GPIO47
- SCLK GPIO21
- CS GPIO44
- DC GPIO43
- RST -1
- Backlight GPIO46
- SPI2_HOST
- SPI mode 0
- 40 MHz
- ST7789
- 240x240

请按优先级列出排查步骤，并说明每一步应该修改什么或观察什么。
```

## 5. 让 AI 输出最终完整代码时的总提示词

如果课堂时间较短，可以直接使用下面这个总提示词，一次性让 AI 生成完整工程。

```text
请生成一个完整 ESP-IDF v5.x 项目，用于复现 ESP-SparkBot LCD Blink 实验。

项目名：lcd_blink
目标芯片：ESP32-S3
开发框架：ESP-IDF，不使用 Arduino、PlatformIO、LVGL。

最终现象：
- 上电后初始化 ESP-SparkBot 头部 LCD。
- LCD 显示内容填充为白色。
- 然后通过 GPIO46 周期性打开/关闭背光，让 LCD 看起来亮灭闪烁。
- 默认每 1000 ms 切换一次背光状态。

硬件：
- LCD 控制器：ST7789
- 分辨率：240x240
- 颜色格式：RGB565
- MOSI: GPIO47
- SCLK: GPIO21
- CS: GPIO44
- DC: GPIO43
- RESET: 未连接，代码中使用 -1
- Backlight: GPIO46，高电平亮，低电平灭
- SPI host: SPI2_HOST
- SPI mode: 0
- pixel clock: 40 MHz

工程文件：
1. CMakeLists.txt
2. main/CMakeLists.txt
3. main/Kconfig.projbuild
4. main/lcd_blink_main.c
5. sdkconfig.defaults
6. README.md

源码要求：
- main 组件依赖 esp_lcd、esp_driver_gpio、esp_driver_spi。
- Kconfig 配置项 CONFIG_LCD_BLINK_PERIOD_MS，int，范围 10 到 3600000，默认 1000。
- 如果目标不是 ESP32-S3，使用 #error 编译失败。
- 使用 esp_lcd_new_panel_io_spi 和 esp_lcd_new_panel_st7789。
- 使用 esp_lcd_panel_reset、esp_lcd_panel_init、esp_lcd_panel_invert_color(true)、esp_lcd_panel_disp_on_off(true)。
- 使用 DMA_ATTR uint16_t 行缓冲区，每次刷 10 行。
- 使用 on_color_trans_done 回调和 FreeRTOS 二值信号量等待 SPI 传输完成。
- app_main 中先 lcd_init，再循环控制 GPIO46 背光 on/off。
- 每个关键步骤打印 ESP_LOGI 日志。
- 添加适合教学的简洁中文注释。

请输出完整项目结构和每个文件的完整内容。
```

## 6. 构建、烧录和验证提示词

### 构建命令

```powershell
idf.py set-target esp32s3
idf.py build
```

### 烧录和监视命令

```powershell
idf.py -p COM口 flash monitor
```

例如：

```powershell
idf.py -p COM5 flash monitor
```

### 让 AI 帮你解释构建产物

```text
idf.py build 已成功，build 目录中生成了 lcd_blink.bin、lcd_blink.elf、lcd_blink.map。请解释这几个文件分别是什么，以及烧录时主要使用哪些文件。
```

### 让 AI 帮你确认串口日志

```text
下面是 idf.py monitor 的串口输出。请判断 LCD 初始化是否成功，背光闪烁循环是否已经运行，并指出还有没有异常。

【粘贴串口输出】
```

正常日志中应该能看到类似内容：

```text
I lcd_blink: LCD backlight blink started
I lcd_blink: Initialize LCD SPI bus
I lcd_blink: Install LCD panel IO
I lcd_blink: Install ST7789 LCD panel driver
I lcd_blink: Reset and initialize LCD
I lcd_blink: Fill LCD white
I lcd_blink: LCD backlight on
I lcd_blink: LCD backlight off
```

## 7. 学生常见误区和处理方式

### 情况 1：AI 写成了颜色轮播

现象：
- AI 生成 `red / green / blue / white / black` 循环。
- 代码不断调用 `lcd_fill_color()` 改变屏幕内容。

原因：
- “LCD Blink” 可能被 AI 理解成“屏幕颜色闪烁”。
- 项目 README 中也可能让 AI 偏向颜色轮播。

修正提示词：

```text
请修改代码：本实验不是红绿蓝颜色轮播。LCD 初始化后只需要填充一次白色，之后不再改变屏幕内容。Blink 指的是 GPIO46 控制背光亮灭。请保留 LCD 初始化和白屏填充，只把主循环改成 GPIO46 背光 on/off。
```

### 情况 2：AI 用了 Arduino 或 TFT_eSPI

现象：
- 代码中出现 `setup()`、`loop()`、`TFT_eSPI`、`Arduino.h`。

原因：
- AI 默认把 LCD 教程联想到 Arduino 生态。

修正提示词：

```text
请把项目改回纯 ESP-IDF v5.x 工程。不能使用 Arduino.h、setup、loop、TFT_eSPI、PlatformIO。入口函数必须是 app_main，LCD 驱动必须使用 ESP-IDF 的 esp_lcd 组件。
```

### 情况 3：缺少组件依赖导致头文件找不到

典型报错：

```text
fatal error: driver/gpio.h: No such file or directory
fatal error: esp_lcd_panel_io.h: No such file or directory
```

可能原因：
- `main/CMakeLists.txt` 没有写 `REQUIRES esp_lcd esp_driver_gpio esp_driver_spi`。

修正提示词：

```text
请检查 main/CMakeLists.txt，确保 idf_component_register 中 REQUIRES 包含 esp_lcd、esp_driver_gpio、esp_driver_spi。请给出修正后的 main/CMakeLists.txt。
```

### 情况 4：目标芯片没有设置成 ESP32-S3

典型报错：

```text
#error "This LCD test is for the ESP-SparkBot ESP32-S3 head hardware."
```

处理：

```powershell
idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

给 AI 的提示词：

```text
编译时报错提示本实验只支持 ESP32-S3。请说明为什么必须运行 idf.py set-target esp32s3，以及 set-target 后为什么建议重新 build。
```

### 情况 5：屏幕完全不亮

优先检查：

1. 是否烧录到正确开发板。
2. 是否使用 ESP-SparkBot 头部硬件。
3. LCD 排线是否插好。
4. GPIO46 是否配置为输出并拉高。
5. 串口是否已经打印到 `LCD backlight on`。

排查提示词：

```text
程序能正常运行到 LCD backlight on，但屏幕完全不亮。请优先从背光 GPIO46、电源、排线、硬件型号和 GPIO 复用角度排查，不要先大改 LCD 驱动。
```

### 情况 6：背光亮，但屏幕没有白色内容

可能原因：

- SPI 引脚、CS、DC 配错。
- SPI mode 或 clock 不匹配。
- ST7789 初始化参数不对。
- `esp_lcd_panel_disp_on_off(true)` 没有调用。
- `esp_lcd_panel_draw_bitmap()` 没有真正完成传输。

排查提示词：

```text
背光能亮灭，但 LCD 没有显示白色内容。当前参数是 MOSI GPIO47、SCLK GPIO21、CS GPIO44、DC GPIO43、RST -1、SPI2_HOST、mode 0、40MHz、ST7789、240x240。请按最小修改原则排查 SPI 通信和面板初始化，不要改背光逻辑。
```

### 情况 7：颜色异常或显示反色

可能原因：

- `esp_lcd_panel_invert_color(s_panel, true)` 与屏幕实际极性有关。
- RGB/BGR 顺序或颜色反相设置不同。

修正提示词：

```text
LCD 能显示，但白色/黑色/颜色表现异常。请解释 esp_lcd_panel_invert_color(true/false) 对 ST7789 的影响，并给出如何通过最小实验验证应该使用 true 还是 false。
```

### 情况 8：刷屏等待超时

典型日志：

```text
E lcd_blink: Timed out waiting for LCD SPI transfer
```

可能原因：

- `on_color_trans_done` 回调没有正确配置。
- `user_ctx` 没有传入信号量。
- `trans_queue_depth` 或 SPI 初始化异常。
- `esp_lcd_panel_draw_bitmap()` 没有正常发起传输。

修正提示词：

```text
程序在等待 LCD SPI transfer 时超时。请检查 esp_lcd_panel_io_spi_config_t 中 on_color_trans_done、user_ctx、trans_queue_depth 的设置，以及回调里 xSemaphoreGiveFromISR 的写法。请给出最小修复方案。
```

### 情况 9：AI 使用整屏缓冲区，占用内存偏大

现象：

```c
uint16_t frame_buffer[240 * 240];
```

说明：
- 240 x 240 x 2 字节约为 115200 字节。
- 当前项目只需要纯色填充，使用 10 行行缓冲区即可。

修正提示词：

```text
请不要使用 240x240 的整屏 framebuffer。本实验只需要纯色填充，请改成 DMA_ATTR uint16_t line_buffer[240 * 10]，每次通过 esp_lcd_panel_draw_bitmap 发送 10 行，循环刷完整屏。
```

### 情况 10：Kconfig 文案与真实行为不一致

当前项目的 `main/Kconfig.projbuild` 中显示名和 help 可能仍写着 color blink，但真实功能是 backlight blink。为了教学更清楚，建议写成：

```kconfig
config LCD_BLINK_PERIOD_MS
    int "LCD backlight blink period in ms"
```

修正提示词：

```text
请把 Kconfig 中 CONFIG_LCD_BLINK_PERIOD_MS 的显示名称和 help 从 color blink 改成 backlight blink，使它和当前代码行为一致。不要改配置项名称。
```

## 8. 检查清单

学生完成后，可以按下面清单自检。

| 检查项 | 应该满足 |
| --- | --- |
| 工程名 | `lcd_blink` |
| 目标芯片 | `esp32s3` |
| 入口函数 | `app_main()` |
| 构建系统 | ESP-IDF CMake |
| LCD 驱动 | `esp_lcd_new_panel_st7789()` |
| LCD IO | `esp_lcd_new_panel_io_spi()` |
| 背光 GPIO | GPIO46 |
| 背光逻辑 | 高电平亮，低电平灭 |
| 初始画面 | 白色 |
| 主循环行为 | 只切换背光，不循环刷颜色 |
| CMake 依赖 | `esp_lcd esp_driver_gpio esp_driver_spi` |
| 配置项 | `CONFIG_LCD_BLINK_PERIOD_MS=1000` |
| 构建产物 | `build/lcd_blink.bin` |

## 9. 课堂延伸提示词

### 修改闪烁速度

```text
请说明如何通过 idf.py menuconfig 修改 CONFIG_LCD_BLINK_PERIOD_MS，让背光每 500 ms 切换一次。也请说明如何直接在 sdkconfig.defaults 中设置默认值。
```

### 改成按钮控制背光

```text
在当前 ESP-IDF LCD 背光闪烁项目基础上，请设计一个扩展实验：使用一个 GPIO 按钮控制 LCD 背光开关。要求保留原来的 LCD 初始化和白屏填充逻辑，只改变 app_main 中的背光控制方式。请先给出设计思路，不要直接写代码。
```

### 改成显示不同纯色

```text
在当前项目基础上，请设计一个扩展实验：保持背光常亮，然后每隔 1 秒把 LCD 内容切换为红、绿、蓝、白、黑。请说明这和原始 LCD backlight blink 实验的区别，并给出需要新增的 RGB565 颜色宏。
```

## 10. 给老师的提示

Vibe coding 课堂中，建议让学生保留 AI 的每轮提示词和回答，并要求他们解释每次修改的原因。这个项目最适合训练三个能力：

1. 把硬件约束说清楚：芯片、引脚、屏幕型号、SPI 参数。
2. 把软件边界说清楚：ESP-IDF，不是 Arduino，不是 LVGL。
3. 把真实现象说清楚：白屏内容不变，闪的是背光。

只要学生能围绕这三点和 AI 对话，就能更稳定地复现当前项目，也更容易在报错和硬件异常时定位问题。
