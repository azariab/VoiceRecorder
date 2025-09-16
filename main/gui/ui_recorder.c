/*
 * SPDX-FileCopyrightText: 2024 VoiceRecorder Project
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "bsp/esp-box-3.h"
#include "bsp/esp-bsp.h"
#include "bsp_board.h"
#include "esp_afe_sr_models.h"
#include "settings.h"
#include "ui_recorder.h"
#include "ui_main.h"

static const char *TAG = "ui_recorder";
// Export a simple recording lock to prevent player I2S reconfiguration during recording
bool g_recorder_active = false;
// Raw test mode for channel mapping
typedef enum {
    RAW_MODE_STEREO = 0,
    RAW_MODE_LEFT_ONLY,
    RAW_MODE_RIGHT_ONLY,
    RAW_MODE_DOWNMIX
} raw_mode_t;
static raw_mode_t g_raw_mode = RAW_MODE_DOWNMIX;

// Recording state
typedef enum {
    RECORDER_STATE_IDLE,
    RECORDER_STATE_RECORDING
} recorder_state_t;

static recorder_state_t g_recorder_state = RECORDER_STATE_IDLE;
static void (*g_end_cb)(void) = NULL;
static lv_obj_t *g_recorder_screen = NULL;
static lv_obj_t *g_record_btn = NULL;
static lv_obj_t *g_status_label = NULL;
static lv_obj_t *g_file_label = NULL;
static lv_obj_t *g_time_label = NULL;
static lv_obj_t *g_afe_btn = NULL;
static lv_timer_t *g_timer = NULL;
static uint32_t g_recording_start_time = 0;
static uint32_t g_recording_duration = 0;
static FILE *g_recording_file = NULL;
static char g_current_filename[128];
static TaskHandle_t g_recording_task = NULL;
static lv_obj_t *g_agc_btn = NULL;

// AFE controls
static bool g_use_afe = false; // default to RAW path
static int g_agc_mode = 0;    // default AGC OFF
static const esp_afe_sr_iface_t *g_afe_iface = NULL; // unused when DSP only
static esp_afe_sr_data_t *g_afe = NULL;              // unused when DSP only
static int g_afe_feed_chunks = 0;                    // unused when DSP only
static int g_afe_fetch_chunks = 0;                   // unused when DSP only

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

static void list_recorded_files(void)
{
    ESP_LOGI(TAG, "=== RECORDED FILE VERIFICATION ===");
    ESP_LOGI(TAG, "Checking file: %s", g_current_filename);
    
    // Check if the file we just recorded exists
    struct stat st;
    if (stat(g_current_filename, &st) == 0) {
        ESP_LOGI(TAG, "✅ File exists: %s", g_current_filename);
        ESP_LOGI(TAG, "📁 File size: %ld bytes", st.st_size);
        ESP_LOGI(TAG, "✅ Recording verification: SUCCESS");
    } else {
        ESP_LOGE(TAG, "❌ File not found: %s", g_current_filename);
        ESP_LOGE(TAG, "❌ Recording verification: FAILED");
        ESP_LOGE(TAG, "Check SD card mount and file system");
    }
    
    ESP_LOGI(TAG, "=== END FILE VERIFICATION ===");
}

static void recording_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Audio recording task started");
    
    int16_t *audio_buffer = NULL;
    int audio_chunksize = 512; // per-channel samples; may be overridden by AFE
    const int raw_channels = 2; // stereo mic input
    size_t bytes_read;
    
    // Initialize AFE if enabled
    int16_t *afe_out = NULL;
    int16_t *afe_stereo = NULL;
    int16_t *afe_feed3 = NULL;        // interleaved L,R,Ref(0) feed frame
    int16_t *afe_accum_st = NULL;     // interleaved stereo accumulator
    int accum_samples_per_ch = 0;
    int feeds_since_last_fetch = 0;
    if (g_use_afe) {
        // SR AFE used as frontend: SE only, no AEC/VAD/WW/VC; stereo feed
        g_afe_iface = &ESP_AFE_SR_HANDLE;
        afe_config_t cfg = AFE_CONFIG_DEFAULT();
        cfg.aec_init = false;
        cfg.se_init = true;
        cfg.vad_init = false;
        cfg.wakenet_init = false;
        cfg.voice_communication_init = false;
        cfg.voice_communication_agc_init = false;
        cfg.pcm_config.mic_num = 2;               // interleaved stereo
        cfg.pcm_config.ref_num = 0;
        cfg.pcm_config.total_ch_num = 2;
        cfg.pcm_config.sample_rate = 16000;

        g_afe = g_afe_iface->create_from_config(&cfg);
        if (!g_afe) {
            ESP_LOGE(TAG, "AFE create failed, falling back to raw stereo");
            g_use_afe = false;
        } else {
            g_afe_feed_chunks = g_afe_iface->get_feed_chunksize(g_afe);
            g_afe_fetch_chunks = g_afe_iface->get_fetch_chunksize(g_afe);
            if (g_afe_feed_chunks > 0) audio_chunksize = g_afe_feed_chunks;
            ESP_LOGI(TAG, "AFE ready: fs=%dHz mic_ch=%d total_ch=%d feed=%d fetch=%d",
                     g_afe_iface->get_samp_rate(g_afe),
                     g_afe_iface->get_channel_num(g_afe),
                     g_afe_iface->get_total_channel_num(g_afe),
                     g_afe_feed_chunks, g_afe_fetch_chunks);
            if (g_afe_fetch_chunks % g_afe_feed_chunks != 0) {
                ESP_LOGW(TAG, "AFE fetch (%d) not multiple of feed (%d); will accumulate", g_afe_fetch_chunks, g_afe_feed_chunks);
            }
            afe_out = heap_caps_malloc(g_afe_fetch_chunks * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!afe_out) {
                ESP_LOGE(TAG, "AFE out buffer alloc failed; fallback to raw");
                g_use_afe = false;
            }
            if (g_use_afe) {
                afe_stereo = heap_caps_malloc(g_afe_fetch_chunks * 2 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                afe_accum_st = heap_caps_malloc(g_afe_feed_chunks * 2 * 16 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                if (!afe_stereo || !afe_accum_st) {
                    ESP_LOGE(TAG, "AFE buffers alloc failed; fallback to raw");
                    g_use_afe = false;
                }
            }
        }
    }

    // Allocate I2S input buffer (stereo)
    audio_buffer = heap_caps_malloc(audio_chunksize * raw_channels * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vTaskDelete(NULL);
    }
    
    while (1) {
        if (g_recorder_state == RECORDER_STATE_RECORDING && g_recording_file) {
            // Read audio data from I2S
            esp_err_t ret = bsp_i2s_read((char *)audio_buffer, audio_chunksize * raw_channels * sizeof(int16_t), &bytes_read, portMAX_DELAY);
            if (ret == ESP_OK && bytes_read > 0) {
                // Double-check file is still valid before writing
                if (g_recording_file && g_recorder_state == RECORDER_STATE_RECORDING) {
                    if (g_use_afe && g_afe && afe_out && afe_stereo && afe_accum_st) {
                        int samples_per_ch = bytes_read / sizeof(int16_t) / raw_channels;
                        // Append interleaved stereo into accumulator
                        int max_samples_per_ch = g_afe_feed_chunks * 8;
                        int copy = samples_per_ch;
                        if (accum_samples_per_ch + copy > max_samples_per_ch) copy = max_samples_per_ch - accum_samples_per_ch;
                        if (copy > 0) {
                            memcpy(&afe_accum_st[accum_samples_per_ch * 2], audio_buffer, copy * 2 * sizeof(int16_t));
                            accum_samples_per_ch += copy;
                        }
                        // Feed exact frames of size feed_chunks per channel (stereo interleaved)
                        while (accum_samples_per_ch >= g_afe_feed_chunks) {
                            g_afe_iface->feed(g_afe, afe_accum_st);
                            // Shift accumulator by one feed frame (stereo)
                            int remain_per_ch = accum_samples_per_ch - g_afe_feed_chunks;
                            if (remain_per_ch > 0) {
                                memmove(afe_accum_st, &afe_accum_st[g_afe_feed_chunks * 2], remain_per_ch * 2 * sizeof(int16_t));
                            }
                            accum_samples_per_ch = remain_per_ch;
                            // Drain processed frames (mono -> duplicate to stereo)
                            for (;;) {
                                afe_fetch_result_t *res = g_afe_iface->fetch(g_afe);
                                if (!res) { ESP_LOGD(TAG, "AFE fetch: res=NULL"); break; }
                                if (res->ret_value != ESP_OK) { ESP_LOGW(TAG, "AFE fetch ret=%d", res->ret_value); break; }
                                if (!res->data || res->data_size <= 0) { ESP_LOGD(TAG, "AFE fetch: no data (size=%d)", (int)res->data_size); break; }
                                int mono_samples = res->data_size / sizeof(int16_t);
                                for (int s = 0; s < mono_samples; s++) {
                                    int16_t v = res->data[s];
                                    afe_stereo[2 * s + 0] = v;
                                    afe_stereo[2 * s + 1] = v;
                                }
                                size_t wrote = fwrite(afe_stereo, sizeof(int16_t) * 2, mono_samples, g_recording_file);
                                ESP_LOGI(TAG, "AFE wrote %d stereo samples", (int)wrote);
                                feeds_since_last_fetch = 0;
                            }
                        }
                    } else {
                        // RAW test path: choose how to write from L/R
                        int samples = bytes_read / sizeof(int16_t) / raw_channels;
                        if (g_raw_mode == RAW_MODE_STEREO) {
                            fwrite(audio_buffer, 1, bytes_read, g_recording_file);
                        } else {
                            for (int i = 0; i < samples; i++) {
                                int16_t l = audio_buffer[2 * i + 0];
                                int16_t r = audio_buffer[2 * i + 1];
                                int16_t vL = l, vR = r;
                                switch (g_raw_mode) {
                                    case RAW_MODE_LEFT_ONLY:  vL = l; vR = l; break;
                                    case RAW_MODE_RIGHT_ONLY: vL = r; vR = r; break;
                                    case RAW_MODE_DOWNMIX:    {
                                        int16_t m = (int16_t)(((int32_t)l + (int32_t)r) / 2);
                                        vL = m; vR = m; break;
                                    }
                                    default: break;
                                }
                                fwrite(&vL, sizeof(int16_t), 1, g_recording_file);
                                fwrite(&vR, sizeof(int16_t), 1, g_recording_file);
                            }
                        }
                        fflush(g_recording_file);
                    }
                }
            }
        } else {
            // No recording active, sleep briefly
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void generate_filename(char *filename, size_t max_len)
{
    // Use a simple counter-based filename that works with FAT filesystem
    static int file_counter = 1;
    snprintf(filename, max_len, "/sdcard/r/rec%03d.wav", file_counter++);
    ESP_LOGI(TAG, "Generated filename: %s", filename);
}

static void timer_cb(lv_timer_t *timer)
{
    if (g_recorder_state == RECORDER_STATE_RECORDING) {
        // Update duration
        g_recording_duration = (xTaskGetTickCount() - g_recording_start_time) / portTICK_PERIOD_MS;
        
        // Update time display
        uint32_t minutes = g_recording_duration / 60000;
        uint32_t seconds = (g_recording_duration % 60000) / 1000;
        lv_label_set_text_fmt(g_time_label, "%02lu:%02lu", minutes, seconds);
        
        ESP_LOGI(TAG, "Timer update: %02lu:%02lu", minutes, seconds);
    }
}

static void record_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "*** BUTTON CLICKED ***");
    ESP_LOGI(TAG, "Record button clicked, current state: %d", g_recorder_state);
    
    if (g_recorder_state == RECORDER_STATE_IDLE) {
        // Start recording
        ESP_LOGI(TAG, "Starting recording...");
        // Disallow changing AFE during recording
        if (g_afe_btn) lv_obj_add_state(g_afe_btn, LV_STATE_DISABLED);
        if (g_agc_btn) lv_obj_add_state(g_agc_btn, LV_STATE_DISABLED);
        // SR disabled
        
        generate_filename(g_current_filename, sizeof(g_current_filename));
        ESP_LOGI(TAG, "Generated filename: %s", g_current_filename);
        
        // Create recordings directory if it doesn't exist
        ESP_LOGI(TAG, "Ensuring recordings directory /sdcard/r exists");
        int mkdir_result = mkdir("/sdcard/r", 0755);
        if (mkdir_result == 0) {
            ESP_LOGI(TAG, "Created /sdcard/r");
        } else {
            ESP_LOGI(TAG, "mkdir /sdcard/r result: %d (may already exist)", mkdir_result);
        }
        
        // Check if recordings directory exists and is accessible
        struct stat dir_stat;
        int stat_result = stat("/sdcard/r", &dir_stat);
        ESP_LOGI(TAG, "stat /sdcard/r result: %d", stat_result);
        if (stat_result == 0) {
            ESP_LOGI(TAG, "/sdcard/r directory exists and is accessible");
        } else {
            ESP_LOGE(TAG, "/sdcard/r directory does not exist or is not accessible");
        }
        
        // List existing files to check if we're hitting the max_files limit
        ESP_LOGI(TAG, "Listing existing recordings...");
        DIR *dir = opendir("/sdcard/r");
        if (dir) {
            struct dirent *entry;
            int file_count = 0;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_REG) {  // Regular file
                    ESP_LOGI(TAG, "Existing file: %s", entry->d_name);
                    file_count++;
                } else if (entry->d_type == DT_DIR) {
                    ESP_LOGI(TAG, "Directory: %s", entry->d_name);
                }
            }
            closedir(dir);
            ESP_LOGI(TAG, "Total files in /sdcard/r: %d", file_count);
            ESP_LOGI(TAG, "SD card max_files limit: 5 (from BSP configuration)");
            if (file_count >= 5) {
                ESP_LOGE(TAG, "WARNING: SD card has %d files, max_files limit is 5!", file_count);
            }
        } else {
            ESP_LOGE(TAG, "Failed to open /sdcard/r directory");
        }
        
        // (Debug write tests removed to avoid cluttering SD root)
        
        // Open file for writing
        ESP_LOGI(TAG, "Opening file for writing: %s", g_current_filename);
        g_recording_file = fopen(g_current_filename, "wb");
        if (!g_recording_file) {
            ESP_LOGE(TAG, "Failed to open file for writing: %s", g_current_filename);
            ESP_LOGE(TAG, "Error: %s (errno: %d)", strerror(errno), errno);
            ESP_LOGE(TAG, "Check if SD card is mounted and accessible");
            return;
        }
        ESP_LOGI(TAG, "File opened successfully for recording");
        
        // Write WAV header (will be updated later with correct data size)
        // Write stereo header to maximize player compatibility; duplicate mono AFE into L/R
        write_wav_header(g_recording_file, 16000, 2, 16, 0);
        // Reset AFE ring buffer at start to avoid stale frames
        if (g_use_afe && g_afe && g_afe_iface && g_afe_iface->reset_buffer) {
            g_afe_iface->reset_buffer(g_afe);
        }
        
        // Set up codec for recording
        bsp_codec_set_fs(16000, 16, I2S_SLOT_MODE_STEREO);
        bsp_codec_mute_set(false);
        bsp_codec_volume_set(50, NULL);
        
        // Update state
        g_recorder_state = RECORDER_STATE_RECORDING;
        g_recorder_active = true;
        g_recording_start_time = xTaskGetTickCount();
        g_recording_duration = 0;
        
        // Update UI
        lv_obj_set_style_bg_color(g_record_btn, lv_color_hex(0x00FF00), LV_PART_MAIN);
        lv_label_set_text(g_status_label, "Recording...");
        lv_label_set_text(g_file_label, g_current_filename);
        
        ESP_LOGI(TAG, "Recording started successfully");
        
    } else if (g_recorder_state == RECORDER_STATE_RECORDING) {
        // Stop recording
        ESP_LOGI(TAG, "Stopping recording...");
        // First, signal the recording task to stop writing
        g_recorder_state = RECORDER_STATE_IDLE;
        // Give the recording task a moment to finish any in-flight write
        vTaskDelay(pdMS_TO_TICKS(50));

        if (g_recording_file) {
            // Get file size to update WAV header
            fflush(g_recording_file);
            long file_size = ftell(g_recording_file);
            if (file_size < (long)sizeof(wav_header_t)) {
                ESP_LOGW(TAG, "Recorded file too small (%ld), writing empty data header", file_size);
                file_size = sizeof(wav_header_t);
            }
            uint32_t data_size = (uint32_t)(file_size - (long)sizeof(wav_header_t));
            
            // Update WAV header with actual data size
            fseek(g_recording_file, 0, SEEK_SET);
            write_wav_header(g_recording_file, 16000, 2, 16, data_size);
            fflush(g_recording_file);
            fclose(g_recording_file);
            g_recording_file = NULL;
            // Drain any remaining AFE frames after stop
            if (g_use_afe && g_afe && g_afe_iface) {
                for (int i = 0; i < 3; i++) {
                    afe_fetch_result_t *res = g_afe_iface->fetch(g_afe);
                    if (!res || res->ret_value != ESP_OK || !res->data || res->data_size <= 0) break;
                }
            }
            
            ESP_LOGI(TAG, "File saved: %s (size: %ld bytes)", g_current_filename, file_size);
            
            // List all recorded files
            list_recorded_files();
        }
        
        // Update UI
        lv_obj_set_style_bg_color(g_record_btn, lv_color_hex(0xFF0000), LV_PART_MAIN);
        lv_label_set_text(g_status_label, "Ready to record");
        lv_label_set_text(g_time_label, "00:00");
        if (g_afe_btn) lv_obj_clear_state(g_afe_btn, LV_STATE_DISABLED);
        if (g_agc_btn) lv_obj_clear_state(g_agc_btn, LV_STATE_DISABLED);
        
        ESP_LOGI(TAG, "Recording stopped successfully");
        g_recorder_active = false;

        // SR disabled
    }
}

static void afe_btn_event_cb(lv_event_t *e)
{
    if (g_recorder_state == RECORDER_STATE_RECORDING) {
        ESP_LOGW(TAG, "Cannot toggle AFE while recording. Stop first.");
        return;
    }
    g_use_afe = !g_use_afe;
    const char *txt = g_use_afe ? "AFE: ON" : "AFE: OFF";
    lv_obj_t *label = lv_obj_get_child(g_afe_btn, 0);
    if (label) lv_label_set_text(label, txt);
    ESP_LOGI(TAG, "AFE toggle -> %s", txt);
}

static void agc_btn_event_cb(lv_event_t *e)
{
    if (g_recorder_state == RECORDER_STATE_RECORDING) {
        ESP_LOGW(TAG, "Cannot toggle AGC while recording. Stop first.");
        return;
    }
    g_agc_mode = (g_agc_mode + 1) % 3; // 0->1->2->0
    const char *txt = (g_agc_mode == 0) ? "AGC: OFF" : (g_agc_mode == 1 ? "AGC: LOW" : "AGC: MED");
    lv_obj_t *label = lv_obj_get_child(g_agc_btn, 0);
    if (label) lv_label_set_text(label, txt);
    ESP_LOGI(TAG, "AGC mode -> %s", txt);
}

static void lr_btn_event_cb(lv_event_t *e)
{
    if (g_recorder_state == RECORDER_STATE_RECORDING) {
        ESP_LOGW(TAG, "Cannot toggle L/R test while recording. Stop first.");
        return;
    }
    g_raw_mode = (raw_mode_t)(((int)g_raw_mode + 1) % 4);
    const char *txt = (g_raw_mode == RAW_MODE_STEREO) ? "RAW: ST" :
                      (g_raw_mode == RAW_MODE_LEFT_ONLY) ? "RAW: L" :
                      (g_raw_mode == RAW_MODE_RIGHT_ONLY) ? "RAW: R" : "RAW: M";
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) lv_label_set_text(label, txt);
    ESP_LOGI(TAG, "Raw mode -> %s", txt);
}

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
    ESP_LOGI(TAG, "Record button created and event callback added");
    
    // Create a right-side settings column to avoid overlapping the Back button
    lv_obj_t *settings_col = lv_obj_create(g_recorder_screen);
    lv_obj_set_size(settings_col, 110, 110);
    lv_obj_align(settings_col, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_set_style_border_width(settings_col, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(settings_col, LV_OPA_TRANSP, LV_PART_MAIN);

    // Back button (top-left)
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
    ESP_LOGI(TAG, "Timer created: %p", g_timer);

    // Apply provisioned settings from config (via settings.c)
    sys_param_t *param = settings_get_parameter();
    g_use_afe = param->rec_use_afe;
    if (param->rec_agc_mode <= 2) g_agc_mode = param->rec_agc_mode;
    if (param->rec_raw_mode <= 3) g_raw_mode = (raw_mode_t)param->rec_raw_mode;
    
    // Create recording task
    BaseType_t ret = xTaskCreate(recording_task, "recording_task", 8192, NULL, 5, &g_recording_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create recording task: %d", ret);
    } else {
        ESP_LOGI(TAG, "Recording task created successfully");
    }
    
    ESP_LOGI(TAG, "Voice recorder UI created successfully");
}

void ui_recorder_end(void)
{
    ESP_LOGI(TAG, "Ending voice recorder UI");
    
    // Stop recording if active
    if (g_recorder_state == RECORDER_STATE_RECORDING && g_recording_file) {
        fclose(g_recording_file);
        g_recording_file = NULL;
        g_recorder_state = RECORDER_STATE_IDLE;
    }
    
    // Delete recording task
    if (g_recording_task) {
        vTaskDelete(g_recording_task);
        g_recording_task = NULL;
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