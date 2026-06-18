/*
 * ESP-SparkBot voice-controlled tracked chassis demo.
 *
 * Wake with "Hi Lexin", then speak a local Chinese command. The ESP32-S3 head
 * sends plain UART command strings to the chassis controller.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_afe_sr_models.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_process_sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "model_path.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "sparkbot_audio.h"
#include "sparkbot_lcd.h"
#include "tracked_chassis_control.h"

#define AFE_FEED_CHANNELS 2
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifndef CONFIG_SPARKBOT_VOICE_CHASSIS_MIC_GAIN
#define CONFIG_SPARKBOT_VOICE_CHASSIS_MIC_GAIN 30
#endif

#ifndef CONFIG_SPARKBOT_VOICE_CHASSIS_WAKE_MODE_90
#define CONFIG_SPARKBOT_VOICE_CHASSIS_WAKE_MODE_90 1
#endif

#ifndef CONFIG_SPARKBOT_VOICE_CHASSIS_WAKE_PROMPT_DELAY_MS
#define CONFIG_SPARKBOT_VOICE_CHASSIS_WAKE_PROMPT_DELAY_MS 120
#endif

#ifndef CONFIG_SPARKBOT_VOICE_CHASSIS_WAKE_PROMPT_ENABLE
#define CONFIG_SPARKBOT_VOICE_CHASSIS_WAKE_PROMPT_ENABLE 0
#endif

#ifndef CONFIG_SPARKBOT_VOICE_CHASSIS_MN_THRESHOLD_PERCENT
#define CONFIG_SPARKBOT_VOICE_CHASSIS_MN_THRESHOLD_PERCENT 0
#endif

#ifndef CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_WAIT_MS
#define CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_WAIT_MS 8000
#endif

#ifndef CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_TIMEOUT_MS
#define CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_TIMEOUT_MS 5760
#endif

#ifndef CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_SETTLE_MS
#define CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_SETTLE_MS 400
#endif

#ifndef CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_VAD_GATE
#define CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_VAD_GATE 0
#endif

#ifndef CONFIG_SPARKBOT_VOICE_CHASSIS_LCD_ENABLE
#define CONFIG_SPARKBOT_VOICE_CHASSIS_LCD_ENABLE 0
#endif

typedef struct {
    wakenet_state_t wakenet_state;
    esp_mn_state_t mn_state;
    int command_id;
} sr_event_t;

typedef enum {
    CMD_FORWARD = 0,
    CMD_BACKWARD,
    CMD_LEFT,
    CMD_RIGHT,
    CMD_FLASH_LIGHT,
    CMD_BREATH_LIGHT,
    CMD_SOLID_LIGHT,
    CMD_LIGHT_SHOW,
    CMD_DANCE,
    CMD_STOP,
    CMD_COUNT,
} voice_command_t;

static const char *TAG = "voice_chassis";

static const char *const s_command_names[CMD_COUNT] = {
    [CMD_FORWARD] = "forward",
    [CMD_BACKWARD] = "backward",
    [CMD_LEFT] = "turn left",
    [CMD_RIGHT] = "turn right",
    [CMD_FLASH_LIGHT] = "flash light",
    [CMD_BREATH_LIGHT] = "breath light",
    [CMD_SOLID_LIGHT] = "solid light",
    [CMD_LIGHT_SHOW] = "light show",
    [CMD_DANCE] = "dance",
    [CMD_STOP] = "stop",
};

typedef struct {
    voice_command_t command_id;
    const char *phrase;
} voice_command_phrase_t;

static const voice_command_phrase_t s_command_phrases[] = {
    {CMD_FORWARD, "xiang qian chong"},
    {CMD_BACKWARD, "xiang hou tui"},
    {CMD_LEFT, "xiang zuo zhuan"},
    {CMD_RIGHT, "xiang you zhuan"},
    {CMD_FLASH_LIGHT, "shan guang mo shi"},
    {CMD_BREATH_LIGHT, "hu xi mo shi"},
    {CMD_SOLID_LIGHT, "chang liang mo shi"},
    {CMD_LIGHT_SHOW, "deng guang xiu"},
    {CMD_DANCE, "tiao ge wu"},
    {CMD_STOP, "ting zhi yun dong"},
};

static const esp_afe_sr_iface_t *s_afe = NULL;
static const esp_mn_iface_t *s_multinet = NULL;
static model_iface_data_t *s_multinet_data = NULL;
static QueueHandle_t s_sr_event_queue = NULL;
static volatile bool s_command_prompt_active = false;
static volatile bool s_command_listening = false;

static int normalize_command_id(const esp_mn_results_t *mn_result)
{
    if (!mn_result || mn_result->num <= 0) {
        return -1;
    }

    const int phrase_id = mn_result->phrase_id[0];
    if (phrase_id >= 0 && phrase_id < (int)ARRAY_SIZE(s_command_phrases)) {
        return s_command_phrases[phrase_id].command_id;
    }

    const int command_id = mn_result->command_id[0];
    if (command_id >= 0 && command_id < CMD_COUNT) {
        return command_id;
    }
    if (command_id > 0 && command_id <= CMD_COUNT) {
        return command_id - 1;
    }

    return -1;
}

static esp_err_t voice_lcd_init(void)
{
#if CONFIG_SPARKBOT_VOICE_CHASSIS_LCD_ENABLE
    return sparkbot_lcd_init();
#else
    ESP_LOGI(TAG, "LCD UI disabled for speech-recognition isolation");
    return ESP_OK;
#endif
}

static void voice_lcd_post_state(sparkbot_lcd_state_t state, const char *line1, const char *line2)
{
#if CONFIG_SPARKBOT_VOICE_CHASSIS_LCD_ENABLE
    sparkbot_lcd_post_state(state, line1, line2);
#else
    (void)state;
    (void)line1;
    (void)line2;
#endif
}

static void voice_ack_light_flash(void)
{
    tracked_chassis_rgb_light_control(3);
    vTaskDelay(pdMS_TO_TICKS(110));
}

static void send_timed_motion(const char *command, TickType_t duration_ticks)
{
    tracked_chassis_motion_control(command);
    vTaskDelay(duration_ticks);
    tracked_chassis_stop();
    tracked_chassis_rgb_light_control(2);
    voice_lcd_post_state(SPARKBOT_LCD_STATE_LISTENING, "LISTEN", "NEXT");
}

static void handle_voice_command(int command_id)
{
    if (command_id < 0 || command_id >= CMD_COUNT) {
        ESP_LOGW(TAG, "Unknown command id: %d", command_id);
        voice_lcd_post_state(SPARKBOT_LCD_STATE_ERROR, "UNKNOWN", "COMMAND");
        return;
    }

    ESP_LOGI(TAG, "Voice command: %s", s_command_names[command_id]);
    voice_lcd_post_state(SPARKBOT_LCD_STATE_RUNNING, "OK", s_command_names[command_id]);

    switch ((voice_command_t)command_id) {
    case CMD_FORWARD:
        send_timed_motion("x0.0 y1.0", pdMS_TO_TICKS(CONFIG_SPARKBOT_VOICE_CHASSIS_MOVE_MS));
        break;
    case CMD_BACKWARD:
        send_timed_motion("x0.0 y-1.0", pdMS_TO_TICKS(CONFIG_SPARKBOT_VOICE_CHASSIS_MOVE_MS));
        break;
    case CMD_LEFT:
        send_timed_motion("x-1.0 y0.0", pdMS_TO_TICKS(CONFIG_SPARKBOT_VOICE_CHASSIS_TURN_MS));
        break;
    case CMD_RIGHT:
        send_timed_motion("x1.0 y0.0", pdMS_TO_TICKS(CONFIG_SPARKBOT_VOICE_CHASSIS_TURN_MS));
        break;
    case CMD_STOP:
        tracked_chassis_stop();
        tracked_chassis_rgb_light_control(2);
        break;
    case CMD_FLASH_LIGHT:
        tracked_chassis_rgb_light_control(3);
        break;
    case CMD_BREATH_LIGHT:
        tracked_chassis_rgb_light_control(5);
        break;
    case CMD_SOLID_LIGHT:
        tracked_chassis_rgb_light_control(2);
        break;
    case CMD_LIGHT_SHOW:
        tracked_chassis_rgb_light_control(7);
        break;
    case CMD_DANCE:
        tracked_chassis_set_dance_mode(1);
        break;
    default:
        break;
    }
}

static void audio_feed_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    const int chunksize = s_afe->get_feed_chunksize(afe_data);
    ESP_LOGI(TAG, "AFE feed chunksize=%d", chunksize);

    int16_t *audio = heap_caps_malloc(chunksize * AFE_FEED_CHANNELS * sizeof(int16_t),
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(audio ? ESP_OK : ESP_ERR_NO_MEM);

    esp_codec_dev_handle_t microphone = sparkbot_audio_codec_microphone_init();
    ESP_ERROR_CHECK(microphone ? ESP_OK : ESP_ERR_NO_MEM);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 1,
        .bits_per_sample = 16,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(microphone, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(microphone, CONFIG_SPARKBOT_VOICE_CHASSIS_MIC_GAIN));

    int read_fail_count = 0;
    while (true) {
        const esp_err_t read_ret = esp_codec_dev_read(microphone, audio, chunksize * sizeof(int16_t));
        if (read_ret != ESP_OK) {
            read_fail_count++;
            if (read_fail_count == 1 || (read_fail_count % 10) == 0) {
                ESP_LOGW(TAG, "Microphone read failed: %s", esp_err_to_name(read_ret));
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        read_fail_count = 0;

        for (int i = chunksize - 1; i >= 0; i--) {
            audio[i * 2] = audio[i];
            audio[i * 2 + 1] = 0;
        }

        s_afe->feed(afe_data, audio);
    }
}

static void audio_detect_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    bool detect_flag = false;

    const int afe_chunksize = s_afe->get_fetch_chunksize(afe_data);
    const int mn_chunksize = s_multinet->get_samp_chunksize(s_multinet_data);
    ESP_ERROR_CHECK(afe_chunksize == mn_chunksize ? ESP_OK : ESP_ERR_INVALID_STATE);

    ESP_LOGI(TAG, "Speech detection started. Say wake word: Hi Lexin");

    while (true) {
        afe_fetch_result_t *res = s_afe->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            ESP_LOGE(TAG, "AFE fetch failed");
            continue;
        }

        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "Wake word detected");
            const sr_event_t event = {
                .wakenet_state = WAKENET_DETECTED,
                .mn_state = ESP_MN_STATE_DETECTING,
                .command_id = -1,
            };
            xQueueSend(s_sr_event_queue, &event, 0);
        } else if (res->wakeup_state == WAKENET_CHANNEL_VERIFIED) {
            ESP_LOGI(TAG, "Wake channel verified; feeding MultiNet");
            s_afe->disable_wakenet(afe_data);
            detect_flag = true;
            s_command_listening = true;
            const sr_event_t event = {
                .wakenet_state = WAKENET_CHANNEL_VERIFIED,
                .mn_state = ESP_MN_STATE_DETECTING,
                .command_id = -1,
            };
            xQueueSend(s_sr_event_queue, &event, 0);
        }

        if (!detect_flag || s_command_prompt_active) {
            continue;
        }

        const esp_mn_state_t mn_state = s_multinet->detect(s_multinet_data, res->data);
        if (mn_state == ESP_MN_STATE_DETECTING) {
            continue;
        }

        if (mn_state == ESP_MN_STATE_TIMEOUT) {
            ESP_LOGW(TAG, "Command detection timeout");
            const sr_event_t event = {
                .wakenet_state = WAKENET_NO_DETECT,
                .mn_state = ESP_MN_STATE_TIMEOUT,
                .command_id = -1,
            };
            xQueueSend(s_sr_event_queue, &event, 0);
            s_afe->enable_wakenet(afe_data);
            detect_flag = false;
            s_command_listening = false;
            continue;
        }

        if (mn_state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *mn_result = s_multinet->get_results(s_multinet_data);
            for (int i = 0; i < mn_result->num; i++) {
                ESP_LOGI(TAG, "TOP %d: command_id=%d phrase_id=%d string=%s prob=%f",
                         i + 1, mn_result->command_id[i], mn_result->phrase_id[i],
                         mn_result->string, mn_result->prob[i]);
            }

            const sr_event_t event = {
                .wakenet_state = WAKENET_NO_DETECT,
                .mn_state = ESP_MN_STATE_DETECTED,
                .command_id = normalize_command_id(mn_result),
            };
            xQueueSend(s_sr_event_queue, &event, 0);
            continue;
        }

        ESP_LOGW(TAG, "Unhandled MultiNet state: %d", mn_state);
    }
}

static void sr_event_handler_task(void *arg)
{
    (void)arg;

    tracked_chassis_control_start();
    tracked_chassis_rgb_light_control(2);

    while (true) {
        sr_event_t event = {0};
        xQueueReceive(s_sr_event_queue, &event, portMAX_DELAY);

        if (event.wakenet_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "Ready for voice command");
            tracked_chassis_rgb_light_control(3);
            voice_lcd_post_state(SPARKBOT_LCD_STATE_WAKE, "ASK", "COMMAND");
#if CONFIG_SPARKBOT_VOICE_CHASSIS_WAKE_PROMPT_ENABLE
            s_command_prompt_active = true;
            ESP_ERROR_CHECK_WITHOUT_ABORT(sparkbot_audio_play_beep(1200, 90, 65));
            vTaskDelay(pdMS_TO_TICKS(40));
            ESP_ERROR_CHECK_WITHOUT_ABORT(sparkbot_audio_play_beep(1600, 90, 65));
            vTaskDelay(pdMS_TO_TICKS(CONFIG_SPARKBOT_VOICE_CHASSIS_WAKE_PROMPT_DELAY_MS));
            s_command_prompt_active = false;
#else
            s_command_prompt_active = false;
#endif
            if (s_command_listening) {
                voice_lcd_post_state(SPARKBOT_LCD_STATE_LISTENING, "LISTEN", "COMMAND");
            }
            continue;
        }

        if (event.wakenet_state == WAKENET_CHANNEL_VERIFIED) {
            ESP_LOGI(TAG, "Command channel verified");
            tracked_chassis_rgb_light_control(5);
            if (!s_command_prompt_active) {
                voice_lcd_post_state(SPARKBOT_LCD_STATE_LISTENING, "LISTEN", "COMMAND");
            }
            continue;
        }

        if (event.mn_state == ESP_MN_STATE_TIMEOUT) {
            ESP_LOGI(TAG, "Back to wake-word mode");
            tracked_chassis_stop();
            voice_lcd_post_state(SPARKBOT_LCD_STATE_TIMEOUT, "TIMEOUT", "HI LEXIN");
            tracked_chassis_rgb_light_control(2);
            vTaskDelay(pdMS_TO_TICKS(800));
            voice_lcd_post_state(SPARKBOT_LCD_STATE_IDLE, "HI LEXIN", "WAITING");
            s_command_listening = false;
            continue;
        }

        if (event.mn_state == ESP_MN_STATE_DETECTED) {
            voice_ack_light_flash();
            handle_voice_command(event.command_id);
        }
    }
}

static esp_err_t app_sr_start(void)
{
    s_sr_event_queue = xQueueCreate(4, sizeof(sr_event_t));
    ESP_RETURN_ON_FALSE(s_sr_event_queue, ESP_ERR_NO_MEM, TAG, "create SR event queue failed");

    srmodel_list_t *models = esp_srmodel_init("model");
    ESP_RETURN_ON_FALSE(models, ESP_FAIL, TAG, "init speech model partition failed");

    s_afe = &ESP_AFE_SR_HANDLE;
    afe_config_t afe_config = AFE_CONFIG_DEFAULT();
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.total_ch_num = AFE_FEED_CHANNELS;
    afe_config.wakenet_model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
    afe_config.wakenet_mode = CONFIG_SPARKBOT_VOICE_CHASSIS_WAKE_MODE_90 ? DET_MODE_2CH_90 : DET_MODE_2CH_95;
    ESP_RETURN_ON_FALSE(afe_config.wakenet_model_name, ESP_FAIL, TAG, "no WakeNet model found");

    esp_afe_sr_data_t *afe_data = s_afe->create_from_config(&afe_config);
    ESP_RETURN_ON_FALSE(afe_data, ESP_FAIL, TAG, "create AFE failed");
    ESP_LOGI(TAG, "WakeNet model: %s", afe_config.wakenet_model_name);

    char *mn_name = esp_srmodel_filter(models, ESP_MN_CHINESE, NULL);
    ESP_RETURN_ON_FALSE(mn_name, ESP_FAIL, TAG, "no Chinese MultiNet model found");
    s_multinet = esp_mn_handle_from_name(mn_name);
    ESP_RETURN_ON_FALSE(s_multinet, ESP_FAIL, TAG, "create MultiNet handle failed");

    s_multinet_data = s_multinet->create(mn_name, CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_TIMEOUT_MS);
    ESP_RETURN_ON_FALSE(s_multinet_data, ESP_FAIL, TAG, "create MultiNet data failed");
#if CONFIG_SPARKBOT_VOICE_CHASSIS_MN_THRESHOLD_PERCENT > 0
    const float mn_threshold = CONFIG_SPARKBOT_VOICE_CHASSIS_MN_THRESHOLD_PERCENT / 100.0f;
    const int threshold_ret = s_multinet->set_det_threshold(s_multinet_data, mn_threshold);
    if (threshold_ret <= 0) {
        ESP_LOGW(TAG, "set MultiNet threshold %.2f failed, keep model default",
                 (double)mn_threshold);
    } else {
        ESP_LOGI(TAG, "MultiNet threshold set to %.2f", (double)mn_threshold);
    }
#else
    ESP_LOGI(TAG, "MultiNet threshold: model default");
#endif
    ESP_LOGI(TAG, "MultiNet model: %s", mn_name);
    ESP_LOGI(TAG, "Wake mode=%d, command timeout=%d ms, mic gain=%d, LCD=%d",
             afe_config.wakenet_mode,
             CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_TIMEOUT_MS,
             CONFIG_SPARKBOT_VOICE_CHASSIS_MIC_GAIN,
             CONFIG_SPARKBOT_VOICE_CHASSIS_LCD_ENABLE);
    ESP_LOGI(TAG, "Wake prompt=%d, command VAD gate=%d, settle=%d ms",
             CONFIG_SPARKBOT_VOICE_CHASSIS_WAKE_PROMPT_ENABLE,
             CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_VAD_GATE,
             CONFIG_SPARKBOT_VOICE_CHASSIS_COMMAND_SETTLE_MS);

    esp_err_t command_ret = esp_mn_commands_clear();
    if (command_ret != ESP_OK) {
        ESP_LOGW(TAG, "MultiNet command list was not ready, allocate it explicitly");
        ESP_RETURN_ON_ERROR(esp_mn_commands_alloc(s_multinet, s_multinet_data),
                            TAG, "create MultiNet command list failed");
        ESP_RETURN_ON_ERROR(esp_mn_commands_clear(), TAG, "clear MultiNet commands failed");
    }

    int registered_commands = 0;
    for (size_t i = 0; i < ARRAY_SIZE(s_command_phrases); i++) {
        const esp_err_t add_ret = esp_mn_commands_add(s_command_phrases[i].command_id,
                                                      (char *)s_command_phrases[i].phrase);
        if (add_ret != ESP_OK) {
            ESP_LOGW(TAG, "skip invalid command phrase: %s", s_command_phrases[i].phrase);
            continue;
        }
        registered_commands++;
    }
    ESP_RETURN_ON_FALSE(registered_commands > 0, ESP_FAIL, TAG, "no valid MultiNet commands registered");

    esp_mn_error_t *command_errors = esp_mn_commands_update();
    if (command_errors && command_errors->num > 0) {
        for (int i = 0; i < command_errors->num; i++) {
            ESP_LOGW(TAG, "MultiNet rejected phrase: %s", command_errors->phrases[i]->string);
        }
        ESP_RETURN_ON_FALSE(false, ESP_FAIL, TAG, "MultiNet command update failed");
    }
    esp_mn_commands_print();
    s_multinet->print_active_speech_commands(s_multinet_data);

#if CONFIG_SPARKBOT_VOICE_CHASSIS_WAKE_PROMPT_ENABLE
    ESP_RETURN_ON_ERROR(sparkbot_audio_prepare_speaker(65), TAG, "prepare wake prompt speaker failed");
#endif

    BaseType_t task_ret = xTaskCreatePinnedToCore(audio_feed_task, "sr_feed", 4 * 1024, afe_data, 5, NULL, 1);
    ESP_RETURN_ON_FALSE(task_ret == pdPASS, ESP_FAIL, TAG, "create feed task failed");

    task_ret = xTaskCreatePinnedToCore(audio_detect_task, "sr_detect", 6 * 1024, afe_data, 5, NULL, 0);
    ESP_RETURN_ON_FALSE(task_ret == pdPASS, ESP_FAIL, TAG, "create detect task failed");

    task_ret = xTaskCreatePinnedToCore(sr_event_handler_task, "sr_handler", 4 * 1024, NULL, 2, NULL, 1);
    ESP_RETURN_ON_FALSE(task_ret == pdPASS, ESP_FAIL, TAG, "create handler task failed");

    return ESP_OK;
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_LOGI(TAG, "ESP-SparkBot voice chassis control starting");
    ESP_ERROR_CHECK(voice_lcd_init());
    voice_lcd_post_state(SPARKBOT_LCD_STATE_BOOT, "BOOT", "SR INIT");
    ESP_ERROR_CHECK(app_sr_start());
    voice_lcd_post_state(SPARKBOT_LCD_STATE_IDLE, "HI LEXIN", "WAITING");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
