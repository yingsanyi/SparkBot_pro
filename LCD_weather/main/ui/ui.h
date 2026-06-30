// Weather UI entry points and shared LVGL objects.

#ifndef _WEATHER_UI_H
#define _WEATHER_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

#define LV_EVENT_SCREEN_HOME            (_LV_EVENT_LAST + 1)
#define LV_EVENT_SCREEN_NEXT            (_LV_EVENT_LAST + 2)
#define LV_EVENT_SCREEN_PRIVIOUS        (_LV_EVENT_LAST + 3)

void ui_home_screen_init(void);
void ui_event_home(lv_event_t *e);
extern lv_obj_t *ui_home;
extern lv_obj_t *ui_Panel1;
extern lv_obj_t *ui_hour;
extern lv_obj_t *ui_min;
extern lv_obj_t *ui_date;
extern lv_obj_t *ui_weekday;
extern lv_obj_t *ui_temp;
extern lv_obj_t *ui_weather;
extern lv_obj_t *ui_location;
extern lv_obj_t *ui_colon;
extern lv_obj_t *ui_weathershow;

void ui_title_screen_init(void);
extern lv_obj_t *ui_title;
extern lv_obj_t *title_panel;
extern lv_obj_t *title_wifistate;
extern lv_obj_t *title_powerstate;
extern lv_obj_t *title_batterybor;
extern lv_obj_t *title_timestate;
extern lv_obj_t *title_batterytxt;

void ui_send_sys_event(lv_obj_t *obj, lv_event_code_t event_code, void *user_data);
void ui_init(void);

LV_IMG_DECLARE(ui_img_wifi_png);
LV_IMG_DECLARE(ui_img_battery_png);
LV_IMG_DECLARE(ui_img_wifi_disconnection_png);

LV_FONT_DECLARE(ui_font_ComfortaaBold75);
LV_FONT_DECLARE(ui_font_OPPOSansH18);
LV_FONT_DECLARE(ui_font_OPPOSansH25);
LV_FONT_DECLARE(ui_font_OPPOSansL70);

#define UI_PAGE_COUNT 1

typedef struct {
    lv_obj_t *page;
    char *name;
} ui_page_name_t;

extern ui_page_name_t ui_pages[UI_PAGE_COUNT];

#ifdef __cplusplus
}
#endif

#endif
