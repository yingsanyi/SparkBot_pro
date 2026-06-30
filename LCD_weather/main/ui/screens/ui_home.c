#include "../ui.h"

void ui_home_screen_init(void)
{
    ui_home = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_home, LV_OBJ_FLAG_SCROLLABLE);

    ui_Panel1 = lv_obj_create(ui_home);
    lv_obj_set_size(ui_Panel1, 240, 240);
    lv_obj_set_align(ui_Panel1, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Panel1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_Panel1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Panel1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Panel1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Panel1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_Panel1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_hour = lv_label_create(ui_Panel1);
    lv_obj_set_pos(ui_hour, -60, 0);
    lv_obj_set_align(ui_hour, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_hour, "00");
    lv_obj_set_style_text_color(ui_hour, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_hour, &ui_font_ComfortaaBold75, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_min = lv_label_create(ui_Panel1);
    lv_obj_set_pos(ui_min, 60, 0);
    lv_obj_set_align(ui_min, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_min, "00");
    lv_obj_set_style_text_color(ui_min, lv_color_hex(0xF1BA3B), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_min, &ui_font_ComfortaaBold75, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_date = lv_label_create(ui_Panel1);
    lv_obj_set_pos(ui_date, 25, 15);
    lv_obj_set_align(ui_date, LV_ALIGN_CENTER);
    lv_label_set_text(ui_date, "--/--");
    lv_obj_set_style_text_color(ui_date, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_date, &ui_font_OPPOSansH18, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_weekday = lv_label_create(ui_Panel1);
    lv_obj_set_pos(ui_weekday, 85, 15);
    lv_obj_set_align(ui_weekday, LV_ALIGN_CENTER);
    lv_label_set_text(ui_weekday, "--");
    lv_obj_set_style_text_color(ui_weekday, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_weekday, &ui_font_OPPOSansH25, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_temp = lv_label_create(ui_Panel1);
    lv_obj_set_pos(ui_temp, 60, 30);
    lv_obj_set_align(ui_temp, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_temp, "--");
    lv_obj_set_style_text_color(ui_temp, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_temp, &ui_font_OPPOSansL70, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_weather = lv_label_create(ui_Panel1);
    lv_obj_set_pos(ui_weather, -60, 5);
    lv_obj_set_align(ui_weather, LV_ALIGN_CENTER);
    lv_label_set_text(ui_weather, "--");
    lv_obj_set_style_text_color(ui_weather, lv_color_hex(0xF1BA3B), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_weather, &ui_font_OPPOSansH25, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_location = lv_label_create(ui_Panel1);
    lv_obj_set_width(ui_location, 128);
    lv_obj_set_pos(ui_location, -60, 31);
    lv_obj_set_align(ui_location, LV_ALIGN_CENTER);
    lv_label_set_long_mode(ui_location, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(ui_location, "Area --");
    lv_obj_set_style_text_color(ui_location, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_location, &ui_font_OPPOSansH18, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_colon = lv_label_create(ui_Panel1);
    lv_obj_set_pos(ui_colon, 0, -8);
    lv_obj_set_align(ui_colon, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_colon, ":");
    lv_obj_set_style_text_color(ui_colon, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_colon, &ui_font_ComfortaaBold75, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_weathershow = lv_img_create(ui_Panel1);
    lv_obj_set_size(ui_weathershow, 110, 110);
    lv_obj_set_pos(ui_weathershow, -60, -54);
    lv_obj_set_align(ui_weathershow, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_weathershow, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_weathershow, LV_OBJ_FLAG_SCROLLABLE);
    lv_img_set_zoom(ui_weathershow, 255);

    lv_obj_add_event_cb(ui_home, ui_event_home, LV_EVENT_ALL, NULL);
}
