/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include "esp_bt.h"
#include <esp_netif.h>
#include <qrcode.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <mbedtls/aes.h>
#include <esp_system.h>
#include <esp_random.h>
#include <unistd.h>
#include <errno.h>

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>

#include "app_wifi.h"
#include "app_sntp.h"
#include "ui_main.h"
#include "ui_net_config.h"

static const int WIFI_STA_CONNECT_OK    = BIT0;
static const int WIFI_PROV_EVENT_START  = BIT1;
static const int WIFI_PROV_EVENT_STOP   = BIT2;
static const int WIFI_PROV_EVENT_EXIST  = BIT3;
static const int WIFI_PROV_EVENT_STOPED = BIT4;

static bool s_connected = false;
static char s_payload[150] = "";
static const char *TAG = "app_wifi";
static EventGroupHandle_t wifi_event_group;
static bool s_manual_mode = false; /* when true, don't auto-reconnect; allow scan */
/* ===== Simple SD-card vault: AES-256-GCM with key kept in NVS ===== */
#define VAULT_PATH "/sdcard/wifi_vault.bin"
#define VAULT_NS   "wifi_vault"
#define VAULT_KEY  "k"

typedef struct {
    uint8_t tag[16];
    uint8_t iv[12];
    uint32_t len;
    /* ciphertext follows (len bytes) */
} vault_blob_hdr_t;

static esp_err_t vault_get_key(uint8_t key[32])
{
    nvs_handle h;
    size_t len = 32;
    if (nvs_open(VAULT_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, VAULT_KEY, key, &len) == ESP_OK && len == 32) {
            nvs_close(h);
            return ESP_OK;
        }
        nvs_close(h);
    }
    if (nvs_open(VAULT_NS, NVS_READWRITE, &h) != ESP_OK) return ESP_FAIL;
    esp_fill_random(key, 32);
    esp_err_t err = nvs_set_blob(h, VAULT_KEY, key, 32);
    err |= nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t vault_encrypt_and_append(const char *ssid, const char *pwd)
{
    uint8_t key[32];
    if (vault_get_key(key) != ESP_OK) return ESP_FAIL;
    size_t plain_len = strlen(ssid) + 1 + strlen(pwd) + 1; /* ssid\0pwd\0 */
    uint8_t *plain = malloc(plain_len);
    if (!plain) return ESP_ERR_NO_MEM;
    strcpy((char *)plain, ssid);
    strcpy((char *)plain + strlen(ssid) + 1, pwd);

    uint8_t iv[12];
    esp_fill_random(iv, sizeof(iv));

    uint8_t *out = malloc(sizeof(vault_blob_hdr_t) + plain_len);
    if (!out) { free(plain); return ESP_ERR_NO_MEM; }
    vault_blob_hdr_t *hdr = (vault_blob_hdr_t *)out;
    memcpy(hdr->iv, iv, sizeof(iv));
    hdr->len = plain_len;

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 256);
    size_t olen = 0;
    /* Use CTR to encrypt, compute tag as simple CMAC substitute with AES-CBC-MAC over ciphertext (lightweight) */
    /* NOTE: For production use, switch to full AES-GCM when available in your IDF build. */
    uint8_t stream_block[16] = {0};
    uint8_t nonce_counter[16] = {0};
    memcpy(nonce_counter, iv, 12);
    uint8_t *ciphertext = out + sizeof(vault_blob_hdr_t);
    mbedtls_aes_crypt_ctr(&aes, plain_len, &olen, nonce_counter, stream_block, plain, ciphertext);

    uint8_t mac[16] = {0};
    mbedtls_aes_setkey_enc(&aes, key, 256);
    uint8_t iv0[16] = {0};
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, plain_len, iv0, ciphertext, mac);
    memcpy(hdr->tag, mac, 16);
    mbedtls_aes_free(&aes);

    FILE *f = fopen(VAULT_PATH, "ab");
    if (!f) { free(out); free(plain); return ESP_FAIL; }
    fwrite(out, 1, sizeof(vault_blob_hdr_t) + plain_len, f);
    fflush(f);
    int fd = fileno(f);
    if (fd >= 0) { fsync(fd); }
    fclose(f);
    free(out);
    free(plain);
    return ESP_OK;
}

static esp_err_t vault_try_match_and_connect(void)
{
    FILE *f = fopen(VAULT_PATH, "rb");
    if (!f) return ESP_FAIL;
    uint8_t key[32];
    if (vault_get_key(key) != ESP_OK) { fclose(f); return ESP_FAIL; }
    while (1) {
        vault_blob_hdr_t hdr;
        if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) break;
        uint8_t *ciphertext = malloc(hdr.len);
        if (!ciphertext) { fclose(f); return ESP_ERR_NO_MEM; }
        if (fread(ciphertext, 1, hdr.len, f) != hdr.len) { free(ciphertext); break; }
        /* Decrypt */
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, key, 256);
        size_t olen = 0;
        uint8_t *plain = malloc(hdr.len);
        if (!plain) { mbedtls_aes_free(&aes); free(ciphertext); fclose(f); return ESP_ERR_NO_MEM; }
        uint8_t stream_block[16] = {0};
        uint8_t nonce_counter[16] = {0};
        memcpy(nonce_counter, hdr.iv, 12);
        mbedtls_aes_crypt_ctr(&aes, hdr.len, &olen, nonce_counter, stream_block, ciphertext, plain);
        mbedtls_aes_free(&aes);
        const char *ssid = (const char *)plain;
        const char *pwd = (const char *)plain + strlen((const char *)plain) + 1;
        if (strlen(ssid) > 0) {
            wifi_config_t cfg = {0};
            strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
            strncpy((char *)cfg.sta.password, pwd, sizeof(cfg.sta.password) - 1);
            cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_set_config(WIFI_IF_STA, &cfg);
            esp_wifi_set_ps(WIFI_PS_NONE);
            esp_wifi_disconnect();
            if (esp_wifi_connect() == ESP_OK) {
                free(plain); free(ciphertext); fclose(f);
                return ESP_OK;
            }
        }
        free(plain);
        free(ciphertext);
    }
    fclose(f);
    return ESP_FAIL;
}

esp_err_t wifi_vault_save(const char *ssid, const char *password)
{
    return vault_encrypt_and_append(ssid, password);
}

esp_err_t wifi_vault_try_auto_connect(void)
{
    return vault_try_match_and_connect();
}

static esp_err_t vault_list(char ssids[][33], size_t max, size_t *out_count)
{
    if (out_count) *out_count = 0;
    FILE *f = fopen(VAULT_PATH, "rb");
    if (!f) return ESP_FAIL;
    uint8_t key[32];
    if (vault_get_key(key) != ESP_OK) { fclose(f); return ESP_FAIL; }
    size_t count = 0;
    while (1) {
        vault_blob_hdr_t hdr;
        if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) break;
        uint8_t *ciphertext = malloc(hdr.len);
        if (!ciphertext) { fclose(f); return ESP_ERR_NO_MEM; }
        if (fread(ciphertext, 1, hdr.len, f) != hdr.len) { free(ciphertext); break; }
        mbedtls_aes_context aes; mbedtls_aes_init(&aes); mbedtls_aes_setkey_enc(&aes, key, 256);
        uint8_t *plain = malloc(hdr.len); if (!plain) { mbedtls_aes_free(&aes); free(ciphertext); fclose(f); return ESP_ERR_NO_MEM; }
        size_t olen = 0; uint8_t stream_block[16] = {0}; uint8_t nonce_counter[16] = {0}; memcpy(nonce_counter, hdr.iv, 12);
        mbedtls_aes_crypt_ctr(&aes, hdr.len, &olen, nonce_counter, stream_block, ciphertext, plain);
        mbedtls_aes_free(&aes);
        const char *ssid = (const char *)plain;
        if (ssid && ssid[0] != '\0' && count < max) {
            strncpy(ssids[count], ssid, 32); ssids[count][32] = '\0';
            count++;
        }
        free(plain); free(ciphertext);
    }
    fclose(f);
    if (out_count) *out_count = count;
    return count > 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_vault_list_ssids(char ssids[][33], size_t max, size_t *out_count)
{
    return vault_list(ssids, max, out_count);
}

esp_err_t wifi_vault_forget_all(void)
{
    /* Remove the vault file */
    if (unlink(VAULT_PATH) == 0) return ESP_OK;
    return ESP_FAIL;
}

esp_err_t wifi_vault_forget(const char *ssid)
{
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    FILE *f = fopen(VAULT_PATH, "rb");
    if (!f) return ESP_FAIL;
    uint8_t key[32];
    if (vault_get_key(key) != ESP_OK) { fclose(f); return ESP_FAIL; }
    /* Rebuild file excluding matching SSID */
    FILE *tmp = fopen(VAULT_PATH ".tmp", "wb");
    if (!tmp) { fclose(f); return ESP_FAIL; }
    while (1) {
        vault_blob_hdr_t hdr;
        if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) break;
        uint8_t *ciphertext = malloc(hdr.len);
        if (!ciphertext) { fclose(f); fclose(tmp); unlink(VAULT_PATH ".tmp"); return ESP_ERR_NO_MEM; }
        if (fread(ciphertext, 1, hdr.len, f) != hdr.len) { free(ciphertext); break; }
        mbedtls_aes_context aes; mbedtls_aes_init(&aes); mbedtls_aes_setkey_enc(&aes, key, 256);
        uint8_t *plain = malloc(hdr.len); if (!plain) { mbedtls_aes_free(&aes); free(ciphertext); fclose(f); fclose(tmp); unlink(VAULT_PATH ".tmp"); return ESP_ERR_NO_MEM; }
        size_t olen = 0; uint8_t stream_block[16] = {0}; uint8_t nonce_counter[16] = {0}; memcpy(nonce_counter, hdr.iv, 12);
        mbedtls_aes_crypt_ctr(&aes, hdr.len, &olen, nonce_counter, stream_block, ciphertext, plain);
        mbedtls_aes_free(&aes);
        const char *file_ssid = (const char *)plain;
        bool match = (strncmp(file_ssid, ssid, 32) == 0);
        if (!match) {
            fwrite(&hdr, 1, sizeof(hdr), tmp);
            fwrite(ciphertext, 1, hdr.len, tmp);
        }
        free(plain); free(ciphertext);
    }
    fclose(f); fclose(tmp);
    /* Replace original */
    unlink(VAULT_PATH);
    if (rename(VAULT_PATH ".tmp", VAULT_PATH) != 0) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t wifi_vault_clear(void)
{
    /* Remove the vault file from SD card */
    int rc = remove(VAULT_PATH);
    return (rc == 0 || errno == ENOENT) ? ESP_OK : ESP_FAIL;
}

#define PROV_QR_VERSION         "v1"
#define PROV_TRANSPORT_BLE      "ble"
#define QRCODE_BASE_URL         "https://rainmaker.espressif.com/qrcode.html"

#define CREDENTIALS_NAMESPACE   "rmaker_creds"
#define RANDOM_NVS_KEY          "random"


#if CONFIG_BSP_BOARD_ESP32_S3_BOX_3
/*set the ssid and password via "idf.py menuconfig"*/
#define DEFAULT_LISTEN_INTERVAL CONFIG_EXAMPLE_WIFI_LISTEN_INTERVAL
#define DEFAULT_BEACON_TIMEOUT  CONFIG_EXAMPLE_WIFI_BEACON_TIMEOUT
#endif

#if CONFIG_EXAMPLE_POWER_SAVE_MIN_MODEM
#define DEFAULT_PS_MODE WIFI_PS_MIN_MODEM
#elif CONFIG_EXAMPLE_POWER_SAVE_MAX_MODEM
#define DEFAULT_PS_MODE WIFI_PS_MAX_MODEM
#elif CONFIG_EXAMPLE_POWER_SAVE_NONE
#define DEFAULT_PS_MODE WIFI_PS_NONE
#else
#define DEFAULT_PS_MODE WIFI_PS_NONE
#endif /*CONFIG_POWER_SAVE_MODEM*/

static void app_wifi_print_qr(const char *name)
{
    if (!name) {
        ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
        return;
    }
    snprintf(s_payload, sizeof(s_payload), "{\"ver\":\"%s\",\"name\":\"%s\",\"pop\":\"\",\"transport\":\"%s\"}", PROV_QR_VERSION, name, PROV_TRANSPORT_BLE);
    /* Just highlight */
    // ESP_LOGW(TAG, "Scan this QR code from the ESP BOX app for Provisioning.");
    // qrcode_display(s_payload);
    // ESP_LOGW(TAG, "If QR code is not visible, copy paste the below URL in a browser.\n%s?data=%s", QRCODE_BASE_URL, s_payload);
}

char *app_wifi_get_prov_payload(void)
{
    return s_payload;
}

/* Event handler for catching system events */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            break;
        case WIFI_PROV_CRED_RECV: {
            wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "Received Wi-Fi credentials"
                     "\n\tSSID     : %s\n\tPassword : %s",
                     (const char *) wifi_sta_cfg->ssid,
                     (const char *) wifi_sta_cfg->password);
            break;
        }
        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                     "\n\tPlease reset to factory and retry provisioning",
                     (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                     "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
            esp_wifi_disconnect();
            wifi_prov_mgr_reset_sm_state_on_failure();
            ui_net_config_update_cb(UI_NET_EVT_PROV_CRED_FAIL, NULL);
            break;
        }
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            break;
        case WIFI_PROV_END:
            ESP_LOGI(TAG, "Provisioning end");
            /* De-initialize manager once provisioning is finished */
            wifi_prov_mgr_deinit();
            break;
        case WIFI_PROV_DEINIT:
            ESP_LOGI(TAG, "Provisioning deinit");
            xEventGroupSetBits(wifi_event_group, WIFI_PROV_EVENT_STOPED);
            ESP_ERROR_CHECK(esp_wifi_set_ps(DEFAULT_PS_MODE));
            break;
        default:
            break;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (!s_manual_mode) {
            ui_net_config_update_cb(UI_NET_EVT_START_CONNECT, NULL);
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "STA_START in manual mode; skipping auto connect");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_WIFI_READY) {
        ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "STA_CONNECTED: forcing PS NONE");
        esp_wifi_set_ps(WIFI_PS_NONE);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = 1;
        ui_acquire();
        ui_main_status_bar_set_wifi(s_connected);
        ui_release();
        /* Signal main application to continue execution */
        xEventGroupSetBits(wifi_event_group, WIFI_STA_CONNECT_OK);
        /* Ensure SNTP is started so time sync occurs on every connection */
        app_sntp_init();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGI(TAG, "Disconnected: reason=%d. %s", disc ? disc->reason : -1, s_manual_mode ? "manual-mode (no auto-reconnect)" : "auto-reconnecting");
        if (!s_manual_mode) {
            esp_wifi_connect();
        }
        s_connected = 0;
        ui_acquire();
        ui_main_status_bar_set_wifi(s_connected);
        ui_release();
        ui_net_config_update_cb(UI_NET_EVT_START_CONNECT, NULL);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        /* Notify UI that scan completed; UI will fetch results */
        ui_net_config_update_cb(UI_NET_EVT_START, NULL);
    }
}

static void wifi_init_sta()
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    /* Keep Wi-Fi always on for stable connectivity */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* Free random_bytes after use only if function returns ESP_OK */
static esp_err_t read_random_bytes_from_nvs(uint8_t **random_bytes, size_t *len)
{
    nvs_handle handle;
    esp_err_t err;
    *len = 0;

    if ((err = nvs_open_from_partition(CONFIG_ESP_RMAKER_FACTORY_PARTITION_NAME, CREDENTIALS_NAMESPACE,
                                       NVS_READONLY, &handle)) != ESP_OK) {
        ESP_LOGD(TAG, "NVS open for %s %s %s failed with error %d", CONFIG_ESP_RMAKER_FACTORY_PARTITION_NAME, CREDENTIALS_NAMESPACE, RANDOM_NVS_KEY, err);
        return ESP_FAIL;
    }

    if ((err = nvs_get_blob(handle, RANDOM_NVS_KEY, NULL, len)) != ESP_OK) {
        ESP_LOGD(TAG, "Error %d. Failed to read key %s.", err, RANDOM_NVS_KEY);
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }

    *random_bytes = calloc(*len, 1);
    if (*random_bytes) {
        nvs_get_blob(handle, RANDOM_NVS_KEY, *random_bytes, len);
        nvs_close(handle);
        return ESP_OK;
    }
    nvs_close(handle);
    return ESP_ERR_NO_MEM;
}

static esp_err_t get_device_service_name(char *service_name, size_t max)
{
    uint8_t *nvs_random = NULL;
    const char *ssid_prefix = "BOX_";
    size_t nvs_random_size = 0;
    if ((read_random_bytes_from_nvs(&nvs_random, &nvs_random_size) != ESP_OK) || nvs_random_size < 3) {
        uint8_t eth_mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
        snprintf(service_name, max, "%s%02x%02x%02x", ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
    } else {
        snprintf(service_name, max, "%s%02x%02x%02x", ssid_prefix, nvs_random[nvs_random_size - 3],
                 nvs_random[nvs_random_size - 2], nvs_random[nvs_random_size - 1]);
    }

    if (nvs_random) {
        free(nvs_random);
    }
    return ESP_OK;
}

void app_wifi_init(void)
{
    esp_netif_init();

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

#if CONFIG_BSP_BOARD_ESP32_S3_BOX_3
    wifi_config_t wifi_cfg;
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg));
    /* Avoid long listen interval to prevent missed beacons */
    wifi_cfg.sta.listen_interval = 0;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Do not extend inactive time; keep association tight */
    // ESP_ERROR_CHECK(esp_wifi_set_inactive_time(WIFI_IF_STA, DEFAULT_BEACON_TIMEOUT));
#endif
}

esp_err_t app_wifi_prov_start(void)
{
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
    if (!provisioned) {
        ESP_LOGI(TAG, "app_wifi_prov_start");
        return xEventGroupSetBits(wifi_event_group, WIFI_PROV_EVENT_START);
    } else {
        return ESP_FAIL;
    }
}

esp_err_t app_wifi_prov_stop(void)
{
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
    if (!provisioned) {
        ESP_LOGI(TAG, "app_wifi_prov_stop");
        xEventGroupSetBits(wifi_event_group, WIFI_PROV_EVENT_STOP);
        xEventGroupClearBits(wifi_event_group, WIFI_PROV_EVENT_STOPED);
        xEventGroupWaitBits(wifi_event_group, WIFI_PROV_EVENT_STOPED, pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}

esp_err_t app_wifi_start(void)
{
    esp_err_t err;

    ui_net_config_update_cb(UI_NET_EVT_START, NULL);

    xEventGroupClearBits(wifi_event_group, WIFI_STA_CONNECT_OK);
    xEventGroupClearBits(wifi_event_group, WIFI_PROV_EVENT_START);
    xEventGroupClearBits(wifi_event_group, WIFI_PROV_EVENT_STOP);
    xEventGroupClearBits(wifi_event_group, WIFI_PROV_EVENT_EXIST);

    /* If device is not yet provisioned start provisioning service */
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
    if (!provisioned) {
        while (1) {
            ESP_LOGI(TAG, "waiting provisioning");
            xEventGroupWaitBits(wifi_event_group, WIFI_PROV_EVENT_START, pdFALSE, pdFALSE, portMAX_DELAY);
            xEventGroupClearBits(wifi_event_group, WIFI_PROV_EVENT_START);

            /* Provisioning framework initialization */
            wifi_prov_mgr_config_t config = {
                .scheme = wifi_prov_scheme_ble,
                .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BT,
            };
            ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

            ui_net_config_update_cb(UI_NET_EVT_START_PROV, NULL);
            /* Keep PS disabled during provisioning as well */
            err = esp_wifi_set_ps(WIFI_PS_NONE);
            if (err != ESP_OK) {
                ui_net_config_update_cb(UI_NET_EVT_PROV_SET_PS_FAIL, NULL);
                continue;
            };

            /* Get bluetooth broadcast name */
            char service_name[12];
            err = get_device_service_name(service_name, sizeof(service_name));
            if (err != ESP_OK) {
                ui_net_config_update_cb(UI_NET_EVT_PROV_GET_NAME_FAIL, NULL);
                continue;
            };

            uint8_t mfg[] = { 0xe5, 0x02, 'N', 'o', 'v', 'a', 0x00, 0x02, 0x00, 0xF0, 0x01, 0x00 };
            err = wifi_prov_scheme_ble_set_mfg_data(mfg, sizeof(mfg));
            if (err != ESP_OK) {
                ui_net_config_update_cb(UI_NET_EVT_PROV_SET_MFG_FAIL, NULL);
                continue;
            }

            /* Start provisioning */
            err = wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, NULL, service_name, NULL);
            if (err != ESP_OK) {
                ui_net_config_update_cb(UI_NET_EVT_PROV_START_FAIL, NULL);
                continue;
            }

            app_wifi_print_qr(service_name);
            ui_net_config_update_cb(UI_NET_EVT_GET_NAME, NULL);
            ESP_LOGI(TAG, "Provisioning Started. Name : %s", service_name);

            xEventGroupWaitBits(wifi_event_group, WIFI_STA_CONNECT_OK | WIFI_PROV_EVENT_STOP, pdFALSE, pdFALSE, portMAX_DELAY);

            if (WIFI_STA_CONNECT_OK & xEventGroupGetBits(wifi_event_group) ) {
                ESP_LOGI(TAG, "Wi-Fi Provisioned OK, stoped:%d", WIFI_PROV_EVENT_STOPED & xEventGroupGetBits(wifi_event_group));
                xEventGroupWaitBits(wifi_event_group, WIFI_PROV_EVENT_STOPED, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
                esp_bt_mem_release(ESP_BT_MODE_BTDM);
                ESP_LOGI(TAG, "BLE memory released");
                break;
            } else if (WIFI_PROV_EVENT_STOP & xEventGroupGetBits(wifi_event_group) ) {
                ESP_LOGI(TAG, "Wi-Fi Provisioned Stop");
                wifi_prov_mgr_stop_provisioning();
                xEventGroupClearBits(wifi_event_group, WIFI_PROV_EVENT_STOP);
                continue;
            }
        }
    } else {
        ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");
        wifi_init_sta();
        xEventGroupSetBits(wifi_event_group, WIFI_PROV_EVENT_EXIST);
    }

    xEventGroupWaitBits(wifi_event_group, WIFI_STA_CONNECT_OK, pdFALSE, pdFALSE, pdMS_TO_TICKS(1000 * 80));
    if ( 0 == (WIFI_STA_CONNECT_OK & xEventGroupGetBits(wifi_event_group)) ) {
        if (WIFI_PROV_EVENT_EXIST & xEventGroupGetBits(wifi_event_group) ) {
            ESP_LOGI(TAG, "Wi-Fi Connect Failed");
            esp_wifi_disconnect();
            ui_net_config_update_cb(UI_NET_EVT_CONNECT_FAILED, NULL);
        }
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "Wi-Fi Connect");
        ui_net_config_update_cb(UI_NET_EVT_WIFI_CONNECTED, NULL);
        app_sntp_init();

        printf("Current Free Memory\t%d\t\t%d\n",
               heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
               heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        printf("Largest Free Block\t%d\t\t%d\n",
               heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
               heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        return ESP_OK;
    }
}

bool app_wifi_is_connected(void)
{
    return s_connected;
}

void app_wifi_set_manual_mode(bool enable)
{
    s_manual_mode = enable;
}

typedef struct {
    char ssid[33];
    char pwd[65];
} connect_job_t;

static void connect_task(void *arg)
{
    connect_job_t job = {0};
    if (arg) memcpy(&job, arg, sizeof(job));
    free(arg);

    /* Enter manual mode to avoid auto-reconnect racing with config */
    app_wifi_set_manual_mode(true);

    /* Ensure we are not in connecting state */
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(150));

    /* Retry set_config until station not busy */
    wifi_config_t cfg = (wifi_config_t){0};
    strncpy((char *)cfg.sta.ssid, job.ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, job.pwd, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_mode(WIFI_MODE_STA);

    for (int i = 0; i < 20; i++) {
        esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &cfg);
        if (e == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_wifi_set_ps(WIFI_PS_NONE);
    (void)esp_wifi_connect();

    /* Leave manual mode; allow normal reconnects after we initiate connect */
    app_wifi_set_manual_mode(false);

    vTaskDelete(NULL);
}

esp_err_t app_wifi_connect_async(const char *ssid, const char *password)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;
    connect_job_t *job = calloc(1, sizeof(connect_job_t));
    if (!job) return ESP_ERR_NO_MEM;
    strncpy(job->ssid, ssid, sizeof(job->ssid) - 1);
    if (password) strncpy(job->pwd, password, sizeof(job->pwd) - 1);
    BaseType_t ok = xTaskCreate(connect_task, "wifi_conn", 4096, job, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

static void auto_connect_task(void *arg)
{
    (void)arg;
    wifi_vault_try_auto_connect();
    vTaskDelete(NULL);
}

void app_wifi_auto_connect_async(void)
{
    xTaskCreate(auto_connect_task, "wifi_auto", 4096, NULL, 5, NULL);
}

esp_err_t app_wifi_get_wifi_ssid(char *ssid, size_t len)
{
    wifi_config_t wifi_cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK) {
        return ESP_FAIL;
    }
    strncpy(ssid, (const char *)wifi_cfg.sta.ssid, len);
    return ESP_OK;
}
