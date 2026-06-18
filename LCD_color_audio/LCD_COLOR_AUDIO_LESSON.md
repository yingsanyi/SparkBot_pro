# LCD 颜色与喇叭发声进阶项目

本项目基于两个已有项目继续进阶：

- `LCD_blink`：学习 LCD 初始化和背光控制。
- `LCD_color_frequency`：学习编程改变 LCD 屏幕颜色。

新项目增加喇叭控制，让 LCD 显示不同颜色时，喇叭同步发出不同音高。

项目路径：

```text
E:\practiceWeek\codes\get_start\LCD_color_audio
```

## 一、实验现象

程序运行后会循环执行下面几组模式：

| 模式 | LCD 颜色 | 喇叭声音 |
| ---- | -------- | -------- |
| red-do | 红色 | 262 Hz |
| green-re | 绿色 | 294 Hz |
| blue-mi | 蓝色 | 330 Hz |
| yellow-sol | 黄色 | 392 Hz |
| purple-la | 紫色 | 440 Hz |

每次切换模式时：

1. LCD 变成一种颜色。
2. 喇叭播放一段对应频率的声音。
3. 等待一小段时间后进入下一种颜色和声音。

## 二、参考的硬件连接

根据你提供的原理图，音频部分使用 ES8311 音频 Codec，ESP32-S3 通过
I2S 和 I2C 控制它。

本项目参考了：

```text
E:\practiceWeek\codes\esp_sparkbot-master\example\factory_demo_v1
```

尤其参考了：

```text
components\bsp_extra\include\bsp_board_extra.h
components\bsp_extra\src\bsp_board_extra.c
main\app\app_audio_record.c
```

音频相关引脚如下：

| 原理图信号 | ESP32-S3 GPIO | 作用 |
| ---------- | ------------- | ---- |
| I2S_MCLK | GPIO45 | I2S 主时钟 |
| I2S_SCLK | GPIO39 | I2S 位时钟 |
| I2S_LRCK | GPIO41 | I2S 左右声道时钟 |
| I2S_DOUT | GPIO42 | ESP32 输出音频数据到 ES8311 |
| I2S_DSIN | GPIO40 | ES8311 输入音频数据到 ESP32，本项目暂不使用 |
| I2C_SCL | GPIO5 | 配置 ES8311 的 I2C 时钟 |
| I2C_SDA | GPIO4 | 配置 ES8311 的 I2C 数据 |

原理图中还出现了 `PA_CTRL`，示例 BSP 中功放控制脚写为
`GPIO_NUM_NC`。本项目保持和示例一致，不单独拉高额外功放 GPIO。

## 三、项目结构

```text
LCD_color_audio
├── CMakeLists.txt
├── idf_component.yml
├── sdkconfig.defaults
├── main
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   └── lcd_color_audio_main.c
└── components
    └── sparkbot_audio
        ├── CMakeLists.txt
        ├── include
        │   └── sparkbot_audio.h
        └── sparkbot_audio.c
```

## 四、LCD 部分的代码

LCD 初始化基本沿用 `LCD_blink` 和 `LCD_color_frequency`：

```c
#define LCD_MOSI_GPIO             47
#define LCD_CLK_GPIO              21
#define LCD_CS_GPIO               44
#define LCD_DC_GPIO               43
#define LCD_RST_GPIO              (-1)
#define LCD_BL_GPIO               46
```

屏幕颜色仍然使用 RGB565：

```c
#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
```

每种模式中同时保存颜色和声音频率：

```c
typedef struct {
    const char *name;
    uint16_t color;
    uint16_t tone_hz;
} lcd_audio_mode_t;

static const lcd_audio_mode_t s_modes[] = {
    { "red-do", RGB565(255, 0, 0), 262 },
    { "green-re", RGB565(0, 255, 0), 294 },
    { "blue-mi", RGB565(0, 64, 255), 330 },
    { "yellow-sol", RGB565(255, 210, 0), 392 },
    { "purple-la", RGB565(160, 32, 240), 440 },
};
```

## 五、喇叭初始化代码

本项目新增了一个本地组件：

```text
components\sparkbot_audio
```

它的作用是把示例项目中的喇叭初始化方案简化出来。

音频引脚定义：

```c
#define SPARKBOT_I2S_MCLK       (GPIO_NUM_45)
#define SPARKBOT_I2S_SCLK       (GPIO_NUM_39)
#define SPARKBOT_I2S_DOUT       (GPIO_NUM_42)
#define SPARKBOT_I2S_LRCK       (GPIO_NUM_41)
#define SPARKBOT_I2S_DSIN       (GPIO_NUM_40)
```

I2S 配置：

```c
#define SPARKBOT_I2S_STD_MONO_CFG(sample_rate)                                                       \
    {                                                                                                 \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),                                           \
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), \
        .gpio_cfg = SPARKBOT_I2S_GPIO_CFG,                                                            \
    }
```

创建 ES8311 喇叭设备：

```c
esp_codec_dev_handle_t sparkbot_audio_codec_speaker_init(void)
{
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(sparkbot_audio_init(NULL));

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);

    const es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_TYPE_OUT,
        .pa_pin = SPARKBOT_POWER_AMP_IO,
        .master_mode = false,
        .use_mclk = true,
    };

    const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
    const esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_dev,
        .data_if = s_i2s_data_if,
    };

    return esp_codec_dev_new(&codec_dev_cfg);
}
```

这里可以简单理解为：

- I2C 用来配置 ES8311。
- I2S 用来持续发送声音数据。
- ES8311 把数字音频变成模拟音频。
- 后级功放把模拟音频推到喇叭。

## 六、声音是怎么生成的

本项目没有读取音乐文件，而是在程序里生成正弦波。

喇叭播放的是 PCM 数据，也就是一串 16 bit 采样点。采样率设置为
16000 Hz，意思是每秒发送 16000 个采样点。

初始化喇叭：

```c
static void audio_init(void)
{
    s_speaker = sparkbot_audio_codec_speaker_init();
    esp_codec_dev_set_out_vol(s_speaker, CONFIG_LCD_AUDIO_VOLUME);

    const esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 1,
        .bits_per_sample = 16,
    };
    esp_codec_dev_open(s_speaker, &fs);
}
```

播放一个音：

```c
static void audio_play_tone(uint16_t frequency_hz, uint32_t duration_ms)
{
    const uint32_t total_samples = (16000 * duration_ms) / 1000;
    const float phase_step = 2.0f * AUDIO_PI * frequency_hz / 16000;

    for (...) {
        samples[i] = (int16_t)(sinf(phase) * AUDIO_AMPLITUDE);
        phase += phase_step;
        esp_codec_dev_write(s_speaker, samples, bytes);
    }
}
```

`frequency_hz` 越大，声音越高；`frequency_hz` 越小，声音越低。

## 七、主循环

主循环把 LCD 和喇叭组合起来：

```c
static void run_mode(const lcd_audio_mode_t *mode)
{
    lcd_backlight_set(true);
    lcd_fill_color(mode->color);
    audio_play_tone(mode->tone_hz, CONFIG_LCD_AUDIO_TONE_MS);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_LCD_AUDIO_MODE_HOLD_MS));
}
```

所以每个模式做三件事：

1. 打开 LCD 背光。
2. 刷新 LCD 颜色。
3. 通过喇叭播放对应频率的声音。

## 八、编译和烧录

进入项目目录：

```powershell
cd E:\practiceWeek\codes\get_start\LCD_color_audio
```

设置目标芯片：

```powershell
idf.py set-target esp32s3
```

编译：

```powershell
idf.py build
```

烧录并查看串口：

```powershell
idf.py -p COMx flash monitor
```

把 `COMx` 换成实际串口号。

本项目还提供了几个可在 `idf.py menuconfig` 中修改的配置：

```text
LCD Color Audio Configuration
  Mode hold time in ms
  Tone duration in ms
  Speaker volume
  Audio I2C port
  Audio I2S port
```

## 九、可以继续改什么

可以让学生尝试：

- 修改 `s_modes[]` 中的颜色。
- 修改 `tone_hz`，听不同音高。
- 增加更多音符。
- 把单音改成短旋律。
- 结合 `LCD_color_frequency`，让颜色、背光闪烁和声音同时变化。
