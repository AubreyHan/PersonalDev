#include "wifi_mgr.h"
#include "web_server.h"
#include "ui_app.h"
#include "bsp/esp-bsp.h"
#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "WIFI_MGR";
static const char *NVS_NAMESPACE = "wifi_store";
static const char *NVS_KEY_SSID = "ssid";
static const char *NVS_KEY_PASS = "pass";

static esp_netif_t *s_netif_sta = NULL;
static int s_retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA started, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (g_app_state.wifi_state == WIFI_STATE_CONNECTING || g_app_state.wifi_state == WIFI_STATE_CONNECTED) {
            if (s_retry_count < 5) {
                s_retry_count++;
                ESP_LOGW(TAG, "Wi-Fi disconnected. Reconnecting (attempt %d/5)...", s_retry_count);
                esp_wifi_connect();
            } else {
                ESP_LOGE(TAG, "Wi-Fi connection failed. Entering disconnected state.");
                g_app_state.wifi_state = WIFI_STATE_DISCONNECTED;
                g_app_state.wifi_ip[0] = '\0';
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_ip4addr_ntoa(&event->ip_info.ip, g_app_state.wifi_ip, sizeof(g_app_state.wifi_ip));
        g_app_state.wifi_state = WIFI_STATE_CONNECTED;
        s_retry_count = 0;

        ESP_LOGI(TAG, "Wi-Fi connected successfully! IP: %s", g_app_state.wifi_ip);

        // 关闭配网二维码弹窗
        bsp_display_lock(-1);
        ui_hide_wifi_qrcode();
        char msg[64];
        snprintf(msg, sizeof(msg), "SSID: %s\nIP: %s", g_app_state.wifi_ssid, g_app_state.wifi_ip);
        ui_show_toast("Wi-Fi 连接成功", msg, 4000);
        bsp_display_unlock();
    }
}

esp_err_t wifi_mgr_init(void)
{
    if (!s_netif_sta) {
        s_netif_sta = esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    // 尝试从 NVS 读取已保存的 Wi-Fi 凭证
    nvs_handle_t nvs_h;
    char saved_ssid[36] = {0};
    char saved_pass[64] = {0};
    size_t ssid_len = sizeof(saved_ssid);
    size_t pass_len = sizeof(saved_pass);
    bool has_saved_wifi = false;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_h) == ESP_OK) {
        if (nvs_get_str(nvs_h, NVS_KEY_SSID, saved_ssid, &ssid_len) == ESP_OK) {
            nvs_get_str(nvs_h, NVS_KEY_PASS, saved_pass, &pass_len);
            has_saved_wifi = true;
            snprintf(g_app_state.wifi_ssid, sizeof(g_app_state.wifi_ssid), "%s", saved_ssid);
        }
        nvs_close(nvs_h);
    }

    if (has_saved_wifi && strlen(saved_ssid) > 0) {
        ESP_LOGI(TAG, "Found saved Wi-Fi: %s, auto-connecting...", saved_ssid);
        wifi_config_t wifi_config = {0};
        snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", saved_ssid);
        snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", saved_pass);

        g_app_state.wifi_state = WIFI_STATE_CONNECTING;
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_start();
    } else {
        g_app_state.wifi_state = WIFI_STATE_DISCONNECTED;
        ESP_LOGI(TAG, "No saved Wi-Fi found. Ready for QR code provisioning.");
    }

    return ESP_OK;
}

esp_err_t wifi_mgr_start_provisioning(void)
{
    ESP_LOGI(TAG, "Starting Wi-Fi QR Code Provisioning Mode...");
    g_app_state.wifi_state = WIFI_STATE_PROVISIONING;

    // 启动热点与 Web 服务供手机配置
    web_server_start();

    // 屏幕展示二维码 (标准 Wi-Fi 配置格式: WIFI:S:CarTracker-Setup;T:nopass;;)
    bsp_display_lock(-1);
    ui_show_wifi_qrcode("WIFI:S:CarTracker-Setup;T:nopass;;", "请用手机扫码连接热点\n浏览器打开 192.168.4.1 配网");
    bsp_display_unlock();

    return ESP_OK;
}

esp_err_t wifi_mgr_stop_provisioning(void)
{
    if (g_app_state.wifi_state == WIFI_STATE_PROVISIONING) {
        bsp_display_lock(-1);
        ui_hide_wifi_qrcode();
        bsp_display_unlock();

        if (g_app_state.wifi_ssid[0] != '\0') {
            g_app_state.wifi_state = WIFI_STATE_CONNECTING;
        } else {
            g_app_state.wifi_state = WIFI_STATE_DISCONNECTED;
        }
    }
    return ESP_OK;
}

esp_err_t wifi_mgr_toggle_provisioning(void)
{
    if (g_app_state.wifi_state == WIFI_STATE_PROVISIONING) {
        return wifi_mgr_stop_provisioning();
    } else {
        return wifi_mgr_start_provisioning();
    }
}

esp_err_t wifi_mgr_save_and_connect(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Saving Wi-Fi credentials to NVS: SSID='%s'", ssid);

    // 1. 持久化到 NVS
    nvs_handle_t nvs_h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_h) == ESP_OK) {
        nvs_set_str(nvs_h, NVS_KEY_SSID, ssid);
        nvs_set_str(nvs_h, NVS_KEY_PASS, password ? password : "");
        nvs_commit(nvs_h);
        nvs_close(nvs_h);
    }

    snprintf(g_app_state.wifi_ssid, sizeof(g_app_state.wifi_ssid), "%s", ssid);

    // 2. 配置并连接 STA
    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", ssid);
    if (password) {
        snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", password);
    }

    s_retry_count = 0;
    g_app_state.wifi_state = WIFI_STATE_CONNECTING;

    // 切换模式为 STA (或 APSTA) 并连接
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();

    return ESP_OK;
}

esp_err_t wifi_mgr_disconnect(void)
{
    esp_wifi_disconnect();
    g_app_state.wifi_state = WIFI_STATE_DISCONNECTED;
    g_app_state.wifi_ip[0] = '\0';
    return ESP_OK;
}
