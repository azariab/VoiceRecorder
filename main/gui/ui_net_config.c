/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <wifi_provisioning/manager.h>
#include "esp_log.h"
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include "bsp_board.h"
#include "lvgl.h"
#include "app_wifi.h"
#include "app_rmaker.h"
#include "ui_main.h"
#include "ui_net_config.h"
#include <freertos/task.h>

static const char *TAG = "ui_net_config";

static bool provide_no_err = true;
static lv_obj_t *g_btn_app_hint = NULL;
static lv_obj_t *g_hint_lab = NULL;
static lv_obj_t *g_qr = NULL;
static lv_obj_t *g_img = NULL;
static lv_obj_t *g_page = NULL;
static ui_net_state_t g_net_state = UI_NET_EVT_LOARDING;
static lv_obj_t *g_btn_return = NULL;

/* Manual Wi-Fi setup UI */
static lv_obj_t *g_manual_page = NULL;
static lv_obj_t *g_ssid_list = NULL;
static lv_obj_t *g_pass_ta = NULL;
static lv_obj_t *g_kb = NULL;
static lv_obj_t *g_connect_btn = NULL;
static char g_selected_ssid[33] = {0};
static wifi_auth_mode_t g_selected_auth = WIFI_AUTH_OPEN;
static bool s_wifi_inited = false;
static lv_timer_t *g_manual_conn_timer = NULL;
static uint32_t g_manual_start_tick = 0;
static bool s_ip_evt_registered = false;
static bool s_wifi_evt_registered = false;
static bool g_pending_save = false;
static char g_last_pwd[65] = {0};
static char g_pending_ssid[33] = {0};
static bool g_manual_connecting = false;
/* Saved networks UI */
static lv_obj_t *g_saved_list = NULL;
/* Config details page */
static lv_obj_t *g_cfg_page = NULL;
static lv_obj_t *g_cfg_label = NULL;

/* Forward declarations */
static void show_conn_details(void);
static void ip_wifi_evt_cb(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void manual_list_item_click_cb(lv_event_t *e);
static void view_config_btn_cb(lv_event_t *e);
static void refresh_saved_list(void);
static void forget_btn_cb(lv_event_t *e);

static void manual_conn_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (app_wifi_is_connected()) {
        if (g_manual_conn_timer) {
            lv_timer_del(g_manual_conn_timer);
            g_manual_conn_timer = NULL;
        }
        show_conn_details();
        g_manual_connecting = false;
        if (g_connect_btn) lv_obj_clear_state(g_connect_btn, LV_STATE_DISABLED);
        return;
    }
    if (lv_tick_elaps(g_manual_start_tick) >= 15000) {
        if (g_manual_conn_timer) {
            lv_timer_del(g_manual_conn_timer);
            g_manual_conn_timer = NULL;
        }
        ui_net_config_update_cb(UI_NET_EVT_CONNECT_FAILED, NULL);
        g_manual_connecting = false;
        if (g_connect_btn) lv_obj_clear_state(g_connect_btn, LV_STATE_DISABLED);
    }
}

static const char *wifi_proto_str(const wifi_ap_record_t *ap)
{
    if (!ap) return "";
    if (ap->phy_11n) return "802.11n";
    if (ap->phy_11g) return "802.11g";
    if (ap->phy_11b) return "802.11b";
    return "802.11";
}

static lv_obj_t *g_manual_status = NULL;
static lv_obj_t *g_details_label = NULL;

static void show_conn_details(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        if (g_cfg_label) {
            lv_label_set_text(g_cfg_label, "Wi-Fi connected");
            lv_obj_align(g_cfg_label, LV_ALIGN_TOP_LEFT, 6, 50);
        }
        return;
    }

    esp_netif_ip_info_t ip = {0};
    esp_netif_dns_info_t dns = {0};
    esp_netif_get_ip_info(netif, &ip);
    esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);

    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);

    wifi_ap_record_t ap = {0};
    esp_wifi_sta_get_ap_info(&ap);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "SSID: %s\nBSSID: %02X:%02X:%02X:%02X:%02X:%02X\nIP: %d.%d.%d.%d  GW: %d.%d.%d.%d\nMAC: %02X:%02X:%02X:%02X:%02X:%02X\nDNS: %d.%d.%d.%d\nProto: %s",
             (const char *)ap.ssid,
             ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5],
             IP2STR(&ip.ip), IP2STR(&ip.gw),
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             IP2STR(&dns.ip.u_addr.ip4),
             wifi_proto_str(&ap));
    if (g_cfg_label) {
        lv_label_set_text(g_cfg_label, buf);
        lv_obj_align(g_cfg_label, LV_ALIGN_TOP_LEFT, 6, 50);
    }
}

static void (*g_net_config_end_cb)(void) = NULL;

#if !CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite
static void btn_return_down_cb(void *handle, void *arg);
#endif

static void ui_app_page_return_click_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_user_data(e);
    if (ui_get_btn_op_group()) {
        lv_group_focus_freeze(ui_get_btn_op_group(), false);
    }
#if !CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite
    bsp_btn_rm_all_callback(BSP_BUTTON_MAIN);
    bsp_btn_register_callback(BSP_BUTTON_MAIN, BUTTON_PRESS_UP, btn_return_down_cb, (void *)g_btn_return);
#endif
    lv_obj_del_async(obj);
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

static void ui_net_config_page_app_click_cb(lv_event_t *e)
{
    /* **************** FRAMWORK **************** */
    ESP_LOGI(TAG, "Network: app hint clicked (open QR)");
    lv_obj_t *page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page, lv_obj_get_width(lv_obj_get_parent(page)), 185);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(page, 15, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(page, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(page, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(page, LV_OPA_30, LV_PART_MAIN);
    lv_obj_align(page, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_return = lv_btn_create(page);
    lv_obj_set_size(btn_return, 24, 24);
    lv_obj_add_style(btn_return, &ui_button_styles()->style, 0);
    lv_obj_add_style(btn_return, &ui_button_styles()->style_pr, LV_STATE_PRESSED);
    lv_obj_add_style(btn_return, &ui_button_styles()->style_focus, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_return, &ui_button_styles()->style_focus, LV_STATE_FOCUSED);
    lv_obj_align(btn_return, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *lab_btn_text = lv_label_create(btn_return);
    lv_label_set_text_static(lab_btn_text, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lab_btn_text, lv_color_make(158, 158, 158), LV_STATE_DEFAULT);
    lv_obj_center(lab_btn_text);
    lv_obj_add_event_cb(btn_return, ui_app_page_return_click_cb, LV_EVENT_CLICKED, page);
#if !CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite
    bsp_btn_rm_event_callback(BSP_BUTTON_MAIN, BUTTON_PRESS_UP);
    bsp_btn_register_callback(BSP_BUTTON_MAIN, BUTTON_PRESS_UP, btn_return_down_cb, (void *)btn_return);
#endif
    if (ui_get_btn_op_group()) {
        lv_group_add_obj(ui_get_btn_op_group(), btn_return);
        lv_group_focus_obj(btn_return);
        lv_group_focus_freeze(ui_get_btn_op_group(), true);
    }

    /* **************** HINT MESSAGE **************** */
    lv_obj_t *hint_label = lv_label_create(page);
    lv_label_set_text_static(hint_label,
                             "Please scan the QR code below to\n"
                             "download the ESP-BOX APP.");
    lv_obj_align(hint_label, LV_ALIGN_TOP_MID, 10, 0);

    /* **************** QR CODE **************** */
    static const char *qr_payload = "https://espressif.com/esp-box";
    lv_obj_t *qr = lv_qrcode_create(page, 92, lv_color_black(), lv_color_white());
    lv_qrcode_update(qr, qr_payload, strlen(qr_payload));
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, 10);

    /* **************** LINK ADDR **************** */
    lv_obj_t *lab_link = lv_label_create(page);
    lv_label_set_text_static(lab_link, qr_payload);
    lv_obj_align(lab_link, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void ip_wifi_evt_cb(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Network: IP_EVENT_STA_GOT_IP");
        /* Ensure power save stays disabled after connect */
        esp_wifi_set_ps(WIFI_PS_NONE);
        ui_acquire();
        show_conn_details();
        ui_release();
        g_manual_connecting = false;
        if (g_pending_save) {
            if (g_pending_ssid[0] != '\0') {
                ESP_LOGI(TAG, "Network: saving creds for SSID='%s'", g_pending_ssid);
                if (wifi_vault_save(g_pending_ssid, g_last_pwd) == ESP_OK) {
                    /* Refresh saved list on success */
                    ui_acquire();
                    refresh_saved_list();
                    ui_release();
                }
            }
            g_pending_save = false;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);
        ESP_LOGI(TAG, "Network: SCAN_DONE, ap_num=%u", (unsigned)ap_num);
        /* Populate SSID list after async scan */
        if (!g_ssid_list || !g_manual_page) return;
        ui_acquire();
        /* Clear list */
        uint32_t cnt = lv_obj_get_child_cnt(g_ssid_list);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_obj_t *child = lv_obj_get_child(g_ssid_list, 0);
            lv_obj_del(child);
        }
        if (ap_num == 0) {
            lv_list_add_text(g_ssid_list, "No networks found");
            ui_release();
            /* Allow reconnects again */
            app_wifi_set_manual_mode(false);
            return;
        }
        wifi_ap_record_t *aps = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_num);
        if (!aps) { ui_release(); app_wifi_set_manual_mode(false); return; }
        if (esp_wifi_scan_get_ap_records(&ap_num, aps) != ESP_OK) {
            free(aps);
            ui_release();
            app_wifi_set_manual_mode(false);
            return;
        }
        for (int i = 0; i < ap_num; i++) {
            const wifi_ap_record_t *rec = &aps[i];
            lv_obj_t *btn = lv_list_add_btn(g_ssid_list, NULL, (const char *)rec->ssid);
            lv_obj_add_event_cb(btn, manual_list_item_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)rec->authmode);
        }
        free(aps);
        ui_release();
        /* Allow reconnects again */
        app_wifi_set_manual_mode(false);
    }
}

static void auto_connect_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Network: Auto Connect pressed");
    if (!s_wifi_inited) {
        ESP_LOGI(TAG, "Network: initializing Wi‑Fi for auto-connect");
        app_wifi_init();
        s_wifi_inited = true;
    }
    if (!s_ip_evt_registered) {
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_wifi_evt_cb, NULL, NULL);
        s_ip_evt_registered = true;
    }
    if (!s_wifi_evt_registered) {
        esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, ip_wifi_evt_cb, NULL, NULL);
        s_wifi_evt_registered = true;
    }
    app_wifi_auto_connect_async();
    lv_label_set_text(g_hint_lab, "Connecting using saved...");
}

static void saved_forget_all_cb(lv_event_t *e)
{
    (void)e;
    wifi_vault_forget_all();
    if (g_saved_list) {
        uint32_t cnt = lv_obj_get_child_cnt(g_saved_list);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_obj_t *child = lv_obj_get_child(g_saved_list, 0);
            lv_obj_del(child);
        }
        lv_list_add_text(g_saved_list, "No saved networks");
    }
}

static void saved_forget_one_cb(lv_event_t *e)
{
    const char *ssid = (const char *)lv_event_get_user_data(e);
    if (!ssid) return;
    wifi_vault_forget(ssid);
    /* Refresh list */
    if (g_saved_list) {
        uint32_t cnt = lv_obj_get_child_cnt(g_saved_list);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_obj_t *child = lv_obj_get_child(g_saved_list, 0);
            lv_obj_del(child);
        }
        char ssids[16][33]; size_t n = 0;
        if (wifi_vault_list_ssids(ssids, 16, &n) == ESP_OK && n > 0) {
            for (size_t i = 0; i < n; i++) {
                lv_obj_t *row = lv_list_add_btn(g_saved_list, NULL, ssids[i]);
                char *copy = malloc(strlen(ssids[i]) + 1);
                if (copy) { strcpy(copy, ssids[i]); lv_obj_add_event_cb(row, saved_forget_one_cb, LV_EVENT_CLICKED, copy); }
            }
        } else {
            lv_list_add_text(g_saved_list, "No saved networks");
        }
    }
}

static void refresh_saved_list(void)
{
    if (!g_saved_list) return;
    uint32_t cnt = lv_obj_get_child_cnt(g_saved_list);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(g_saved_list, 0);
        lv_obj_del(child);
    }
    char ssids[16][33]; size_t n = 0;
    if (wifi_vault_list_ssids(ssids, 16, &n) == ESP_OK && n > 0) {
        ESP_LOGI(TAG, "Network: saved networks loaded: %u", (unsigned)n);
        for (size_t i = 0; i < n; i++) {
            lv_obj_t *row = lv_list_add_btn(g_saved_list, NULL, ssids[i]);
            char *copy = malloc(strlen(ssids[i]) + 1);
            if (copy) { strcpy(copy, ssids[i]); lv_obj_add_event_cb(row, saved_forget_one_cb, LV_EVENT_CLICKED, copy); }
        }
    } else {
        ESP_LOGI(TAG, "Network: saved networks loaded: 0");
        lv_list_add_text(g_saved_list, "No saved networks");
    }
}

static void manual_page_close(lv_event_t *e)
{
    ESP_LOGI(TAG, "Network: closing Manual Setup page");
    if (g_manual_page) {
        if (g_manual_conn_timer) {
            lv_timer_del(g_manual_conn_timer);
            g_manual_conn_timer = NULL;
        }
        /* Null references before async delete to avoid updates on freed objects */
        lv_obj_t *to_del = g_manual_page;
        g_manual_page = NULL;
        g_ssid_list = NULL;
        g_pass_ta = NULL;
        g_kb = NULL;
        g_connect_btn = NULL;
        g_manual_status = NULL;
        g_manual_connecting = false;
        /* Leave manual mode when closing */
        app_wifi_set_manual_mode(false);
        /* Keep g_pending_ssid intact so save after IP still works */
        g_selected_ssid[0] = '\0';
        g_selected_auth = WIFI_AUTH_OPEN;
        lv_obj_del_async(to_del);
        /* Update saved list if visible */
        if (g_saved_list) { ui_acquire(); refresh_saved_list(); ui_release(); }
    }
}

static void manual_list_item_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    /* First child of list button is its label */
    lv_obj_t *lab = lv_obj_get_child(btn, 0);
    const char *ssid = lab ? lv_label_get_text(lab) : NULL;
    if (!ssid) return;
    ESP_LOGI(TAG, "Network: SSID selected '%s'", ssid);
    strncpy(g_selected_ssid, ssid, sizeof(g_selected_ssid) - 1);
    g_selected_ssid[sizeof(g_selected_ssid) - 1] = '\0';
    /* Derive auth mode from event user data */
    g_selected_auth = (wifi_auth_mode_t)(uintptr_t)lv_event_get_user_data(e);
    /* If open network, hide password, else show */
    if (g_pass_ta) {
        if (g_selected_auth == WIFI_AUTH_OPEN) {
            lv_textarea_set_text(g_pass_ta, "");
            lv_obj_add_flag(g_pass_ta, LV_OBJ_FLAG_HIDDEN);
            if (g_kb) lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(g_pass_ta, LV_OBJ_FLAG_HIDDEN);
            if (g_kb) {
                lv_keyboard_set_textarea(g_kb, g_pass_ta);
                lv_obj_clear_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

static void manual_scan_populate(void)
{
    if (!g_ssid_list) return;
    ESP_LOGI(TAG, "Network: starting async scan");
    /* Enter manual mode to stop auto reconnect during scans */
    app_wifi_set_manual_mode(true);
    /* Clear previous items */
    uint32_t cnt = lv_obj_get_child_cnt(g_ssid_list);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(g_ssid_list, 0);
        lv_obj_del(child);
    }

    wifi_scan_config_t scan_cfg = (wifi_scan_config_t){ 0 };
    scan_cfg.show_hidden = true;
    esp_wifi_scan_stop();
    /* start non-blocking scan; handle results in WIFI_EVENT_SCAN_DONE */
    esp_wifi_scan_start(&scan_cfg, false);
    lv_list_add_text(g_ssid_list, "Scanning...");
}

static void manual_scan_btn_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Network: Scan button pressed");
    manual_scan_populate();
}

static void manual_connect_btn_cb(lv_event_t *e)
{
    if (g_selected_ssid[0] == '\0') {
        lv_label_set_text(g_hint_lab, "Select a network first");
        lv_obj_align(g_hint_lab, LV_ALIGN_CENTER, 0, 0);
        return;
    }
    if (g_manual_connecting) {
        return;
    }
    const char *pwd = g_pass_ta && !lv_obj_has_flag(g_pass_ta, LV_OBJ_FLAG_HIDDEN) ? lv_textarea_get_text(g_pass_ta) : "";
    strncpy(g_last_pwd, pwd ? pwd : "", sizeof(g_last_pwd) - 1);
    strncpy(g_pending_ssid, g_selected_ssid, sizeof(g_pending_ssid) - 1);
    ESP_LOGI(TAG, "Network: Connect pressed ssid='%s' len(pwd)=%d", g_selected_ssid, (int)strlen(g_last_pwd));
    ui_net_config_update_cb(UI_NET_EVT_START_CONNECT, NULL);
    /* Leave manual mode and connect asynchronously */
    app_wifi_set_manual_mode(false);
    g_pending_save = true;
    g_manual_connecting = true;
    if (g_connect_btn) lv_obj_add_state(g_connect_btn, LV_STATE_DISABLED);
    app_wifi_connect_async(g_selected_ssid, g_last_pwd);
    /* Start a short polling timer to report success/failure */
    if (g_manual_conn_timer) {
        lv_timer_del(g_manual_conn_timer);
        g_manual_conn_timer = NULL;
    }
    g_manual_start_tick = lv_tick_get();
    g_manual_conn_timer = lv_timer_create(manual_conn_timer_cb, 200, NULL);
}

static void manual_setup_prep_task(void *arg)
{
    ESP_LOGI(TAG, "Network: manual setup prep task start");
    /* Prevent unintended auto-connects while bringing up Wi‑Fi */
    app_wifi_set_manual_mode(true);
    if (!s_wifi_inited) {
        app_wifi_init();
        s_wifi_inited = true;
    }
    if (!s_ip_evt_registered) {
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_wifi_evt_cb, NULL, NULL);
        s_ip_evt_registered = true;
    }
    if (!s_wifi_evt_registered) {
        esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, ip_wifi_evt_cb, NULL, NULL);
        s_wifi_evt_registered = true;
    }
    ESP_LOGI(TAG, "Network: manual setup prep done");
    vTaskDelete(NULL);
}

static void ui_net_config_page_manual_click_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Network: Manual Setup pressed");
    /* Kick a background task to prep Wi‑Fi/event handlers to avoid UI stall */
    xTaskCreate(manual_setup_prep_task, "wifi_prep", 4096, NULL, 5, NULL);
    /* Create manual setup page */
    g_manual_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_manual_page, 300, 210);
    lv_obj_clear_flag(g_manual_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(g_manual_page, 15, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_manual_page, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(g_manual_page, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(g_manual_page, LV_OPA_30, LV_PART_MAIN);
    lv_obj_align(g_manual_page, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_close = lv_btn_create(g_manual_page);
    lv_obj_set_size(btn_close, 24, 24);
    lv_obj_align(btn_close, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *lab_x = lv_label_create(btn_close);
    lv_label_set_text_static(lab_x, LV_SYMBOL_LEFT);
    lv_obj_center(lab_x);
    lv_obj_add_event_cb(btn_close, manual_page_close, LV_EVENT_CLICKED, NULL);

    /* SSID list and Scan button */
    g_ssid_list = lv_list_create(g_manual_page);
    lv_obj_set_size(g_ssid_list, 180, 150);
    lv_obj_align(g_ssid_list, LV_ALIGN_TOP_LEFT, 5, 30);

    lv_obj_t *btn_scan = lv_btn_create(g_manual_page);
    lv_obj_set_size(btn_scan, 90, 24);
    lv_obj_align(btn_scan, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_t *lab_scan = lv_label_create(btn_scan);
    lv_label_set_text_static(lab_scan, "Scan");
    lv_obj_center(lab_scan);
    lv_obj_add_event_cb(btn_scan, manual_scan_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Password field */
    g_pass_ta = lv_textarea_create(g_manual_page);
    lv_obj_set_width(g_pass_ta, 110);
    lv_textarea_set_placeholder_text(g_pass_ta, "Password");
    lv_textarea_set_password_mode(g_pass_ta, true);
    lv_obj_align(g_pass_ta, LV_ALIGN_TOP_RIGHT, -5, 70);

    /* Keyboard */
    g_kb = lv_keyboard_create(g_manual_page);
    lv_obj_set_size(g_kb, 290, 80);
    lv_obj_align(g_kb, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_keyboard_set_textarea(g_kb, g_pass_ta);

    /* Connect button */
    g_connect_btn = lv_btn_create(g_manual_page);
    lv_obj_set_size(g_connect_btn, 90, 24);
    lv_obj_align(g_connect_btn, LV_ALIGN_TOP_RIGHT, -5, 35);
    lv_obj_t *lab_conn = lv_label_create(g_connect_btn);
    lv_label_set_text_static(lab_conn, "Connect");
    lv_obj_center(lab_conn);
    lv_obj_add_event_cb(g_connect_btn, manual_connect_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Initial scan */
    manual_scan_populate();

    /* Status label (dedicated, so it doesn't clash with global hint) */
    g_manual_status = lv_label_create(g_manual_page);
    lv_label_set_text_static(g_manual_status, "");
    lv_obj_align(g_manual_status, LV_ALIGN_BOTTOM_MID, 0, -90);
}

static void ui_net_config_page_return_click_cb(lv_event_t *e)
{
    if (false == provide_no_err) {
        return;
    }

    lv_obj_t *obj = lv_event_get_user_data(e);
    if (g_btn_app_hint) {
        lv_obj_del_async(g_btn_app_hint);
        g_btn_app_hint = NULL;
    }
    if (ui_get_btn_op_group()) {
        lv_group_remove_all_objs(ui_get_btn_op_group());
    }
#if !CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite
    bsp_btn_rm_all_callback(BSP_BUTTON_MAIN);
#endif
    lv_obj_del_async(obj);
    g_page = NULL;
    g_qr = NULL;
    g_img = NULL;
    if (g_net_config_end_cb) {
        g_net_config_end_cb();
    }
}

void ui_net_config_update_cb(ui_net_state_t state, void *args)
{
    if ((UI_NET_EVT_WIFI_CONNECTED == state) && (UI_NET_EVT_CLOUD_CONNECTED == g_net_state)) {
        return;
    }

    g_net_state = state;
    if (!g_page) {
        return;
    }
    ui_acquire();
    switch (state) {
    case UI_NET_EVT_PROV_SET_PS_FAIL:
        provide_no_err = false;
        lv_label_set_text(g_hint_lab, "UI_NET_EVT_PROV_SET_PS_FAIL");
        lv_label_set_text(g_hint_lab,
                          "1. Set ps mode failed\n"
                          "#FF0000 2. Please reset the device#");
        lv_obj_align(g_hint_lab, LV_ALIGN_CENTER, 0, 0);
        break;
    case UI_NET_EVT_PROV_GET_NAME_FAIL:
        provide_no_err = false;
        lv_label_set_text(g_hint_lab,
                          "1. Get name failed\n"
                          "#FF0000 2. Please reset the device#");
        lv_obj_align(g_hint_lab, LV_ALIGN_CENTER, 0, 0);
        break;
    case UI_NET_EVT_PROV_SET_MFG_FAIL:
        provide_no_err = false;
        lv_label_set_text(g_hint_lab,
                          "1. Set mfg failed\n"
                          "#FF0000 2. Please reset the device#");
        lv_obj_align(g_hint_lab, LV_ALIGN_CENTER, 0, 0);
        break;
    case UI_NET_EVT_PROV_START_FAIL:
        provide_no_err = false;
        lv_label_set_text(g_hint_lab,
                          "1. Start failed\n"
                          "#FF0000 2. Please reset the device#");
        lv_obj_align(g_hint_lab, LV_ALIGN_CENTER, 0, 0);
        break;
    case UI_NET_EVT_PROV_CRED_FAIL://must reboot
        provide_no_err = false;
        lv_label_set_text(g_hint_lab,
                          "1. Authentication failed\n"
                          "#FF0000 2. Please reset the device#");
        lv_obj_align(g_hint_lab, LV_ALIGN_CENTER, 0, 0);
        break;
    case UI_NET_EVT_CONNECT_FAILED:
        provide_no_err = true;
        lv_label_set_text(g_hint_lab, "Connect failed");
        lv_obj_align(g_hint_lab, LV_ALIGN_CENTER, 0, 0);
        break;

    case UI_NET_EVT_LOARDING:
        lv_obj_clear_flag(g_hint_lab, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_hint_lab, "System is loading ...");
        lv_obj_align(g_hint_lab, LV_ALIGN_CENTER, 0, 0);
        break;
    case UI_NET_EVT_START:
        /* code */
        break;
    case UI_NET_EVT_START_PROV:
        /* code */
        break;
    case UI_NET_EVT_GET_NAME: {
        LV_IMG_DECLARE(esp_logo_tiny);
        const char *prov_msg = app_wifi_get_prov_payload();
        size_t prov_msg_len = strlen(prov_msg);
        g_qr = lv_qrcode_create(g_page, 108, lv_color_black(), lv_color_white());
        ESP_LOGI(TAG, "QR Data: %s", prov_msg);
        char *p = strstr(prov_msg, "\"name\":\"");
        if (p) {
            p += 8;
            char *p_end = strstr(p, "\"");
            if (p_end) {
                char name[32] = {0};
                strncpy(name, p, p_end - p);
                lv_obj_t *lab_name = lv_label_create(g_page);
                lv_label_set_text(lab_name, name);
                lv_obj_align_to(lab_name, g_page, LV_ALIGN_TOP_MID, 0, -8);
            }
        }
        lv_obj_align(g_qr, LV_ALIGN_TOP_MID, 0, 8);
        lv_obj_t *img = lv_img_create(g_qr);
        lv_img_set_src(img, &esp_logo_tiny);
        lv_obj_center(img);
        lv_qrcode_update(g_qr, prov_msg, prov_msg_len);
        lv_obj_clear_flag(g_hint_lab, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_hint_lab,
                          "1. Open ESP-BOX APP\n"
                          "2. Scan the QR Code to provision\n"
                          "#FF0000 3. Leave page will stop provision#");
        lv_obj_align_to(g_hint_lab, g_qr, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    }
    break;
    case UI_NET_EVT_START_CONNECT:
        lv_obj_clear_flag(g_hint_lab, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_hint_lab, "Connecting to Wi-Fi ...");
        lv_obj_align(g_hint_lab, LV_ALIGN_CENTER, 0, 0);
        break;
    case UI_NET_EVT_WIFI_CONNECTED: {
        lv_obj_clear_flag(g_hint_lab, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_hint_lab, "Connecting to Rainmaker ...");
        lv_obj_align(g_hint_lab, LV_ALIGN_CENTER, 0, 0);
    }
    break;
    case UI_NET_EVT_CLOUD_CONNECTED: {
        char ssid[64] = {0};
        app_wifi_get_wifi_ssid(ssid, sizeof(ssid));
        LV_IMG_DECLARE(icon_rmaker);
        g_img = lv_img_create(g_page);
        lv_img_set_src(g_img, &icon_rmaker);
        lv_obj_align(g_img, LV_ALIGN_CENTER, 0, -10);
        lv_obj_clear_flag(g_hint_lab, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(g_hint_lab, "Device already connected to cloud\n"
                              "Wi-Fi is connected to #000000 %s#", ssid);
        lv_obj_align_to(g_hint_lab, g_img, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    }
    break;
    default:
        break;
    }

    if ((UI_NET_EVT_CLOUD_CONNECTED != state) && g_img) {
        lv_obj_add_flag(g_img, LV_OBJ_FLAG_HIDDEN);
    }

    if ((UI_NET_EVT_GET_NAME != state) && g_qr) {
        lv_obj_add_flag(g_qr, LV_OBJ_FLAG_HIDDEN);
    }
    ui_release();
}

void ui_net_config_start(void (*fn)(void))
{
    ESP_LOGI(TAG, "Network: entering Network screen");
    g_net_config_end_cb = fn;

    g_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_page, 290, 190);
    lv_obj_clear_flag(g_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(g_page, 15, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_page, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(g_page, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(g_page, LV_OPA_30, LV_PART_MAIN);
    lv_obj_align(g_page, LV_ALIGN_TOP_MID, 0, 40);

    g_btn_return = lv_btn_create(g_page);
    lv_obj_set_size(g_btn_return, 24, 24);
    lv_obj_add_style(g_btn_return, &ui_button_styles()->style, 0);
    lv_obj_add_style(g_btn_return, &ui_button_styles()->style_pr, LV_STATE_PRESSED);
    lv_obj_add_style(g_btn_return, &ui_button_styles()->style_focus, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(g_btn_return, &ui_button_styles()->style_focus, LV_STATE_FOCUSED);
    lv_obj_align(g_btn_return, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *lab_btn_text = lv_label_create(g_btn_return);
    lv_label_set_text_static(lab_btn_text, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lab_btn_text, lv_color_make(158, 158, 158), LV_STATE_DEFAULT);
    lv_obj_center(lab_btn_text);
    lv_obj_add_event_cb(g_btn_return, ui_net_config_page_return_click_cb, LV_EVENT_CLICKED, g_page);
#if !CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite
    bsp_btn_register_callback(BSP_BUTTON_MAIN, BUTTON_PRESS_UP, btn_return_down_cb, (void *)g_btn_return);
#endif

    if (ui_get_btn_op_group()) {
        lv_group_add_obj(ui_get_btn_op_group(), g_btn_return);
    }

    g_hint_lab = lv_label_create(g_page);
    lv_label_set_recolor(g_hint_lab, true);
    lv_label_set_text_static(g_hint_lab, "...");
    lv_obj_align(g_hint_lab, LV_ALIGN_CENTER, 0, 0);
    ui_net_config_update_cb(g_net_state, NULL);

    provide_no_err = true;
    /* Do NOT init Wi‑Fi here; defer to Manual Setup or Auto Connect */
    ui_net_config_update_cb(UI_NET_EVT_START, NULL);

    /* Left: Saved Networks list */
    g_saved_list = lv_list_create(g_page);
    lv_obj_set_size(g_saved_list, 170, 120);
    lv_obj_align(g_saved_list, LV_ALIGN_TOP_LEFT, 6, 28);
    refresh_saved_list();

    /* Right: button column */
    lv_obj_t *btn_col = lv_obj_create(g_page);
    lv_obj_set_size(btn_col, 100, 160);
    lv_obj_align(btn_col, LV_ALIGN_TOP_RIGHT, -6, 20);
    lv_obj_clear_flag(btn_col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_auto = lv_btn_create(btn_col);
    lv_obj_set_size(btn_auto, 96, 28);
    lv_obj_align(btn_auto, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *lab_auto = lv_label_create(btn_auto);
    lv_label_set_text_static(lab_auto, "Auto Connect");
    lv_obj_center(lab_auto);
    lv_obj_add_event_cb(btn_auto, auto_connect_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_manual = lv_btn_create(btn_col);
    lv_obj_set_size(btn_manual, 96, 28);
    lv_obj_align(btn_manual, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_t *lab_manual = lv_label_create(btn_manual);
    lv_label_set_text_static(lab_manual, "Manual Setup");
    lv_obj_center(lab_manual);
    lv_obj_add_event_cb(btn_manual, ui_net_config_page_manual_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_forget_all = lv_btn_create(btn_col);
    lv_obj_set_size(btn_forget_all, 96, 28);
    lv_obj_align(btn_forget_all, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_t *lab_fa = lv_label_create(btn_forget_all);
    lv_label_set_text_static(lab_fa, "Forget All");
    lv_obj_center(lab_fa);
    lv_obj_add_event_cb(btn_forget_all, saved_forget_all_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_view = lv_btn_create(btn_col);
    lv_obj_set_size(btn_view, 96, 28);
    lv_obj_align(btn_view, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_t *lab_view = lv_label_create(btn_view);
    lv_label_set_text_static(lab_view, "View Config");
    lv_obj_center(lab_view);
    lv_obj_add_event_cb(btn_view, view_config_btn_cb, LV_EVENT_CLICKED, NULL);

    /* If already connected, present details immediately */
    if (app_wifi_is_connected()) {
        ESP_LOGI(TAG, "Network: already connected");
    }
}

static void forget_btn_cb(lv_event_t *e)
{
    (void)e;
    if (wifi_vault_clear() == ESP_OK) {
        lv_label_set_text(g_hint_lab, "Cleared saved credentials");
    } else {
        lv_label_set_text(g_hint_lab, "Failed to clear saved creds");
    }
}

static void cfg_page_close_cb(lv_event_t *e)
{
    (void)e;
    if (g_cfg_page) {
        lv_obj_del(g_cfg_page);
        g_cfg_page = NULL;
        g_cfg_label = NULL;
    }
}

static void view_config_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_cfg_page) {
        cfg_page_close_cb(NULL);
    }
    g_cfg_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_cfg_page, 270, 160);
    lv_obj_clear_flag(g_cfg_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(g_cfg_page, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_cfg_page, 0, LV_STATE_DEFAULT);
    lv_obj_align(g_cfg_page, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_close = lv_btn_create(g_cfg_page);
    lv_obj_set_size(btn_close, 24, 24);
    lv_obj_align(btn_close, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *lab_x = lv_label_create(btn_close);
    lv_label_set_text_static(lab_x, LV_SYMBOL_LEFT);
    lv_obj_center(lab_x);
    lv_obj_add_event_cb(btn_close, cfg_page_close_cb, LV_EVENT_CLICKED, NULL);

    g_cfg_label = lv_label_create(g_cfg_page);
    lv_label_set_text_static(g_cfg_label, "Reading...");
    lv_obj_set_width(g_cfg_label, 250);
    lv_label_set_long_mode(g_cfg_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(g_cfg_label, LV_ALIGN_TOP_LEFT, 6, 50);

    ui_acquire();
    show_conn_details();
    ui_release();
}
