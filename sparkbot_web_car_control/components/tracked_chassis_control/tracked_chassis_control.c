#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "tracked_chassis_control.h"

#define TRACKED_CHASSIS_UART_PORT UART_NUM_1
#define TRACKED_CHASSIS_BAUD_RATE 115200
#define TRACKED_CHASSIS_UART_BUF_SIZE 512

static const char *TAG = "tracked_chassis";

static SemaphoreHandle_t s_uart_lock;
static bool s_uart_ready;

static float clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void format_fixed_2(char *dst, size_t dst_len, float value)
{
    int scaled = value >= 0.0f ? (int)(value * 100.0f + 0.5f) : (int)(value * 100.0f - 0.5f);
    const char *sign = "";
    if (scaled < 0) {
        sign = "-";
        scaled = -scaled;
    }
    snprintf(dst, dst_len, "%s%d.%02d", sign, scaled / 100, scaled % 100);
}

static esp_err_t tracked_chassis_write(const char *command)
{
    ESP_RETURN_ON_FALSE(s_uart_ready, ESP_ERR_INVALID_STATE, TAG, "UART is not ready");
    ESP_RETURN_ON_FALSE(command && command[0], ESP_ERR_INVALID_ARG, TAG, "empty command");

    if (s_uart_lock) {
        xSemaphoreTake(s_uart_lock, portMAX_DELAY);
    }

    const int len = strlen(command);
    const int written = uart_write_bytes(TRACKED_CHASSIS_UART_PORT, command, len);

    if (s_uart_lock) {
        xSemaphoreGive(s_uart_lock);
    }

    ESP_RETURN_ON_FALSE(written == len, ESP_FAIL, TAG, "UART write failed");
    ESP_LOGI(TAG, "UART -> %s", command);
    return ESP_OK;
}

esp_err_t tracked_chassis_control_start(void)
{
    if (s_uart_ready) {
        return ESP_OK;
    }

    s_uart_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_uart_lock, ESP_ERR_NO_MEM, TAG, "create UART mutex failed");

    const uart_config_t uart_config = {
        .baud_rate = TRACKED_CHASSIS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(TRACKED_CHASSIS_UART_PORT,
                                            TRACKED_CHASSIS_UART_BUF_SIZE * 2,
                                            0, 0, NULL, 0),
                        TAG, "install UART driver failed");
    ESP_RETURN_ON_ERROR(uart_param_config(TRACKED_CHASSIS_UART_PORT, &uart_config),
                        TAG, "config UART failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(TRACKED_CHASSIS_UART_PORT,
                                     TRACKED_CHASSIS_UART_TXD,
                                     TRACKED_CHASSIS_UART_RXD,
                                     TRACKED_CHASSIS_UART_RTS,
                                     TRACKED_CHASSIS_UART_CTS),
                        TAG, "set UART pins failed");

    s_uart_ready = true;
    ESP_LOGI(TAG, "Chassis UART ready: TX GPIO%d, RX GPIO%d, %d baud",
             TRACKED_CHASSIS_UART_TXD, TRACKED_CHASSIS_UART_RXD, TRACKED_CHASSIS_BAUD_RATE);
    return ESP_OK;
}

esp_err_t tracked_chassis_send_motion(float x, float y)
{
    char command[32];
    char x_text[12];
    char y_text[12];
    x = clamp_float(x, -1.0f, 1.0f);
    y = clamp_float(y, -1.0f, 1.0f);
    format_fixed_2(x_text, sizeof(x_text), x);
    format_fixed_2(y_text, sizeof(y_text), y);
    snprintf(command, sizeof(command), "x%s y%s", x_text, y_text);
    return tracked_chassis_write(command);
}

esp_err_t tracked_chassis_rgb_light_control(uint8_t rgb_mode)
{
    char command[12];
    snprintf(command, sizeof(command), "w%u", rgb_mode);
    return tracked_chassis_write(command);
}

esp_err_t tracked_chassis_set_dance_mode(uint8_t dance_mode)
{
    char command[12];
    snprintf(command, sizeof(command), "d%u", dance_mode);
    return tracked_chassis_write(command);
}

esp_err_t tracked_chassis_set_correction(float correction)
{
    char command[16];
    char value_text[12];
    correction = clamp_float(correction, -1.0f, 1.0f);
    format_fixed_2(value_text, sizeof(value_text), correction);
    snprintf(command, sizeof(command), "c%s", value_text);
    return tracked_chassis_write(command);
}
