/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "bsp_board.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "settings.h"
#include "esp_spiffs.h"
#include "json_parser.h"

static const char *TAG = "settings";

#define NAME_SPACE "sys_param"
#define KEY "param"

static sys_param_t g_sys_param = {0};

static const sys_param_t g_default_sys_param = {
    .need_hint = true,
    .sr_lang = SR_LANG_EN,
    .volume = 70, // default volume is 70%
    .radar_en = true,
    .rec_use_afe = false,
    .rec_agc_mode = 0,
    .rec_raw_mode = 3,
};

static esp_err_t settings_check(sys_param_t *param)
{
    esp_err_t ret;
    ESP_GOTO_ON_FALSE(param->sr_lang < SR_LANG_MAX, ESP_ERR_INVALID_ARG, reset, TAG, "language incorrect");
    ESP_GOTO_ON_FALSE(param->volume <= 100, ESP_ERR_INVALID_ARG, reset, TAG, "volume incorrect");
    return ret;
reset:
    ESP_LOGW(TAG, "Set to default");
    memcpy(&g_sys_param, &g_default_sys_param, sizeof(sys_param_t));
    return ret;
}

esp_err_t settings_read_parameter_from_nvs(void)
{
    nvs_handle_t my_handle = 0;
    esp_err_t ret = nvs_open(NAME_SPACE, NVS_READONLY, &my_handle);
    if (ESP_ERR_NVS_NOT_FOUND == ret) {
        ESP_LOGW(TAG, "Not found, Set to default");
        memcpy(&g_sys_param, &g_default_sys_param, sizeof(sys_param_t));
        settings_write_parameter_to_nvs();
        return ESP_OK;
    }

    ESP_GOTO_ON_FALSE(ESP_OK == ret, ret, err, TAG, "nvs open failed (0x%x)", ret);

    size_t len = sizeof(sys_param_t);
    ret = nvs_get_blob(my_handle, KEY, &g_sys_param, &len);
    ESP_GOTO_ON_FALSE(ESP_OK == ret, ret, err, TAG, "can't read param");
    nvs_close(my_handle);

    settings_check(&g_sys_param);
    return ret;
err:
    if (my_handle) {
        nvs_close(my_handle);
    }
    return ret;
}

esp_err_t settings_write_parameter_to_nvs(void)
{
    ESP_LOGI(TAG, "Saving settings");
    settings_check(&g_sys_param);
    nvs_handle_t my_handle = {0};
    esp_err_t err = nvs_open(NAME_SPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        err = nvs_set_blob(my_handle, KEY, &g_sys_param, sizeof(sys_param_t));
        err |= nvs_commit(my_handle);
        nvs_close(my_handle);
    }
    return ESP_OK == err ? ESP_OK : ESP_FAIL;
}

sys_param_t *settings_get_parameter(void)
{
    return &g_sys_param;
}

esp_err_t settings_load_app_config(void)
{
    // Expect /spiffs/config.json (mounted earlier in app_main)
    FILE *f = fopen("/spiffs/config.json", "rb");
    if (!f) {
        // Backward compat: accept recorder_config.json
        f = fopen("/spiffs/recorder_config.json", "rb");
    }
    if (!f) {
        ESP_LOGW(TAG, "config.json not found; using defaults");
        return ESP_OK;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 4096) {
        ESP_LOGW(TAG, "config.json size invalid: %ld", len);
        fclose(f);
        return ESP_OK;
    }
    char *buf = (char *)malloc(len + 1);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);

    jparse_ctx_t jp;
    if (json_parse_start(&jp, buf, len) != OS_SUCCESS) {
        free(buf);
        return ESP_FAIL;
    }
    // Recording settings
    bool bval;
    int ival;
    if (json_obj_get_bool(&jp, "recording.use_afe", &bval) == OS_SUCCESS) {
        g_sys_param.rec_use_afe = bval;
    }
    if (json_obj_get_int(&jp, "recording.agc_mode", &ival) == OS_SUCCESS) {
        if (ival >= 0 && ival <= 2) g_sys_param.rec_agc_mode = (uint8_t)ival;
    }
    if (json_obj_get_int(&jp, "recording.raw_mode", &ival) == OS_SUCCESS) {
        if (ival >= 0 && ival <= 3) g_sys_param.rec_raw_mode = (uint8_t)ival;
    }
    json_parse_end(&jp);
    free(buf);
    ESP_LOGI(TAG, "Loaded app config: use_afe=%d agc=%d raw=%d",
             g_sys_param.rec_use_afe, g_sys_param.rec_agc_mode, g_sys_param.rec_raw_mode);
    return ESP_OK;
}
