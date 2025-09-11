/*
 * SPDX-FileCopyrightText: 2024 VoiceRecorder Project
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "lvgl.h"
#include "ui_recorder.h"
#include "ui_main.h"

static const char *TAG = "ui_recorder";

// Recording state
typedef enum {
    RECORDER_STATE_IDLE,
    RECORDER_STATE_RECORDING,
    RECORDER_STATE_STOPPING
} recorder_state_t;

static recorder_state_t g_recorder_state = RECORDER_STATE_IDLE;
static void (*g_end_cb)(void) = NULL;
static lv_obj_t *g_recorder_screen = NULL;
static lv_obj_t *g_record_btn = NULL;
static lv_obj_t *g_status_label = NULL;
static lv_obj_t *g_file_label = NULL;
static lv_obj_t *g_time_label = NULL;
static lv_timer_t *g_timer = NULL;

// Recording task
static TaskHandle_t g_recording_task = NULL;
static QueueHandle_t g_recording_queue = NULL;

// Recording message types
typedef enum {
    RECORD_MSG_START,
    RECORD_MSG_STOP,
    RECORD_MSG_EXIT
} record_msg_type_t;

typedef struct {
    record_msg_type_t type;
    char filename[64];
} record_msg_t;

// Forward declarations
static void recorder_task(void *pvParameters);
static void timer_cb(lv_timer_t *timer);
static void record_btn_cb(lv_event_t *e);
static void back_btn_cb(lv_event_t *e);
static void create_recorder_screen(void);
static void destroy_recorder_screen(void);
static esp_err_t start_recording(const char *filename);
static esp_err_t stop_recording(void);
static void update_status_display(void);

static void recorder_task(void *pvParameters)
{
    record_msg_t msg;
    ESP_LOGI(TAG, "Recorder task started");
    
    while (1) {
        if (xQueueReceive(g_recording_queue, &msg, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Received message type: %d", msg.type);
            
            switch (msg.type) {
                case RECORD_MSG_START:
                    ESP_LOGI(TAG, "Starting recording to file: %s", msg.filename);
                    if (start_recording(msg.filename) == ESP_OK) {
                        g_recorder_state = RECORDER_STATE_RECORDING;
                        ESP_LOGI(TAG, "Recording started successfully");
                    } else {
                        ESP_LOGE(TAG, "Failed to start recording");
                        g_recorder_state = RECORDER_STATE_IDLE;
                    }
                    break;
                    
                case RECORD_MSG_STOP:
                    ESP_LOGI(TAG, "Stopping recording");
                    if (stop_recording() == ESP_OK) {
                        ESP_LOGI(TAG, "Recording stopped successfully");
                    } else {
                        ESP_LOGE(TAG, "Failed to stop recording");
                    }
                    g_recorder_state = RECORDER_STATE_IDLE;
                    break;
                    
                case RECORD_MSG_EXIT:
                    ESP_LOGI(TAG, "Exiting recorder task");
                    vTaskDelete(NULL);
                    break;
            }
            
            // Update UI on main thread
            ui_acquire();
            update_status_display();
            ui_release();
        }
    }
}

static void timer_cb(lv_timer_t *timer)
{
    static uint32_t recording_time = 0;
    
    if (g_recorder_state == RECORDER_STATE_RECORDING) {
        recording_time++;
        uint32_t minutes = recording_time / 60;
        uint32_t seconds = recording_time % 60;
        lv_label_set_text_fmt(g_time_label, "%02lu:%02lu", minutes, seconds);
    } else {
        recording_time = 0;
        lv_label_set_text_static(g_time_label, "00:00");
    }
}

static void record_btn_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        record_msg_t msg;
        
        if (g_recorder_state == RECORDER_STATE_IDLE) {
            // Start recording
            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);
            
            snprintf(msg.filename, sizeof(msg.filename), "/sdcard/rec/rec_%04d%02d%02d_%02d%02d%02d.wav",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            
            msg.type = RECORD_MSG_START;
            // filename already set above
            
            ESP_LOGI(TAG, "Sending start recording message");
            xQueueSend(g_recording_queue, &msg, portMAX_DELAY);
            
        } else if (g_recorder_state == RECORDER_STATE_RECORDING) {
            // Stop recording
            msg.type = RECORD_MSG_STOP;
            ESP_LOGI(TAG, "Sending stop recording message");
            xQueueSend(g_recording_queue, &msg, portMAX_DELAY);
        }
    }
}

static void back_btn_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Back button clicked");
        
        // Stop recording if active
        if (g_recorder_state == RECORDER_STATE_RECORDING) {
            record_msg_t msg = { .type = RECORD_MSG_STOP };
            xQueueSend(g_recording_queue, &msg, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(100)); // Give time to stop
        }
        
        // Exit recorder task
        record_msg_t msg = { .type = RECORD_MSG_EXIT };
        xQueueSend(g_recording_queue, &msg, portMAX_DELAY);
        
        // Clean up and return to main menu
        destroy_recorder_screen();
        if (g_end_cb) {
            g_end_cb();
        }
    }
}

static void create_recorder_screen(void)
{
    ESP_LOGI(TAG, "Creating recorder screen");
    
    // Create main screen
    g_recorder_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_recorder_screen, lv_obj_get_width(lv_obj_get_parent(g_recorder_screen)), 
                    lv_obj_get_height(lv_obj_get_parent(g_recorder_screen)));
    lv_obj_set_style_bg_color(g_recorder_screen, lv_color_make(237, 238, 239), LV_PART_MAIN);
    lv_obj_set_style_border_width(g_recorder_screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(g_recorder_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(g_recorder_screen);
    lv_label_set_text_static(title, "Voice Recorder");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // Status label
    g_status_label = lv_label_create(g_recorder_screen);
    lv_label_set_text_static(g_status_label, "Ready to record");
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(g_status_label, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    
    // File label
    g_file_label = lv_label_create(g_recorder_screen);
    lv_label_set_text_static(g_file_label, "No file selected");
    lv_obj_set_style_text_font(g_file_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(g_file_label, g_status_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    
    // Time label
    g_time_label = lv_label_create(g_recorder_screen);
    lv_label_set_text_static(g_time_label, "00:00");
    lv_obj_set_style_text_font(g_time_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align_to(g_time_label, g_file_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    
    // Record button
    g_record_btn = lv_btn_create(g_recorder_screen);
    lv_obj_set_size(g_record_btn, 100, 100);
    lv_obj_add_style(g_record_btn, &ui_button_styles()->style_pr, LV_STATE_PRESSED);
    lv_obj_add_style(g_record_btn, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(g_record_btn, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(g_record_btn, lv_color_make(255, 100, 100), LV_PART_MAIN);
    lv_obj_set_style_radius(g_record_btn, 50, LV_PART_MAIN);
    lv_obj_align_to(g_record_btn, g_time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);
    lv_obj_add_event_cb(g_record_btn, record_btn_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *record_label = lv_label_create(g_record_btn);
    lv_label_set_text_static(record_label, "REC");
    lv_obj_set_style_text_color(record_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(record_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(record_label);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(g_recorder_screen);
    lv_obj_set_size(back_btn, 60, 40);
    lv_obj_add_style(back_btn, &ui_button_styles()->style_pr, LV_STATE_PRESSED);
    lv_obj_add_style(back_btn, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(back_btn, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(100, 100, 100), LV_PART_MAIN);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_add_event_cb(back_btn, back_btn_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text_static(back_label, "Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(back_label);
    
    // Create timer for time display
    g_timer = lv_timer_create(timer_cb, 1000, NULL);
}

static void destroy_recorder_screen(void)
{
    ESP_LOGI(TAG, "Destroying recorder screen");
    
    if (g_timer) {
        lv_timer_del(g_timer);
        g_timer = NULL;
    }
    
    if (g_recorder_screen) {
        lv_obj_del(g_recorder_screen);
        g_recorder_screen = NULL;
    }
    
    g_record_btn = NULL;
    g_status_label = NULL;
    g_file_label = NULL;
    g_time_label = NULL;
}

static esp_err_t start_recording(const char *filename)
{
    ESP_LOGI(TAG, "Starting recording to: %s", filename);
    
    // TODO: Implement actual audio recording
    // For now, just simulate recording start
    ESP_LOGI(TAG, "Audio recording started (simulated)");
    
    return ESP_OK;
}

static esp_err_t stop_recording(void)
{
    ESP_LOGI(TAG, "Stopping recording");
    
    // TODO: Implement actual audio recording stop
    // For now, just simulate recording stop
    ESP_LOGI(TAG, "Audio recording stopped (simulated)");
    
    return ESP_OK;
}

static void update_status_display(void)
{
    if (!g_status_label || !g_record_btn) return;
    
    switch (g_recorder_state) {
        case RECORDER_STATE_IDLE:
            lv_label_set_text_static(g_status_label, "Ready to record");
            lv_obj_set_style_bg_color(g_record_btn, lv_color_make(255, 100, 100), LV_PART_MAIN);
            break;
            
        case RECORDER_STATE_RECORDING:
            lv_label_set_text_static(g_status_label, "Recording...");
            lv_obj_set_style_bg_color(g_record_btn, lv_color_make(100, 255, 100), LV_PART_MAIN);
            break;
            
        case RECORDER_STATE_STOPPING:
            lv_label_set_text_static(g_status_label, "Stopping...");
            lv_obj_set_style_bg_color(g_record_btn, lv_color_make(255, 255, 100), LV_PART_MAIN);
            break;
    }
}

void ui_recorder_start(void (*end_cb)(void))
{
    ESP_LOGI(TAG, "Starting voice recorder UI");
    
    g_end_cb = end_cb;
    g_recorder_state = RECORDER_STATE_IDLE;
    
    // Create recording queue
    g_recording_queue = xQueueCreate(5, sizeof(record_msg_t));
    if (!g_recording_queue) {
        ESP_LOGE(TAG, "Failed to create recording queue");
        return;
    }
    
    // Create recording task
    BaseType_t ret = xTaskCreatePinnedToCore(recorder_task, "recorder_task", 4096, NULL, 5, &g_recording_task, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create recording task");
        vQueueDelete(g_recording_queue);
        g_recording_queue = NULL;
        return;
    }
    
    // Create UI
    ui_acquire();
    create_recorder_screen();
    ui_release();
    
    ESP_LOGI(TAG, "Voice recorder UI started successfully");
}

void ui_recorder_end(void)
{
    ESP_LOGI(TAG, "Ending voice recorder UI");
    
    // Send exit message to task
    if (g_recording_queue) {
        record_msg_t msg = { .type = RECORD_MSG_EXIT };
        xQueueSend(g_recording_queue, &msg, portMAX_DELAY);
        
        // Wait for task to finish
        if (g_recording_task) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        vQueueDelete(g_recording_queue);
        g_recording_queue = NULL;
    }
    
    // Clean up UI
    ui_acquire();
    destroy_recorder_screen();
    ui_release();
    
    g_recording_task = NULL;
    g_end_cb = NULL;
    
    ESP_LOGI(TAG, "Voice recorder UI ended");
}
