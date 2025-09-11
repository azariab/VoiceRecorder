/*
 * SPDX-FileCopyrightText: 2024 VoiceRecorder Project
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "lvgl.h"
#include "ui_recorder.h"
#include "ui_main.h"

static const char *TAG = "ui_recorder";

static void (*g_end_cb)(void) = NULL;
static lv_obj_t *g_recorder_screen = NULL;

static void back_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Back button clicked");
    ui_recorder_end();
}

void ui_recorder_start(void (*end_cb)(void))
{
    ESP_LOGI(TAG, "Starting voice recorder UI");
    
    g_end_cb = end_cb;
    
    // Create recorder screen
    g_recorder_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_recorder_screen, lv_obj_get_width(lv_scr_act()), lv_obj_get_height(lv_scr_act()));
    lv_obj_set_style_bg_color(g_recorder_screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_clear_flag(g_recorder_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create simple status label
    lv_obj_t *status_label = lv_label_create(g_recorder_screen);
    lv_label_set_text(status_label, "Voice Recorder - Simple Test");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -50);
    
    // Create back button
    lv_obj_t *back_btn = lv_btn_create(g_recorder_screen);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_align(back_btn, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), LV_PART_MAIN);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(back_label);
    
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    ESP_LOGI(TAG, "Voice recorder UI created successfully");
}

void ui_recorder_end(void)
{
    ESP_LOGI(TAG, "Ending voice recorder UI");
    
    if (g_recorder_screen) {
        lv_obj_del(g_recorder_screen);
        g_recorder_screen = NULL;
    }
    
    if (g_end_cb) {
        g_end_cb();
    }
}