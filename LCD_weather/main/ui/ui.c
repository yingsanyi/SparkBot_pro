#include "ui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "ui";

lv_obj_t *ui_home;
lv_obj_t *ui_Panel1;
lv_obj_t *ui_hour;
lv_obj_t *ui_min;
lv_obj_t *ui_date;
lv_obj_t *ui_weekday;
lv_obj_t *ui_temp;
lv_obj_t *ui_weather;
lv_obj_t *ui_location;
lv_obj_t *ui_colon;
lv_obj_t *ui_weathershow;

lv_obj_t *ui_title;
lv_obj_t *title_panel;
lv_obj_t *title_wifistate;
lv_obj_t *title_powerstate;
lv_obj_t *title_batterybor;
lv_obj_t *title_timestate;
lv_obj_t *title_batterytxt;

ui_page_name_t ui_pages[UI_PAGE_COUNT];

typedef struct {
    lv_obj_t *obj;
    lv_event_code_t event_code;
    void *user_data;
} lv_sys_event_t;

static QueueHandle_t ui_sys_event;

static void ui_system_update(lv_timer_t *timer)
{
    (void)timer;

    lv_sys_event_t event;
    if (ui_sys_event && pdPASS == xQueueReceive(ui_sys_event, &event, 0)) {
        if (event.event_code == LV_EVENT_SCREEN_HOME) {
            lv_scr_load(ui_home);
        } else if (event.obj) {
            lv_event_send(event.obj, event.event_code, event.user_data);
        }
    }
}

void ui_event_home(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_SCREEN_LOAD_START) {
        if (title_panel && ui_Panel1) {
            lv_obj_set_parent(title_panel, ui_Panel1);
        }
    } else if (event_code == LV_EVENT_LONG_PRESSED ||
               event_code == LV_EVENT_SCREEN_NEXT ||
               event_code == LV_EVENT_SCREEN_PRIVIOUS ||
               event_code == LV_EVENT_PRESSED) {
        ESP_LOGI(TAG, "Stay on weather home");
    }
}

void ui_send_sys_event(lv_obj_t *obj, lv_event_code_t event_code, void *user_data)
{
    if (ui_sys_event == NULL) {
        return;
    }

    lv_sys_event_t event = {
        .obj = obj,
        .event_code = event_code,
        .user_data = user_data,
    };
    xQueueSend(ui_sys_event, &event, 0);
}

void ui_init(void)
{
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp,
                                              lv_palette_main(LV_PALETTE_BLUE),
                                              lv_palette_main(LV_PALETTE_RED),
                                              false,
                                              LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);

    ui_title_screen_init();
    ui_home_screen_init();

    ui_pages[0].page = ui_home;
    ui_pages[0].name = "weather";

    lv_disp_load_scr(ui_home);

    ui_sys_event = xQueueCreate(4, sizeof(lv_sys_event_t));
    ESP_ERROR_CHECK_WITHOUT_ABORT((ui_sys_event) ? ESP_OK : ESP_FAIL);

    lv_timer_create(ui_system_update, 10, NULL);
}
