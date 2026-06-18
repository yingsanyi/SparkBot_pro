#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "tracked_chassis_control.h"

static const char *TAG = "tracked_chassis";

#define TRACKED_CHASSIS_UART_NUM       UART_NUM_1
#define TRACKED_CHASSIS_UART_BAUD_RATE 115200
#define TRACKED_CHASSIS_UART_BUF_SIZE  1024

static bool s_uart_started = false;

static esp_err_t tracked_chassis_uart_init(void)
{
    if (s_uart_started) {
        return ESP_OK;
    }

    const uart_config_t uart_config = {
        .baud_rate = TRACKED_CHASSIS_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(TRACKED_CHASSIS_UART_NUM,
                                            TRACKED_CHASSIS_UART_BUF_SIZE * 2,
                                            0, 0, NULL, 0),
                        TAG, "install UART driver failed");
    ESP_RETURN_ON_ERROR(uart_param_config(TRACKED_CHASSIS_UART_NUM, &uart_config),
                        TAG, "configure UART failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(TRACKED_CHASSIS_UART_NUM,
                                     TRACKED_CHASSIS_UART_TXD,
                                     TRACKED_CHASSIS_UART_RXD,
                                     TRACKED_CHASSIS_UART_RTS,
                                     TRACKED_CHASSIS_UART_CTS),
                        TAG, "set UART pins failed");

    s_uart_started = true;
    return ESP_OK;
}

void tracked_chassis_control_start(void)
{
    ESP_ERROR_CHECK(tracked_chassis_uart_init());
    ESP_LOGI(TAG, "UART1 ready: TX GPIO%d, RX GPIO%d, baud %d",
             TRACKED_CHASSIS_UART_TXD, TRACKED_CHASSIS_UART_RXD, TRACKED_CHASSIS_UART_BAUD_RATE);
}

static void tracked_chassis_write(const char *command)
{
    if (!s_uart_started) {
        tracked_chassis_control_start();
    }

    const size_t len = strlen(command);
    if (len == 0) {
        return;
    }

    const int written = uart_write_bytes(TRACKED_CHASSIS_UART_NUM, command, len);
    if (written != (int)len) {
        ESP_LOGW(TAG, "UART wrote %d/%u bytes for %s", written, (unsigned)len, command);
    } else {
        ESP_LOGI(TAG, "Sent chassis command: %s", command);
    }
}

void tracked_chassis_motion_control(const char *command)
{
    tracked_chassis_write(command);
}

void tracked_chassis_stop(void)
{
    tracked_chassis_motion_control("x0.0 y0.0");
}

void tracked_chassis_rgb_light_control(uint8_t rgb_mode)
{
    char command[8] = {0};
    snprintf(command, sizeof(command), "w%u", rgb_mode);
    tracked_chassis_write(command);
}

void tracked_chassis_set_dance_mode(uint8_t dance_mode)
{
    char command[8] = {0};
    snprintf(command, sizeof(command), "d%u", dance_mode);
    tracked_chassis_write(command);
}
