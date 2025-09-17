/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "esp_log.h"
#include "bsp_board.h"
#include "lvgl.h"
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include "ui_main.h"
#include "ui_device_ctrl.h"

static const char *TAG = "ui_dev_ctrl";

static void (*g_dev_ctrl_end_cb)(void) = NULL;
// Legacy no-op for build compatibility
void ui_dev_ctrl_set_state(ui_dev_type_t type, bool state)
{
    (void)type;
    (void)state;
}


static int delete_all_recordings(const char *dir_path)
{
    int deleted = 0;
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open %s: %s", dir_path, strerror(errno));
        return -1;
    }
    struct dirent *entry;
    char fullpath[256];
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG) {
            continue;
        }
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, entry->d_name);
        if (remove(fullpath) == 0) {
            deleted++;
            ESP_LOGI(TAG, "Deleted: %s", fullpath);
        } else {
            ESP_LOGE(TAG, "Failed to delete %s: %s", fullpath, strerror(errno));
        }
    }
    closedir(dir);
    return deleted;
}

static void on_confirm_delete_cb(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    const char *btn_txt = lv_msgbox_get_active_btn_text(mbox);
    lv_obj_t *parent = (lv_obj_t *)lv_event_get_user_data(e);
    if (btn_txt && strcmp(btn_txt, "Delete") == 0) {
        // Ensure directory exists
        struct stat st; if (stat("/sdcard/r", &st) != 0) { mkdir("/sdcard/r", 0755); }
        int n = delete_all_recordings("/sdcard/r");
        char msg[96];
        if (n >= 0) {
            snprintf(msg, sizeof(msg), "Deleted %d file(s) from /sdcard/r.", n);
        } else {
            snprintf(msg, sizeof(msg), "Failed to access /sdcard/r. See logs.");
        }
        lv_obj_del(mbox);
        lv_obj_t *done = lv_msgbox_create(parent, "Done", msg, NULL, true);
        lv_obj_center(done);
    } else {
        lv_obj_del(mbox);
    }
}

static void delete_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *parent = (lv_obj_t *)lv_event_get_user_data(e);
    static const char *btns[] = {"Cancel", "Delete", NULL};
    lv_obj_t *mbox = lv_msgbox_create(parent,
                                      "Confirm",
                                      "Delete ALL recordings in /sdcard/r?\nThis cannot be undone.",
                                      btns,
                                      true);
    lv_obj_center(mbox);
    lv_obj_add_event_cb(mbox, on_confirm_delete_cb, LV_EVENT_VALUE_CHANGED, parent);
}

static void ui_dev_ctrl_page_return_click_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_user_data(e);
    if (ui_get_btn_op_group()) {
        lv_group_remove_all_objs(ui_get_btn_op_group());
    }
#if !CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite
    bsp_btn_rm_all_callback(BSP_BUTTON_MAIN);
#endif
    lv_obj_del(obj);
    if (g_dev_ctrl_end_cb) {
        g_dev_ctrl_end_cb();
    }
}

#if !CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite
static void btn_return_down_cb(void *handle, void *arg)
{
    lv_obj_t *obj = (lv_obj_t *) arg;
    ui_acquire();
    lv_event_send(obj, LV_EVENT_CLICKED, NULL);
    ui_release();
}
#endif

void ui_device_ctrl_start(void (*fn)(void))
{
    ESP_LOGI(TAG, "device control initialize");
    g_dev_ctrl_end_cb = fn;

    lv_obj_t *page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page, lv_obj_get_width(lv_obj_get_parent(page)), lv_obj_get_height(lv_obj_get_parent(page)) - lv_obj_get_height(ui_main_get_status_bar()));
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(page, lv_obj_get_style_bg_color(lv_scr_act(), LV_STATE_DEFAULT), LV_PART_MAIN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align_to(page, ui_main_get_status_bar(), LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);

    lv_obj_t *btn_return = lv_btn_create(page);
    lv_obj_set_size(btn_return, 24, 24);
    lv_obj_add_style(btn_return, &ui_button_styles()->style, 0);
    lv_obj_add_style(btn_return, &ui_button_styles()->style_pr, LV_STATE_PRESSED);
    lv_obj_add_style(btn_return, &ui_button_styles()->style_focus, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_return, &ui_button_styles()->style_focus, LV_STATE_FOCUSED);
    lv_obj_align(btn_return, LV_ALIGN_TOP_LEFT, 0, -8);
    lv_obj_t *lab_btn_text = lv_label_create(btn_return);
    lv_label_set_text_static(lab_btn_text, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lab_btn_text, lv_color_make(158, 158, 158), LV_STATE_DEFAULT);
    lv_obj_center(lab_btn_text);
    lv_obj_add_event_cb(btn_return, ui_dev_ctrl_page_return_click_cb, LV_EVENT_CLICKED, page);
#if !CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite
    bsp_btn_register_callback(BSP_BUTTON_MAIN, BUTTON_PRESS_UP, btn_return_down_cb, (void *)btn_return);
#endif

    // Single utility: delete all recordings in /sdcard/r
    lv_obj_t *del_btn = lv_btn_create(page);
    lv_obj_set_size(del_btn, 220, 60);
    lv_obj_add_style(del_btn, &ui_button_styles()->style, 0);
    lv_obj_add_style(del_btn, &ui_button_styles()->style_pr, LV_STATE_PRESSED);
    lv_obj_set_style_radius(del_btn, 8, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xAA3030), LV_STATE_DEFAULT);
    lv_obj_align(del_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *lab = lv_label_create(del_btn);
    lv_label_set_text_static(lab, "Delete ALL recordings");
    lv_obj_set_style_text_color(lab, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_center(lab);
    lv_obj_add_event_cb(del_btn, delete_btn_event_cb, LV_EVENT_CLICKED, page);
    if (ui_get_btn_op_group()) {
        lv_group_add_obj(ui_get_btn_op_group(), del_btn);
    }
    if (ui_get_btn_op_group()) {
        lv_group_add_obj(ui_get_btn_op_group(), btn_return);
    }
}
