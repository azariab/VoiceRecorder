/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

#include "app_sr.h"

typedef struct {
    bool need_hint;
    sr_language_t sr_lang;
    uint8_t volume; // 0 - 100%
    bool radar_en;
    // App config loaded from JSON (provisioning)
    bool rec_use_afe;     // default false
    uint8_t rec_agc_mode; // 0/1/2
    uint8_t rec_raw_mode; // 0=ST,1=L,2=R,3=M
} sys_param_t;

esp_err_t settings_read_parameter_from_nvs(void);
esp_err_t settings_write_parameter_to_nvs(void);
sys_param_t *settings_get_parameter(void);
// Load app config from /spiffs/config.json (optional); keeps NVS for user prefs separate
esp_err_t settings_load_app_config(void);
