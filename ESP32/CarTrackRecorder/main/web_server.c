#include "web_server.h"
#include "bsp_board.h"
#include "bsp/esp-bsp.h"
#include "ui_app.h"
#include "wifi_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include <ctype.h>

static const char *TAG = "WEB_SERVER";
static httpd_handle_t s_http_server = NULL;
static esp_netif_t *s_netif_ap = NULL;

// URL 解码辅助函数
static void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// 根目录网页处理器: 展示 TF 卡容量与 GPX 文件下载列表
static esp_err_t index_handler(httpd_req_t *req)
{
    bsp_sdcard_update_space();

    uint64_t total_mb = g_app_state.sd_total_bytes / (1024 * 1024);
    uint64_t free_mb  = g_app_state.sd_free_bytes  / (1024 * 1024);
    uint64_t used_mb  = (total_mb >= free_mb) ? (total_mb - free_mb) : 0;
    int used_percent  = (total_mb > 0) ? (int)((used_mb * 100) / total_mb) : 0;

    httpd_resp_set_type(req, "text/html; charset=utf-8");

    // 1. 发送页面 CSS 样式与框架
    const char *header_html = 
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 轨迹记录仪</title>"
        "<style>"
        "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;margin:0;padding:20px;background:#f5f7fa;color:#333;}"
        ".card{background:#fff;border-radius:12px;padding:20px;box-shadow:0 4px 12px rgba(0,0,0,0.06);margin-bottom:20px;}"
        "h2{margin-top:0;font-size:1.25rem;color:#1a1a1a;}"
        ".bar-box{background:#e2e8f0;border-radius:8px;height:14px;overflow:hidden;margin:10px 0;}"
        ".bar-fill{background:#3b82f6;height:100%;border-radius:8px;}"
        "table{width:100%;border-collapse:collapse;margin-top:10px;}"
        "th,td{padding:12px;text-align:left;border-bottom:1px solid #edf2f7;}"
        "th{background:#f8fafc;font-weight:600;}"
        ".btn{display:inline-block;padding:8px 16px;background:#10b981;color:#fff;text-decoration:none;border-radius:6px;font-size:0.875rem;font-weight:500;}"
        ".btn-blue{background:#3b82f6;}"
        "</style></head><body>";
    httpd_resp_send_chunk(req, header_html, strlen(header_html));

    // 2. 发送 Wi-Fi 设置快捷入口与 TF 卡状态
    char chunk[1024];
    snprintf(chunk, sizeof(chunk),
        "<div class='card'>"
        "<h2>📶 Wi-Fi 联网状态</h2>"
        "<p>当前状态: <b>%s</b> %s</p>"
        "<a class='btn btn-blue' href='/wifi'>⚙️ 扫描并配置 Wi-Fi 网络</a>"
        "</div>"
        "<div class='card'>"
        "<h2>💾 TF 卡存储状态</h2>"
        "<p>已用: <b>%llu MB</b> / 总计: <b>%llu MB</b> (剩余: <b style='color:#10b981'>%llu MB</b>)</p>"
        "<div class='bar-box'><div class='bar-fill' style='width:%d%%'></div></div>"
        "</div>"
        "<div class='card'>"
        "<h2>📂 GPX 轨迹文件列表</h2>"
        "<table><tr><th>文件名</th><th>大小</th><th>操作</th></tr>",
        (g_app_state.wifi_state == WIFI_STATE_CONNECTED) ? "已连接" : "未连接/配网中",
        (g_app_state.wifi_state == WIFI_STATE_CONNECTED) ? g_app_state.wifi_ssid : "",
        used_mb, total_mb, free_mb, used_percent
    );
    httpd_resp_send_chunk(req, chunk, strlen(chunk));

    // 3. 遍历 /sdcard/gpx 目录生成文件列表表格
    DIR *dir = opendir(GPX_STORAGE_DIR);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if ((entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) && entry->d_name[0] != '.' && strstr(entry->d_name, ".gpx")) {
                char full_path[320];
                snprintf(full_path, sizeof(full_path), "%s/%s", GPX_STORAGE_DIR, entry->d_name);
                struct stat st;
                long fsize = 0;
                if (stat(full_path, &st) == 0) {
                    fsize = st.st_size;
                }

                snprintf(chunk, sizeof(chunk),
                    "<tr><td>%s</td><td>%.1f KB</td><td><a class='btn' href='/download?file=%s'>下载</a></td></tr>",
                    entry->d_name, (float)fsize / 1024.0f, entry->d_name
                );
                httpd_resp_send_chunk(req, chunk, strlen(chunk));
            }
        }
        closedir(dir);
    }

    const char *footer = "</table></div></body></html>";
    httpd_resp_send_chunk(req, footer, strlen(footer));
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

// Wi-Fi 扫描与配置页面
static esp_err_t wifi_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    const char *page_head = 
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Wi-Fi 配置</title>"
        "<style>"
        "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;margin:0;padding:20px;background:#f5f7fa;color:#333;}"
        ".card{background:#fff;border-radius:12px;padding:20px;box-shadow:0 4px 12px rgba(0,0,0,0.06);max-width:480px;margin:0 auto;}"
        "h2{margin-top:0;font-size:1.25rem;color:#1a1a1a;}"
        ".form-group{margin-bottom:15px;}"
        "label{display:block;margin-bottom:6px;font-weight:500;font-size:0.9rem;}"
        "input,select{width:100%;box-sizing:border-box;padding:10px;border:1px solid #cbd5e1;border-radius:8px;font-size:1rem;}"
        ".btn{width:100%;padding:12px;background:#3b82f6;color:#fff;border:none;border-radius:8px;font-size:1rem;font-weight:600;cursor:pointer;}"
        ".back{display:inline-block;margin-bottom:15px;color:#64748b;text-decoration:none;}"
        "</style></head><body>"
        "<div class='card'>"
        "<a class='back' href='/'>← 返回首页</a>"
        "<h2>📶 配置车载 Wi-Fi</h2>"
        "<form action='/save_wifi' method='GET'>"
        "<div class='form-group'>"
        "<label>选择或输入 Wi-Fi 名称 (SSID):</label>"
        "<input type='text' name='ssid' id='ssid' placeholder='例如: MyPhone_Hotspot' required>"
        "</div>"
        "<div class='form-group'>"
        "<label>Wi-Fi 密码:</label>"
        "<input type='password' name='pass' placeholder='输入Wi-Fi密码 (无密码留空)'>"
        "</div>"
        "<button type='submit' class='btn'>保存并立即连接</button>"
        "</form>"
        "</div></body></html>";

    httpd_resp_send(req, page_head, strlen(page_head));
    return ESP_OK;
}

// 保存 Wi-Fi 凭证并立即连接
static esp_err_t save_wifi_handler(httpd_req_t *req)
{
    char query[256];
    char raw_ssid[64] = {0};
    char raw_pass[64] = {0};
    char ssid[64] = {0};
    char pass[64] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "ssid", raw_ssid, sizeof(raw_ssid));
        httpd_query_key_value(query, "pass", raw_pass, sizeof(raw_pass));
        url_decode(ssid, raw_ssid);
        url_decode(pass, raw_pass);
    }

    if (strlen(ssid) == 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Received Wi-Fi config from Web: SSID='%s'", ssid);

    // 调用 Wi-Fi 管理器持久化并连接
    wifi_mgr_save_and_connect(ssid, pass);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    const char *resp_html = 
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>保存成功</title>"
        "<style>body{font-family:sans-serif;padding:30px;text-align:center;}h2{color:#10b981;}</style>"
        "</head><body>"
        "<h2>✅ Wi-Fi 配置已成功保存！</h2>"
        "<p>设备正在尝试连接该 Wi-Fi，请关注手表屏幕上方的连接状态指示。</p>"
        "<p><a href='/'>返回首页</a></p>"
        "</body></html>";
    httpd_resp_send(req, resp_html, strlen(resp_html));

    return ESP_OK;
}

// 文件下载处理器
static esp_err_t download_handler(httpd_req_t *req)
{
    char query[128];
    char filename[64] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "file", filename, sizeof(filename));
    }

    if (strlen(filename) == 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char file_path[320];
    snprintf(file_path, sizeof(file_path), "%s/%s", GPX_STORAGE_DIR, filename);

    FILE *f = fopen(file_path, "r");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char dispo[320];
    snprintf(dispo, sizeof(dispo), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_type(req, "application/gpx+xml");
    httpd_resp_set_hdr(req, "Content-Disposition", dispo);

    char buf[4096];
    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_http_server) return ESP_OK;

    ESP_LOGI(TAG, "Starting Wi-Fi AP & HTTP Server...");

    // 1. 初始化 AP 网络接口
    if (!s_netif_ap) {
        s_netif_ap = esp_netif_create_default_wifi_ap();
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WEB_AP_SSID,
            .ssid_len = strlen(WEB_AP_SSID),
            .channel = 1,
            .password = WEB_AP_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    // 2. 启动 HTTP 服务
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.ctrl_port = 32768;
    config.max_open_sockets = 4;

    if (httpd_start(&s_http_server, &config) == ESP_OK) {
        httpd_uri_t uri_index = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(s_http_server, &uri_index);

        httpd_uri_t uri_wifi = {
            .uri       = "/wifi",
            .method    = HTTP_GET,
            .handler   = wifi_page_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(s_http_server, &uri_wifi);

        httpd_uri_t uri_save_wifi = {
            .uri       = "/save_wifi",
            .method    = HTTP_GET,
            .handler   = save_wifi_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(s_http_server, &uri_save_wifi);

        httpd_uri_t uri_download = {
            .uri       = "/download",
            .method    = HTTP_GET,
            .handler   = download_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(s_http_server, &uri_download);

        g_app_state.ap_web_running = true;
        ESP_LOGI(TAG, "HTTP Server started at http://" WEB_AP_IP "/");
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t web_server_stop(void)
{
    if (s_http_server) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
    }
    // 若 STA 已经连接，不完全停止 Wi-Fi，只切回 STA 模式
    if (g_app_state.wifi_state == WIFI_STATE_CONNECTED) {
        esp_wifi_set_mode(WIFI_MODE_STA);
    } else {
        esp_wifi_stop();
    }
    g_app_state.ap_web_running = false;
    ESP_LOGI(TAG, "Wi-Fi AP & HTTP Server stopped.");
    return ESP_OK;
}

esp_err_t web_server_toggle(void)
{
    if (g_app_state.ap_web_running) {
        web_server_stop();
        // 屏幕 Toast 提示已关闭
        ui_show_toast("AP 热点已关闭", "已恢复低功耗模式", 2500);
    } else {
        web_server_start();
        // 屏幕 Toast 提示已开启
        ui_show_toast("AP 热点已开启", "SSID: Car-Tracker-GPX\nIP: 192.168.4.1", 4000);
    }
    return ESP_OK;
}
