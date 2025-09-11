/*
 * SPDX-FileCopyrightText: 2024 VoiceRecorder Project
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "bsp/esp-box-3.h"
#include "bsp/esp-bsp.h"
#include "bsp_board.h"
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
static FILE *g_recording_file = NULL;
static char g_current_filename[128];
static uint32_t g_recording_start_time = 0;
static uint32_t g_recording_duration = 0;

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

// WAV file header structure
typedef struct {
    char riff_header[4];        // "RIFF"
    uint32_t wav_size;          // File size - 8
    char wave_header[4];        // "WAVE"
    char fmt_header[4];         // "fmt "
    uint32_t fmt_chunk_size;    // 16
    uint16_t audio_format;      // 1 for PCM
    uint16_t num_channels;      // 2 for stereo
    uint32_t sample_rate;       // 16000
    uint32_t byte_rate;         // sample_rate * num_channels * bits_per_sample / 8
    uint16_t sample_alignment;  // num_channels * bits_per_sample / 8
    uint16_t bit_depth;         // 16
    char data_header[4];        // "data"
    uint32_t data_bytes;        // Size of audio data
} wav_header_t;

static void generate_filename(char *filename, size_t max_len)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    snprintf(filename, max_len, "/sdcard/recording_%04d%02d%02d_%02d%02d%02d.wav",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

static void write_wav_header(FILE *file, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, uint32_t data_size)
{
    wav_header_t header;
    
    // Fill WAV header
    memcpy(header.riff_header, "RIFF", 4);
    header.wav_size = data_size + sizeof(wav_header_t) - 8;
    memcpy(header.wave_header, "WAVE", 4);
    memcpy(header.fmt_header, "fmt ", 4);
    header.fmt_chunk_size = 16;
    header.audio_format = 1; // PCM
    header.num_channels = channels;
    header.sample_rate = sample_rate;
    header.byte_rate = sample_rate * channels * bits_per_sample / 8;
    header.sample_alignment = channels * bits_per_sample / 8;
    header.bit_depth = bits_per_sample;
    memcpy(header.data_header, "data", 4);
    header.data_bytes = data_size;
    
    fwrite(&header, sizeof(wav_header_t), 1, file);
}

static void recording_task(void *pvParameters)
{
    record_msg_t msg;
    int16_t *audio_buffer = NULL;
    const int audio_chunksize = 512; // Samples per channel
    const int channels = 2; // Stereo
    const int sample_rate = 16000;
    const int bits_per_sample = 16;
    size_t bytes_read;
    uint32_t total_samples = 0;
    
    ESP_LOGI(TAG, "Recording task started");
    
    // Allocate audio buffer
    audio_buffer = heap_caps_malloc(audio_chunksize * channels * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vTaskDelete(NULL);
    }
    
    // Set up codec for recording
    bsp_codec_set_fs(sample_rate, bits_per_sample, I2S_SLOT_MODE_STEREO);
    bsp_codec_mute_set(false);
    bsp_codec_volume_set(50, NULL);
    
    while (1) {
        if (xQueueReceive(g_recording_queue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.type) {
                case RECORD_MSG_START:
                    ESP_LOGI(TAG, "Starting recording to: %s", msg.filename);
                    
                    // Create recordings directory if it doesn't exist
                    mkdir("/sdcard", 0755);
                    
                    g_recording_file = fopen(msg.filename, "wb");
                    if (!g_recording_file) {
                        ESP_LOGE(TAG, "Failed to open file for writing: %s", msg.filename);
                        break;
                    }
                    
                    // Write WAV header (will be updated later with correct data size)
                    write_wav_header(g_recording_file, sample_rate, channels, bits_per_sample, 0);
                    
                    g_recorder_state = RECORDER_STATE_RECORDING;
                    g_recording_start_time = xTaskGetTickCount();
                    total_samples = 0;
                    
                    ESP_LOGI(TAG, "Recording started");
                    break;
                    
                case RECORD_MSG_STOP:
                    ESP_LOGI(TAG, "Stopping recording");
                    g_recorder_state = RECORDER_STATE_STOPPING;
                    
                    if (g_recording_file) {
                        // Update WAV header with actual data size
                        uint32_t data_size = total_samples * channels * sizeof(int16_t);
                        fseek(g_recording_file, 0, SEEK_SET);
                        write_wav_header(g_recording_file, sample_rate, channels, bits_per_sample, data_size);
                        fclose(g_recording_file);
                        g_recording_file = NULL;
                    }
                    
                    g_recorder_state = RECORDER_STATE_IDLE;
                    ESP_LOGI(TAG, "Recording stopped, total samples: %lu", total_samples);
                    break;
                    
                case RECORD_MSG_EXIT:
                    ESP_LOGI(TAG, "Recording task exiting");
                    if (g_recording_file) {
                        fclose(g_recording_file);
                        g_recording_file = NULL;
                    }
                    if (audio_buffer) {
                        free(audio_buffer);
                    }
                    vTaskDelete(NULL);
                    break;
            }
        }
        
        // Record audio data if recording
        if (g_recorder_state == RECORDER_STATE_RECORDING && g_recording_file) {
            // Read audio data from I2S
            esp_err_t ret = bsp_i2s_read((char *)audio_buffer, audio_chunksize * channels * sizeof(int16_t), &bytes_read, portMAX_DELAY);
            if (ret == ESP_OK && bytes_read > 0) {
                // Write to file
                fwrite(audio_buffer, 1, bytes_read, g_recording_file);
                total_samples += bytes_read / (channels * sizeof(int16_t));
                
                // Update duration
                g_recording_duration = (xTaskGetTickCount() - g_recording_start_time) / portTICK_PERIOD_MS;
            }
        }
    }
}

static void timer_cb(lv_timer_t *timer)
{
    if (g_recorder_state == RECORDER_STATE_RECORDING) {
        // Update time display
        uint32_t minutes = g_recording_duration / 60000;
        uint32_t seconds = (g_recording_duration % 60000) / 1000;
        lv_label_set_text_fmt(g_time_label, "%02lu:%02lu", minutes, seconds);
    }
}

static void record_btn_event_cb(lv_event_t *e)
{
    record_msg_t msg;
    
    if (g_recorder_state == RECORDER_STATE_IDLE) {
        // Start recording
        generate_filename(g_current_filename, sizeof(g_current_filename));
        strcpy(msg.filename, g_current_filename);
        msg.type = RECORD_MSG_START;
        
        xQueueSend(g_recording_queue, &msg, portMAX_DELAY);
        
        // Update UI
        lv_obj_set_style_bg_color(g_record_btn, lv_color_hex(0x00FF00), LV_PART_MAIN);
        lv_label_set_text(g_status_label, "Recording...");
        lv_label_set_text(g_file_label, g_current_filename);
        
    } else if (g_recorder_state == RECORDER_STATE_RECORDING) {
        // Stop recording
        msg.type = RECORD_MSG_STOP;
        xQueueSend(g_recording_queue, &msg, portMAX_DELAY);
        
        // Update UI
        lv_obj_set_style_bg_color(g_record_btn, lv_color_hex(0xFF0000), LV_PART_MAIN);
        lv_label_set_text(g_status_label, "Ready to record");
        lv_label_set_text(g_time_label, "00:00");
    }
}

static void back_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Back button clicked");
    
    // Stop recording if active
    if (g_recorder_state == RECORDER_STATE_RECORDING) {
        record_msg_t msg = { .type = RECORD_MSG_STOP };
        xQueueSend(g_recording_queue, &msg, portMAX_DELAY);
    }
    
    ui_recorder_end();
}

void ui_recorder_start(void (*end_cb)(void))
{
    ESP_LOGI(TAG, "Starting voice recorder UI");
    
    g_end_cb = end_cb;
    
    // Create recording queue
    g_recording_queue = xQueueCreate(5, sizeof(record_msg_t));
    if (g_recording_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create recording queue");
        return;
    }
    
    // Create recorder screen
    g_recorder_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_recorder_screen, lv_obj_get_width(lv_scr_act()), lv_obj_get_height(lv_scr_act()));
    lv_obj_set_style_bg_color(g_recorder_screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_clear_flag(g_recorder_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create status label
    g_status_label = lv_label_create(g_recorder_screen);
    lv_label_set_text(g_status_label, "Ready to record");
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(g_status_label, LV_ALIGN_TOP_MID, 0, 20);
    
    // Create file label
    g_file_label = lv_label_create(g_recorder_screen);
    lv_label_set_text(g_file_label, "File: recording.wav");
    lv_obj_set_style_text_color(g_file_label, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_file_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(g_file_label, g_status_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    
    // Create time label
    g_time_label = lv_label_create(g_recorder_screen);
    lv_label_set_text(g_time_label, "00:00");
    lv_obj_set_style_text_color(g_time_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_time_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(g_time_label, LV_ALIGN_CENTER, 0, -20);
    
    // Create record button
    g_record_btn = lv_btn_create(g_recorder_screen);
    lv_obj_set_size(g_record_btn, 80, 80);
    lv_obj_align(g_record_btn, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(g_record_btn, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_set_style_radius(g_record_btn, 40, LV_PART_MAIN);
    
    lv_obj_t *record_label = lv_label_create(g_record_btn);
    lv_label_set_text(record_label, "REC");
    lv_obj_set_style_text_color(record_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(record_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(record_label);
    
    lv_obj_add_event_cb(g_record_btn, record_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    // Create back button
    lv_obj_t *back_btn = lv_btn_create(g_recorder_screen);
    lv_obj_set_size(back_btn, 60, 30);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), LV_PART_MAIN);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(back_label);
    
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    // Create timer for updating display
    g_timer = lv_timer_create(timer_cb, 100, NULL);
    
    // Create recording task
    xTaskCreate(recording_task, "recording_task", 4096, NULL, 5, &g_recording_task);
    
    ESP_LOGI(TAG, "Voice recorder UI created successfully");
}

void ui_recorder_end(void)
{
    ESP_LOGI(TAG, "Ending voice recorder UI");
    
    // Stop recording task
    if (g_recording_queue) {
        record_msg_t msg = { .type = RECORD_MSG_EXIT };
        xQueueSend(g_recording_queue, &msg, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(100)); // Give task time to exit
        vQueueDelete(g_recording_queue);
        g_recording_queue = NULL;
    }
    
    // Delete timer
    if (g_timer) {
        lv_timer_del(g_timer);
        g_timer = NULL;
    }
    
    // Delete screen
    if (g_recorder_screen) {
        lv_obj_del(g_recorder_screen);
        g_recorder_screen = NULL;
    }
    
    // Reset state
    g_recorder_state = RECORDER_STATE_IDLE;
    
    if (g_end_cb) {
        g_end_cb();
    }
}