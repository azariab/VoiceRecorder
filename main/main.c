/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "bsp_storage.h"
#include "bsp/esp-box-3.h"
#include "settings.h"
#include "app_led.h"
#include "app_rmaker.h"
#include "app_sr.h"
#include "audio_player.h"
#include "file_iterator.h"
#include "gui/ui_main.h"
#include "ui_sensor_monitor.h"

#include "bsp_board.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "main";

file_iterator_instance_t *file_iterator;

#define MEMORY_MONITOR 0

#if MEMORY_MONITOR
static void monitor_task(void *arg)
{
    (void) arg;

    while (true) {
        ESP_LOGI(TAG, "System Info Trace");
        // printf("\tDescription\tInternal\tSPIRAM\n");
        printf("Current Free Memory\t%d\t\t%d\n",
               heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
               heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        printf("Largest Free Block\t%d\t\t%d\n",
               heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
               heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        printf("Min. Ever Free Size\t%d\t\t%d\n",
               heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
               heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));

        // esp_intr_dump(stdout);
        vTaskDelay(pdMS_TO_TICKS(5 * 1000));
    }

    vTaskDelete(NULL);
}

static void sys_monitor_start(void)
{
    BaseType_t ret_val = xTaskCreatePinnedToCore(monitor_task, "Monitor Task", 4 * 1024, NULL, configMAX_PRIORITIES - 3, NULL, 0);
    ESP_ERROR_CHECK_WITHOUT_ABORT((pdPASS == ret_val) ? ESP_OK : ESP_FAIL);
}
#endif

static esp_err_t audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting)
{
    // Volume saved when muting and restored when unmuting. Restoring volume is necessary
    // as es8311_set_voice_mute(true) results in voice volume (REG32) being set to zero.
    static int last_volume;

    sys_param_t *param = settings_get_parameter();
    if (param->volume != 0) {
        last_volume = param->volume;
    }

    bsp_codec_mute_set(setting == AUDIO_PLAYER_MUTE ? true : false);

    // restore the voice volume upon unmuting
    if (setting == AUDIO_PLAYER_UNMUTE) {
        bsp_codec_volume_set(param->volume, NULL);
    }

    ESP_LOGI(TAG, "mute setting %d, volume:%d", setting, last_volume);

    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== APP_MAIN STARTED ===");
    ESP_LOGI(TAG, "Compile time: %s %s", __DATE__, __TIME__);
    /* Initialize NVS. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "NVS initialized successfully");
    ESP_ERROR_CHECK(settings_read_parameter_from_nvs());
    ESP_LOGI(TAG, "Settings read from NVS successfully");

#if !SR_RUN_TEST && MEMORY_MONITOR
    sys_monitor_start(); // Logs should be reduced during SR testing
#endif
    ESP_LOGI(TAG, "=== MAIN INITIALIZATION ===");
    ESP_LOGI(TAG, "About to mount SPIFFS...");
    bsp_spiffs_mount();
    ESP_LOGI(TAG, "SPIFFS mounted successfully");
    
    // Mount SD card for recording
    ESP_LOGI(TAG, "=== SD CARD MOUNTING ===");
    ESP_LOGI(TAG, "About to call bsp_sdcard_mount()...");
    esp_err_t ret = bsp_sdcard_mount();
    ESP_LOGI(TAG, "bsp_sdcard_mount() returned: %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "SD card mount failed - recordings will not work!");
    } else {
        ESP_LOGI(TAG, "SD card mounted successfully");
        
        // Test SD card access
        FILE *test_file = fopen("/sdcard/test.txt", "w");
        if (test_file) {
            fprintf(test_file, "SD card test");
            fclose(test_file);
            ESP_LOGI(TAG, "SD card write test: SUCCESS");
            
            // Clean up test file
            remove("/sdcard/test.txt");
            ESP_LOGI(TAG, "SD card test file cleaned up");
        } else {
            ESP_LOGE(TAG, "SD card write test: FAILED");
        }
    }
    ESP_LOGI(TAG, "=== END SD CARD MOUNTING ===");

    bsp_i2c_init();

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = 0,
        .flags = {
            .buff_dma = true,
        }
    };
    cfg.lvgl_port_cfg.task_affinity = 1;
    bsp_display_start_with_config(&cfg);
    bsp_board_init();

    ESP_LOGI(TAG, "Display LVGL demo");
    sensor_task_state_event_init();
    ESP_ERROR_CHECK(ui_main_start());

    vTaskDelay(pdMS_TO_TICKS(500));
    bsp_display_backlight_on();

    // Initialize file iterator for recordings directory
    ESP_LOGI(TAG, "=== FILE ITERATOR INITIALIZATION ===");
    ESP_LOGI(TAG, "Creating file iterator for directory: /sdcard");
    file_iterator = file_iterator_new("/sdcard");
    if (file_iterator != NULL) {
        size_t count = file_iterator_get_count(file_iterator);
        ESP_LOGI(TAG, "File iterator created successfully");
        ESP_LOGI(TAG, "Files found in /sdcard: %d", count);
        for (size_t i = 0; i < count; i++) {
            const char *filename = file_iterator_get_name_from_index(file_iterator, i);
            ESP_LOGI(TAG, "Startup file %d: %s", i, filename ? filename : "NULL");
        }
    } else {
        ESP_LOGE(TAG, "Failed to create file iterator for /sdcard!");
    }
    ESP_LOGI(TAG, "=== END FILE ITERATOR INITIALIZATION ===");
    audio_player_config_t config = { .mute_fn = audio_mute_function,
                                     .write_fn = bsp_i2s_write,
                                     .clk_set_fn = bsp_codec_set_fs,
                                     .priority = 5
                                   };
    ESP_ERROR_CHECK(audio_player_new(config));

    const board_res_desc_t *brd = bsp_board_get_description();
#ifdef CONFIG_BSP_BOARD_ESP32_S3_BOX_3
    app_pwm_led_init(brd->PMOD2->row2[2], brd->PMOD2->row2[3], brd->PMOD2->row1[3]);
#else
    app_pwm_led_init(brd->PMOD2->row1[1], brd->PMOD2->row1[2], brd->PMOD2->row1[3]);
#endif

    ESP_LOGI(TAG, "speech recognition start");
    vTaskDelay(pdMS_TO_TICKS(4 * 1000));
    app_sr_start(false);
    app_rmaker_start();
}

