/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once
#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_wifi_init();

char *app_wifi_get_prov_payload(void);

bool app_wifi_is_connected(void);

esp_err_t app_wifi_start(void);

esp_err_t app_wifi_get_wifi_ssid(char *ssid, size_t len);

esp_err_t app_wifi_prov_start(void);

esp_err_t app_wifi_prov_stop(void);

/* Manual UI control: disable auto-reconnect so scans can run */
void app_wifi_set_manual_mode(bool enable);

/* Wi-Fi credential vault on SD: save and auto-connect */
esp_err_t wifi_vault_save(const char *ssid, const char *password);
esp_err_t wifi_vault_try_auto_connect(void);
esp_err_t wifi_vault_list_ssids(char ssids[][33], size_t max, size_t *out_count);
esp_err_t wifi_vault_forget(const char *ssid);
esp_err_t wifi_vault_forget_all(void);
esp_err_t wifi_vault_clear(void);

/* Async helpers to avoid blocking UI thread */
esp_err_t app_wifi_connect_async(const char *ssid, const char *password);
void app_wifi_auto_connect_async(void);

#ifdef __cplusplus
}
#endif
