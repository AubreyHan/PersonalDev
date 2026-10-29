#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "hal/usb_serial_jtag_ll.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "gps_lc76g.h"
#include "ui_framework.h"

static const char *TAG = "APP_MAIN";

#define WIFI_CFG_AP_SSID      "ESP32-WIFI-CFG"
#define WIFI_FILE_MGR_AP_SSID "ESP32-FILES"
#define WIFI_CFG_MAX_SCAN_APS 12

typedef enum {
    AP_HTTP_MODE_OFF = 0,
    AP_HTTP_MODE_WIFI_CFG,
    AP_HTTP_MODE_FILE_MGR,
} ap_http_mode_t;

static volatile bool s_wifi_connected = false;
static volatile bool s_sta_connecting = false;
static volatile TickType_t s_last_reconnect_tick = 0;
static uint32_t s_wifi_disconnect_streak = 0;
static bool s_wifi_full_tx_power = false;
static vprintf_like_t s_default_vprintf = NULL;

/* TF / microSD Card State */
static SemaphoreHandle_t s_sdcard_mutex = NULL;
static bool s_sdcard_mounted = false;
static bool s_sdcard_rw_ok = false;
static volatile bool s_sdcard_formatting = false;
static uint64_t s_sdcard_total_mb = 0;
static uint64_t s_sdcard_free_mb = 0;
static char s_sdcard_name[16] = "NONE";

/* SoftAP + HTTP Portal / File Manager State */
static volatile ap_http_mode_t s_ap_http_mode = AP_HTTP_MODE_OFF;
static volatile ap_http_mode_t s_target_ap_http_mode = AP_HTTP_MODE_OFF;
static volatile bool s_portal_active = false;
static volatile bool s_portal_connecting_sta = false;
static volatile bool s_portal_cmd_pending = false;

static volatile bool s_pending_qr_update = false;
static volatile ui_wifi_qr_stage_t s_pending_qr_stage = UI_WIFI_QR_STAGE_JOIN_AP;
static volatile bool s_pending_qr_close = false;
static volatile TickType_t s_portal_auto_close_tick = 0;

static httpd_handle_t s_http_server = NULL;
static volatile bool s_dns_task_running = false;
static volatile int s_dns_sock = -1;
static volatile bool s_wifi_scan_running = false;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} scanned_ap_t;

static scanned_ap_t s_cached_aps[WIFI_CFG_MAX_SCAN_APS];
static uint16_t s_cached_ap_count = 0;

/* Prevent USB_SERIAL_JTAG console from blocking real-time tasks (like GPS I2C polling)
 * when the USB cable is plugged into a host computer with no active serial reader. */
static int nonblocking_usb_vprintf(const char *fmt, va_list args)
{
    if (!usb_serial_jtag_ll_txfifo_writable()) {
        return 0;
    }
    return s_default_vprintf ? s_default_vprintf(fmt, args) : vprintf(fmt, args);
}

static void url_decode_inplace(char *str)
{
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void wifi_scan_cache_refresh(void)
{
    if (s_wifi_scan_running) {
        return;
    }
    s_wifi_scan_running = true;

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 30,
        .scan_time.active.max = 75,
    };

    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        s_wifi_scan_running = false;
        return;
    }

    uint16_t ap_num = WIFI_CFG_MAX_SCAN_APS;
    wifi_ap_record_t ap_records[WIFI_CFG_MAX_SCAN_APS];
    if (esp_wifi_scan_get_ap_records(&ap_num, ap_records) != ESP_OK) {
        s_wifi_scan_running = false;
        return;
    }

    uint16_t out_idx = 0;
    for (uint16_t i = 0; i < ap_num && out_idx < WIFI_CFG_MAX_SCAN_APS; ++i) {
        if (ap_records[i].ssid[0] == '\0') {
            continue;
        }
        /* Deduplicate SSIDs */
        bool dup = false;
        for (uint16_t j = 0; j < out_idx; ++j) {
            if (strcmp(s_cached_aps[j].ssid, (const char *)ap_records[i].ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        snprintf(s_cached_aps[out_idx].ssid, sizeof(s_cached_aps[out_idx].ssid), "%s",
                 (const char *)ap_records[i].ssid);
        s_cached_aps[out_idx].rssi = ap_records[i].rssi;
        s_cached_aps[out_idx].secure = (ap_records[i].authmode != WIFI_AUTH_OPEN);
        out_idx++;
    }
    s_cached_ap_count = out_idx;
    s_wifi_scan_running = false;
}

static void wifi_scan_worker_task(void *arg)
{
    (void)arg;
    wifi_scan_cache_refresh();
    vTaskDelete(NULL);
}

/* Lightweight Captive Portal DNS server (UDP 53): resolves all A queries to 192.168.4.1
 * so iOS / Android automatically pop up the Wi-Fi configuration sheet upon joining AP. */
static void captive_dns_server_task(void *arg)
{
    (void)arg;
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        s_dns_task_running = false;
        vTaskDelete(NULL);
        return;
    }
    s_dns_sock = sock;

    struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sock);
        s_dns_sock = -1;
        s_dns_task_running = false;
        vTaskDelete(NULL);
        return;
    }

    uint8_t rx_buf[256];
    uint8_t tx_buf[288];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (s_dns_task_running) {
        addr_len = sizeof(client_addr);
        int len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0,
                           (struct sockaddr *)&client_addr, &addr_len);
        if (len < 12 || len > 240) {
            continue;
        }

        /* Build minimal DNS A-record response pointing to 192.168.4.1 */
        memcpy(tx_buf, rx_buf, len);
        tx_buf[2] = 0x81; /* QR=1 (Response), RD=1 */
        tx_buf[3] = 0x80; /* RA=1, RCODE=0 */
        tx_buf[6] = 0x00;
        tx_buf[7] = 0x01; /* ANCOUNT = 1 */
        tx_buf[8] = 0x00;
        tx_buf[9] = 0x00;
        tx_buf[10] = 0x00;
        tx_buf[11] = 0x00;

        int pos = len;
        tx_buf[pos++] = 0xC0; /* Pointer to question name at offset 12 */
        tx_buf[pos++] = 0x0C;
        tx_buf[pos++] = 0x00; /* TYPE A (1) */
        tx_buf[pos++] = 0x01;
        tx_buf[pos++] = 0x00; /* CLASS IN (1) */
        tx_buf[pos++] = 0x01;
        tx_buf[pos++] = 0x00; /* TTL = 60s */
        tx_buf[pos++] = 0x00;
        tx_buf[pos++] = 0x00;
        tx_buf[pos++] = 0x3C;
        tx_buf[pos++] = 0x00; /* RDLENGTH = 4 */
        tx_buf[pos++] = 0x04;
        tx_buf[pos++] = 192;
        tx_buf[pos++] = 168;
        tx_buf[pos++] = 4;
        tx_buf[pos++] = 1;

        sendto(sock, tx_buf, pos, 0, (struct sockaddr *)&client_addr, addr_len);
    }

    close(sock);
    s_dns_sock = -1;
    vTaskDelete(NULL);
}

static const char *PORTAL_HTML_PAGE =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1'>"
    "<title>ESP32 Wi-Fi 配置</title>"
    "<style>"
    "body{background:#08100C;color:#F2F7F4;font-family:-apple-system,sans-serif;margin:0;padding:20px;}"
    ".card{max-width:380px;margin:0 auto;background:#0E1B14;border:1px solid #1E3829;border-radius:16px;padding:20px;}"
    "h2{margin:0 0 6px;color:#00E676;font-size:20px;letter-spacing:1px;}"
    "p.sub{margin:0 0 18px;color:#7E998A;font-size:13px;}"
    "label{display:block;font-size:13px;color:#9BB5A6;margin:12px 0 6px;}"
    "input{width:100%;box-sizing:border-box;padding:12px;border-radius:10px;border:1px solid #264634;"
    "background:#070E0A;color:#fff;font-size:15px;outline:none;}"
    "input:focus{border-color:#00E676;}"
    "button.btn{width:100%;margin-top:18px;padding:13px;border:none;border-radius:10px;"
    "background:#00E676;color:#041008;font-size:16px;font-weight:700;cursor:pointer;}"
    "button.scan{background:#15291E;color:#00E676;border:1px solid #264634;margin-top:10px;font-size:13px;padding:9px;}"
    ".ap-list{margin-top:12px;max-height:190px;overflow-y:auto;border:1px solid #1B3024;border-radius:10px;}"
    ".ap-item{padding:10px 12px;border-bottom:1px solid #14241B;display:flex;justify-content:space-between;"
    "align-items:center;cursor:pointer;font-size:14px;}"
    ".ap-item:last-child{border-bottom:none;}"
    ".ap-item:active{background:#193124;}"
    ".rssi{color:#00E676;font-size:12px;}"
    "</style></head><body><div class='card'>"
    "<h2>WIFI CONFIG</h2>"
    "<p class='sub'>选择或输入 Wi-Fi 名称与密码，保存后永久生效并自动重连</p>"
    "<form action='/save' method='GET'>"
    "<label>Wi-Fi 名称 (SSID)</label>"
    "<input id='ssid' name='ssid' placeholder='点击下方列表或手动输入' required maxlength='32'>"
    "<label>Wi-Fi 密码 (无密码可留空)</label>"
    "<input id='pass' name='pass' type='password' placeholder='输入 Wi-Fi 密码' maxlength='63'>"
    "<button class='btn' type='submit'>保存并自动连接</button>"
    "</form>"
    "<button class='btn scan' onclick='loadAps(1)'>刷新附近 Wi-Fi 列表</button>"
    "<div id='aps' class='ap-list'></div>"
    "</div>"
    "<script>"
    "function pick(s){document.getElementById('ssid').value=s;document.getElementById('pass').focus();}"
    "function loadAps(r){"
    "var b=document.getElementById('aps');b.innerHTML='<div class=\"ap-item\">正在加载...</div>';"
    "fetch('/scan'+(r?'?refresh=1':'')).then(x=>x.json()).then(d=>{"
    "if(!d.length){b.innerHTML='<div class=\"ap-item\">未扫到热点，请手动输入</div>';return;}"
    "b.innerHTML=d.map(a=>`<div class='ap-item' onclick='pick(${JSON.stringify(a.ssid)})'>"
    "<span>${a.ssid}</span><span class='rssi'>${a.rssi} dBm</span></div>`).join('');"
    "}).catch(()=>{b.innerHTML='<div class=\"ap-item\">请手动输入 Wi-Fi 名称</div>';});}"
    "loadAps(0);"
    "</script></body></html>";

static const char *FILE_MGR_HTML_PAGE =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1'>"
    "<title>TF 卡文件管理器</title>"
    "<style>"
    "body{background:#08100C;color:#F2F7F4;font-family:-apple-system,sans-serif;margin:0;padding:18px;}"
    ".card{max-width:520px;margin:0 auto;background:#0E1B14;border:1px solid #1E3829;border-radius:16px;padding:18px;}"
    ".hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:6px;}"
    "h2{margin:0;color:#00E676;font-size:19px;letter-spacing:1px;}"
    ".badge{background:#12241A;color:#00E676;border:1px solid #264634;padding:4px 10px;border-radius:99px;font-size:12px;}"
    ".meta{color:#8AA696;font-size:12px;margin-bottom:14px;}"
    ".bar{display:flex;gap:8px;align-items:center;margin-bottom:12px;}"
    ".path{flex:1;background:#070E0A;border:1px solid #1B3024;border-radius:8px;padding:8px 10px;"
    "font-family:monospace;font-size:13px;color:#00E676;overflow-x:auto;white-space:nowrap;}"
    "button.btn{background:#15291E;color:#00E676;border:1px solid #264634;border-radius:8px;"
    "padding:8px 12px;font-size:13px;cursor:pointer;white-space:nowrap;}"
    "button.btn:active{background:#1F3D2C;}"
    ".list{border:1px solid #1B3024;border-radius:12px;overflow:hidden;background:#0A140F;}"
    ".row{display:flex;justify-content:space-between;align-items:center;padding:12px 14px;"
    "border-bottom:1px solid #14241B;gap:10px;}"
    ".row:last-child{border-bottom:none;}"
    ".name{font-size:14px;word-break:break-all;color:#F2F7F4;}"
    ".dir{color:#00E676;font-weight:600;cursor:pointer;}"
    ".right{display:flex;align-items:center;gap:10px;flex-shrink:0;}"
    ".sz{color:#7E998A;font-size:12px;}"
    "a.dl{background:#00E676;color:#041008;text-decoration:none;font-size:12px;font-weight:700;"
    "padding:6px 12px;border-radius:8px;}"
    ".empty{padding:24px;text-align:center;color:#7E998A;font-size:13px;}"
    "</style></head><body><div class='card'>"
    "<div class='hdr'><h2>TF FILE MANAGER</h2><span id='st' class='badge'>LOADING</span></div>"
    "<div id='meta' class='meta'>正在读取 TF 卡信息...</div>"
    "<div class='bar'>"
    "<div id='cur' class='path'>/</div>"
    "<button id='upBtn' class='btn' style='display:none' onclick='goUp()'>上级目录</button>"
    "<button class='btn' onclick='loadDir(curDir)'>刷新</button>"
    "</div>"
    "<div id='list' class='list'><div class='empty'>正在加载文件列表...</div></div>"
    "</div>"
    "<script>"
    "var curDir='/';"
    "function fmtSz(b){if(b<1024)return b+' B';if(b<1048576)return (b/1024).toFixed(1)+' KB';"
    "return (b/1048576).toFixed(2)+' MB';}"
    "function goUp(){if(curDir==='/')return;var p=curDir.replace(/\\/+$/,'').split('/');"
    "p.pop();var n=p.join('/');loadDir(n?n:'/');}"
    "function joinP(d,n){return (d==='/'?'':d)+'/'+n;}"
    "function loadDir(d){"
    "curDir=d;document.getElementById('cur').textContent=d;"
    "document.getElementById('upBtn').style.display=(d==='/'?'none':'inline-block');"
    "var box=document.getElementById('list');"
    "fetch('/api/files?dir='+encodeURIComponent(d)).then(r=>r.json()).then(res=>{"
    "document.getElementById('st').textContent=res.mounted?'MOUNTED':'NO CARD';"
    "document.getElementById('meta').textContent=res.mounted?"
    "('卡名称: '+res.name+' | 可用: '+res.free_mb+' MB / 总计: '+res.total_mb+' MB'):'未检测到 TF 卡';"
    "if(!res.files||!res.files.length){box.innerHTML='<div class=\"empty\">当前目录为空</div>';return;}"
    "res.files.sort((a,b)=>(b.dir-a.dir)||a.name.localeCompare(b.name));"
    "box.innerHTML=res.files.map(f=>{"
    "var full=joinP(curDir,f.name);"
    "if(f.dir){return `<div class='row'><span class='name dir' onclick='loadDir(${JSON.stringify(full)})'>[目录] ${f.name}/</span>"
    "<div class='right'><button class='btn' onclick='loadDir(${JSON.stringify(full)})'>打开</button></div></div>`;}"
    "var dlUrl='/download?path='+encodeURIComponent(full);"
    "return `<div class='row'><span class='name'>${f.name}</span>"
    "<div class='right'><span class='sz'>${fmtSz(f.size)}</span>"
    "<a class='dl' href='${dlUrl}' download>下载</a></div></div>`;"
    "}).join('');"
    "}).catch(()=>{box.innerHTML='<div class=\"empty\">读取目录失败，请点击刷新重试</div>';});}"
    "loadDir('/');"
    "</script></body></html>";

static esp_err_t http_files_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, FILE_MGR_HTML_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t http_root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (s_ap_http_mode == AP_HTTP_MODE_FILE_MGR) {
        return httpd_resp_send(req, FILE_MGR_HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, PORTAL_HTML_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t http_api_files_handler(httpd_req_t *req)
{
    char qbuf[256] = {0};
    char rel_dir[192] = "/";
    if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
        if (httpd_query_key_value(qbuf, "dir", rel_dir, sizeof(rel_dir)) == ESP_OK) {
            url_decode_inplace(rel_dir);
        }
    }

    if (rel_dir[0] != '/' || strstr(rel_dir, "..") != NULL) {
        snprintf(rel_dir, sizeof(rel_dir), "/");
    }

    /* Refresh free/total space */
    if (s_sdcard_mounted) {
        uint64_t total_bytes = 0, free_bytes = 0;
        if (esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total_bytes, &free_bytes) == ESP_OK) {
            s_sdcard_total_mb = total_bytes / (1024ULL * 1024ULL);
            s_sdcard_free_mb = free_bytes / (1024ULL * 1024ULL);
        }
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char chunk[512];
    int n = snprintf(chunk, sizeof(chunk),
                     "{\"mounted\":%s,\"name\":\"%s\",\"total_mb\":%llu,\"free_mb\":%llu,\"files\":[",
                     s_sdcard_mounted ? "true" : "false",
                     s_sdcard_name,
                     (unsigned long long)s_sdcard_total_mb,
                     (unsigned long long)s_sdcard_free_mb);
    httpd_resp_send_chunk(req, chunk, n);

    if (s_sdcard_mounted) {
        char full_dir[256];
        if (strcmp(rel_dir, "/") == 0) {
            snprintf(full_dir, sizeof(full_dir), "%s", BSP_SD_MOUNT_POINT);
        } else {
            snprintf(full_dir, sizeof(full_dir), "%s%s", BSP_SD_MOUNT_POINT, rel_dir);
        }

        DIR *dir = opendir(full_dir);
        if (dir) {
            struct dirent *ent;
            bool first = true;
            while ((ent = readdir(dir)) != NULL) {
                if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                    continue;
                }
                char item_path[520];
                snprintf(item_path, sizeof(item_path), "%s/%s", full_dir, ent->d_name);

                struct stat st;
                bool is_dir = (ent->d_type == DT_DIR);
                unsigned long long fsize = 0;
                if (stat(item_path, &st) == 0) {
                    is_dir = S_ISDIR(st.st_mode);
                    fsize = is_dir ? 0ULL : (unsigned long long)st.st_size;
                }

                /* Escape quotes/backslashes in filename */
                char safe_name[260];
                size_t si = 0;
                for (size_t k = 0; ent->d_name[k] != '\0' && si < sizeof(safe_name) - 2; ++k) {
                    char c = ent->d_name[k];
                    if (c == '"' || c == '\\') {
                        safe_name[si++] = '\\';
                    }
                    safe_name[si++] = c;
                }
                safe_name[si] = '\0';

                int clen = snprintf(chunk, sizeof(chunk),
                                    "%s{\"name\":\"%s\",\"dir\":%s,\"size\":%llu}",
                                    first ? "" : ",",
                                    safe_name,
                                    is_dir ? "true" : "false",
                                    fsize);
                httpd_resp_send_chunk(req, chunk, clen);
                first = false;
            }
            closedir(dir);
        }
    }

    httpd_resp_send_chunk(req, "]}", 2);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t http_download_handler(httpd_req_t *req)
{
    char qbuf[320] = {0};
    char rel_path[240] = {0};

    if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
        if (httpd_query_key_value(qbuf, "path", rel_path, sizeof(rel_path)) == ESP_OK) {
            url_decode_inplace(rel_path);
        }
    }

    if (!s_sdcard_mounted || rel_path[0] != '/' || strstr(rel_path, "..") != NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid file path");
        return ESP_FAIL;
    }

    char full_path[300];
    snprintf(full_path, sizeof(full_path), "%s%s", BSP_SD_MOUNT_POINT, rel_path);

    struct stat st;
    if (stat(full_path, &st) != 0 || S_ISDIR(st.st_mode)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found on TF card");
        return ESP_FAIL;
    }

    FILE *fp = fopen(full_path, "rb");
    if (!fp) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }

    const char *base_name = strrchr(rel_path, '/');
    base_name = base_name ? (base_name + 1) : rel_path;

    char disp_hdr[280];
    snprintf(disp_hdr, sizeof(disp_hdr), "attachment; filename=\"%s\"", base_name);

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", disp_hdr);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char *buf = malloc(4096);
    if (!buf) {
        fclose(fp);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "HTTP File Download started: %s (%llu bytes)",
             full_path, (unsigned long long)st.st_size);

    esp_err_t res = ESP_OK;
    size_t nread;
    while ((nread = fread(buf, 1, 4096, fp)) > 0) {
        if (httpd_resp_send_chunk(req, buf, nread) != ESP_OK) {
            res = ESP_FAIL;
            break;
        }
    }

    free(buf);
    fclose(fp);
    if (res == ESP_OK) {
        httpd_resp_send_chunk(req, NULL, 0);
        ESP_LOGI(TAG, "HTTP File Download completed: %s", full_path);
    }
    return res;
}

static esp_err_t http_scan_handler(httpd_req_t *req)
{
    char qbuf[32] = {0};
    if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
        if (strstr(qbuf, "refresh=1") != NULL) {
            wifi_scan_cache_refresh();
        }
    }

    char *json = malloc(1024);
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int pos = 0;
    pos += snprintf(json + pos, 1024 - pos, "[");
    for (uint16_t i = 0; i < s_cached_ap_count && pos < 940; ++i) {
        /* Escape quotes/backslashes if any in SSID */
        char safe_ssid[68];
        size_t si = 0;
        for (size_t k = 0; s_cached_aps[i].ssid[k] != '\0' && si < sizeof(safe_ssid) - 2; ++k) {
            char c = s_cached_aps[i].ssid[k];
            if (c == '"' || c == '\\') {
                safe_ssid[si++] = '\\';
            }
            safe_ssid[si++] = c;
        }
        safe_ssid[si] = '\0';

        pos += snprintf(json + pos, 1024 - pos, "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                        (i > 0) ? "," : "", safe_ssid, (int)s_cached_aps[i].rssi);
    }
    pos += snprintf(json + pos, 1024 - pos, "]");

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    esp_err_t res = httpd_resp_send(req, json, pos);
    free(json);
    return res;
}

static esp_err_t http_save_handler(httpd_req_t *req)
{
    char qbuf[256] = {0};
    char ssid_raw[96] = {0};
    char pass_raw[160] = {0};

    if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
        httpd_query_key_value(qbuf, "ssid", ssid_raw, sizeof(ssid_raw));
        httpd_query_key_value(qbuf, "pass", pass_raw, sizeof(pass_raw));
    }

    url_decode_inplace(ssid_raw);
    url_decode_inplace(pass_raw);

    if (ssid_raw[0] == '\0') {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        return httpd_resp_send(req, NULL, 0);
    }

    ESP_LOGI(TAG, "Saving new Wi-Fi credentials to NVS flash: SSID='%s' (pass_len=%d)",
             ssid_raw, (int)strlen(pass_raw));

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, ssid_raw, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, pass_raw, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = (pass_raw[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;

    /* WIFI_STORAGE_FLASH is active, so esp_wifi_set_config permanently persists to NVS */
    esp_wifi_disconnect();
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));

    s_portal_connecting_sta = true;
    s_pending_qr_stage = UI_WIFI_QR_STAGE_CONNECTING;
    s_pending_qr_close = false;
    s_pending_qr_update = true;

    esp_wifi_connect();

    static const char *SAVE_OK_HTML =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Wi-Fi 已保存</title>"
        "<style>body{background:#08100C;color:#F2F7F4;font-family:-apple-system,sans-serif;padding:28px;text-align:center;}"
        ".box{max-width:340px;margin:20px auto;background:#0E1B14;border:1px solid #00E676;border-radius:16px;padding:24px;}"
        "h2{color:#00E676;margin:0 0 10px;}p{color:#9BB5A6;font-size:14px;line-height:1.5;}</style></head>"
        "<body><div class='box'><h2>已保存并开始连接</h2>"
        "<p>Wi-Fi 配置已永久写入设备闪存 (NVS)，设备正在连接网络，连接成功后配网页面将自动关闭。</p>"
        "</div></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, SAVE_OK_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t http_captive_redirect_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

static void wifi_sta_try_reconnect(bool force_restart)
{
    if (s_portal_active && !s_portal_connecting_sta) {
        return;
    }

    wifi_config_t cfg = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK || cfg.sta.ssid[0] == '\0') {
        return;
    }

    if (force_restart) {
        esp_wifi_disconnect();
        s_sta_connecting = false;
    }

    s_last_reconnect_tick = xTaskGetTickCount();
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_OK) {
        s_sta_connecting = true;
        ESP_LOGI(TAG, "STA connecting to saved Wi-Fi SSID: %s", (const char *)cfg.sta.ssid);
    }
}

static void ap_http_set_mode_impl(ap_http_mode_t mode)
{
    if (s_ap_http_mode == mode) {
        return;
    }

    if (mode == AP_HTTP_MODE_OFF) {
        ESP_LOGI(TAG, "Stopping SoftAP + HTTP service, restoring STA auto-connect...");
        s_ap_http_mode = AP_HTTP_MODE_OFF;
        s_portal_active = false;
        s_portal_connecting_sta = false;
        s_portal_auto_close_tick = 0;

        if (s_dns_task_running) {
            s_dns_task_running = false;
            /* Wait up to 300ms for captive_dns_server_task to exit and close its own socket cleanly */
            for (int wait_i = 0; wait_i < 15 && s_dns_sock >= 0; ++wait_i) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }

        /* Note: We intentionally keep s_http_server running rather than calling httpd_stop(),
         * because mobile/desktop browsers hold HTTP keep-alive TCP sockets open after browsing
         * or downloading files, which would otherwise block httpd_stop() and prevent STA recovery. */
        esp_wifi_set_mode(WIFI_MODE_STA);

        /* Immediately reconnect STA to saved NVS Wi-Fi if not already connected */
        if (!s_wifi_connected) {
            s_wifi_disconnect_streak = 0;
            wifi_sta_try_reconnect(true);
        }
        return;
    }

    const char *ap_ssid = (mode == AP_HTTP_MODE_FILE_MGR) ? WIFI_FILE_MGR_AP_SSID : WIFI_CFG_AP_SSID;
    ESP_LOGI(TAG, "Starting SoftAP (%s) + HTTP service (mode=%d)...", ap_ssid, (int)mode);

    s_ap_http_mode = mode;
    s_portal_active = true;
    s_portal_connecting_sta = false;
    s_portal_auto_close_tick = 0;
    s_wifi_connected = false;
    s_sta_connecting = false;

    /* Pause STA while SoftAP is active so single-radio channel hopping does not drop SoftAP clients */
    esp_wifi_disconnect();

    wifi_config_t ap_cfg = {
        .ap = {
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = strlen(ap_ssid);

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);

    if (mode == AP_HTTP_MODE_WIFI_CFG && !s_wifi_scan_running) {
        /* Perform scan asynchronously on background worker so UI/telemetry never blocks */
        xTaskCreate(wifi_scan_worker_task, "wifi_scan", 4096, NULL, 2, NULL);
    }

    /* Start Captive Portal DNS server */
    if (!s_dns_task_running && s_dns_sock < 0) {
        s_dns_task_running = true;
        xTaskCreate(captive_dns_server_task, "captive_dns", 4096, NULL, 3, NULL);
    }

    /* Start HTTP server (serving both Wi-Fi Provisioning & TF Card File Manager) */
    if (!s_http_server) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.stack_size = 8192;
        config.max_uri_handlers = 12;
        config.lru_purge_enable = true;

        if (httpd_start(&s_http_server, &config) == ESP_OK) {
            const httpd_uri_t uri_root = {.uri = "/", .method = HTTP_GET, .handler = http_root_handler};
            const httpd_uri_t uri_files = {.uri = "/files", .method = HTTP_GET, .handler = http_files_page_handler};
            const httpd_uri_t uri_api_files = {.uri = "/api/files", .method = HTTP_GET, .handler = http_api_files_handler};
            const httpd_uri_t uri_download = {.uri = "/download", .method = HTTP_GET, .handler = http_download_handler};
            const httpd_uri_t uri_scan = {.uri = "/scan", .method = HTTP_GET, .handler = http_scan_handler};
            const httpd_uri_t uri_save = {.uri = "/save", .method = HTTP_GET, .handler = http_save_handler};
            const httpd_uri_t uri_gen204 = {.uri = "/generate_204", .method = HTTP_GET, .handler = http_root_handler};
            const httpd_uri_t uri_hotspot = {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = http_root_handler};

            httpd_register_uri_handler(s_http_server, &uri_root);
            httpd_register_uri_handler(s_http_server, &uri_files);
            httpd_register_uri_handler(s_http_server, &uri_api_files);
            httpd_register_uri_handler(s_http_server, &uri_download);
            httpd_register_uri_handler(s_http_server, &uri_scan);
            httpd_register_uri_handler(s_http_server, &uri_save);
            httpd_register_uri_handler(s_http_server, &uri_gen204);
            httpd_register_uri_handler(s_http_server, &uri_hotspot);
            httpd_register_err_handler(s_http_server, HTTPD_404_NOT_FOUND, http_captive_redirect_handler);
        }
    }
}

static void on_ui_wifi_portal_ctrl(bool open)
{
    s_target_ap_http_mode = open ? AP_HTTP_MODE_WIFI_CFG : AP_HTTP_MODE_OFF;
    s_portal_cmd_pending = true;
}

static void on_ui_file_mgr_ctrl(bool enable)
{
    s_target_ap_http_mode = enable ? AP_HTTP_MODE_FILE_MGR : AP_HTTP_MODE_OFF;
    s_portal_cmd_pending = true;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_wifi_disconnect_streak = 0;
        wifi_sta_try_reconnect(false);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        s_sta_connecting = false;
        s_wifi_disconnect_streak++;
        s_last_reconnect_tick = xTaskGetTickCount();
        /* Only retry immediately if the user just submitted new credentials in the portal;
         * otherwise let ui_update_task apply exponential backoff so continuous 2.4GHz active scanning
         * does not inject RF noise / power ripple into the GNSS LNA outdoors! */
        if (s_portal_active && s_portal_connecting_sta && s_wifi_disconnect_streak <= 2) {
            wifi_sta_try_reconnect(false);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        if (s_portal_active && s_ap_http_mode == AP_HTTP_MODE_WIFI_CFG && !s_portal_connecting_sta) {
            ESP_LOGI(TAG, "Phone joined SoftAP %s -> switching QR to http://192.168.4.1", WIFI_CFG_AP_SSID);
            s_pending_qr_stage = UI_WIFI_QR_STAGE_OPEN_URL;
            s_pending_qr_close = false;
            s_pending_qr_update = true;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        s_sta_connecting = false;
        s_wifi_disconnect_streak = 0;
        ESP_LOGI(TAG, "Wi-Fi connected and got IP!");
        static bool s_sntp_started = false;
        if (!s_sntp_started) {
            esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
            if (esp_netif_sntp_init(&sntp_cfg) == ESP_OK) {
                s_sntp_started = true;
                ESP_LOGI(TAG, "Started indoor Wi-Fi SNTP time sync (ntp.aliyun.com)");
            }
        }
        if (s_portal_active && s_ap_http_mode == AP_HTTP_MODE_WIFI_CFG && s_portal_connecting_sta) {
            s_pending_qr_stage = UI_WIFI_QR_STAGE_CONNECTED;
            s_pending_qr_close = false;
            s_pending_qr_update = true;
            s_portal_auto_close_tick = xTaskGetTickCount() + pdMS_TO_TICKS(1800);
        }
    }
}

static void wifi_monitor_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi init skipped");
        return;
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_wifi_start();
    /* Start with moderate Wi-Fi TX power (13 dBm = 52 quarter-dBm) during initial GNSS satellite acquisition
     * to minimize 2.4GHz LNA saturation and rail droop; restored to full 19.5 dBm once GNSS locks. */
    esp_wifi_set_max_tx_power(52);
    s_wifi_full_tx_power = false;
}

static int get_wifi_signal_percent(bool *out_connected)
{
    if (!s_wifi_connected) {
        *out_connected = false;
        return 0;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        *out_connected = true;
        int rssi = ap_info.rssi;
        /* Map -90 dBm .. -50 dBm -> 8% .. 100% */
        if (rssi <= -90) {
            return 8;
        }
        if (rssi >= -50) {
            return 100;
        }
        return 8 + ((rssi + 90) * 92) / 40;
    }
    *out_connected = false;
    return 0;
}

static void sdcard_refresh_info_and_verify(void)
{
    if (!bsp_sdcard) {
        return;
    }
    snprintf(s_sdcard_name, sizeof(s_sdcard_name), "%s", bsp_sdcard->cid.name);

    uint64_t total_bytes = 0, free_bytes = 0;
    if (esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total_bytes, &free_bytes) == ESP_OK) {
        s_sdcard_total_mb = total_bytes / (1024ULL * 1024ULL);
        s_sdcard_free_mb = free_bytes / (1024ULL * 1024ULL);
    } else {
        s_sdcard_total_mb = ((uint64_t)bsp_sdcard->csd.capacity * bsp_sdcard->csd.sector_size) / (1024ULL * 1024ULL);
    }

    /* Perform real write + read + verify test */
    const char *test_path = BSP_SD_MOUNT_POINT "/rwtest.tmp";
    const char *test_payload = "ESP32S3_TF_RW_OK_2026\n";
    bool rw_pass = false;

    FILE *fw = fopen(test_path, "w");
    if (fw) {
        size_t written = fwrite(test_payload, 1, strlen(test_payload), fw);
        fflush(fw);
        fclose(fw);
        if (written == strlen(test_payload)) {
            FILE *fr = fopen(test_path, "r");
            if (fr) {
                char rbuf[64] = {0};
                size_t nread = fread(rbuf, 1, sizeof(rbuf) - 1, fr);
                fclose(fr);
                if (nread == strlen(test_payload) && strcmp(rbuf, test_payload) == 0) {
                    rw_pass = true;
                }
            }
        }
        unlink(test_path);
    }
    s_sdcard_rw_ok = rw_pass;

    /* Ensure a sample device report file exists on the TF card for quick download verification */
    const char *info_path = BSP_SD_MOUNT_POINT "/DEVICE_INFO.TXT";
    struct stat st;
    if (stat(info_path, &st) != 0) {
        FILE *fi = fopen(info_path, "w");
        if (fi) {
            fprintf(fi,
                    "========================================\r\n"
                    "  ESP32-S3 1.75\" AMOLED GNSS Terminal  \r\n"
                    "========================================\r\n"
                    "TF Card Name : %s\r\n"
                    "Total Size   : %llu MB\r\n"
                    "Free Space   : %llu MB\r\n"
                    "HTTP Server  : http://192.168.4.1/\r\n",
                    s_sdcard_name,
                    (unsigned long long)s_sdcard_total_mb,
                    (unsigned long long)s_sdcard_free_mb);
            fclose(fi);
        }
    }
}

static void sdcard_format_worker_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Starting TF card FAT format at %s...", BSP_SD_MOUNT_POINT);
    /* Give track_logger_task up to 1.1s to notice s_sdcard_formatting and close any open GPX files */
    vTaskDelay(pdMS_TO_TICKS(1100));
    if (s_sdcard_mutex) {
        xSemaphoreTake(s_sdcard_mutex, portMAX_DELAY);
    }
    esp_err_t err = ESP_FAIL;

    if (!s_sdcard_mounted || !bsp_sdcard) {
        err = bsp_sdcard_mount();
        if (err == ESP_OK && bsp_sdcard) {
            s_sdcard_mounted = true;
        }
    }

    if (s_sdcard_mounted && bsp_sdcard) {
        err = esp_vfs_fat_sdcard_format(BSP_SD_MOUNT_POINT, bsp_sdcard);
    }

    ui_tf_status_t final_status;
    if (err == ESP_OK) {
        sdcard_refresh_info_and_verify();
        final_status = UI_TF_STATUS_DONE;
        ESP_LOGI(TAG, "TF card format DONE: name=%s, total=%lluMB, free=%lluMB, rw_ok=%d",
                 s_sdcard_name,
                 (unsigned long long)s_sdcard_total_mb,
                 (unsigned long long)s_sdcard_free_mb,
                 s_sdcard_rw_ok);
    } else {
        final_status = s_sdcard_mounted ? UI_TF_STATUS_FAILED : UI_TF_STATUS_NO_CARD;
        ESP_LOGE(TAG, "TF card format FAILED: %s (0x%x)", esp_err_to_name(err), err);
    }

    if (s_sdcard_mutex) {
        xSemaphoreGive(s_sdcard_mutex);
    }

    if (bsp_display_lock(500) == ESP_OK) {
        ui_framework_set_tf_status(final_status);
        bsp_display_unlock();
    }

    s_sdcard_formatting = false;
    vTaskDelete(NULL);
}

static void on_ui_tf_format_requested(void)
{
    if (s_sdcard_formatting) {
        return;
    }
    s_sdcard_formatting = true;
    if (xTaskCreate(sdcard_format_worker_task, "tf_fmt", 6144, NULL, 3, NULL) != pdPASS) {
        s_sdcard_formatting = false;
        ui_framework_set_tf_status(UI_TF_STATUS_FAILED);
    }
}

static void sdcard_init_and_verify(void)
{
    ESP_LOGI(TAG, "Mounting TF / microSD card at %s...", BSP_SD_MOUNT_POINT);
    esp_err_t err = bsp_sdcard_mount();
    if (err != ESP_OK || !bsp_sdcard) {
        s_sdcard_mounted = false;
        s_sdcard_rw_ok = false;
        ESP_LOGW(TAG, "TF card mount failed: %s (0x%x)", esp_err_to_name(err), err);
        if (bsp_display_lock(200) == ESP_OK) {
            ui_framework_set_tf_status(UI_TF_STATUS_NO_CARD);
            bsp_display_unlock();
        }
        return;
    }

    s_sdcard_mounted = true;
    sdcard_refresh_info_and_verify();

    if (bsp_display_lock(200) == ESP_OK) {
        ui_framework_set_tf_status(UI_TF_STATUS_READY);
        bsp_display_unlock();
    }

    ESP_LOGI(TAG, "TF Card Status: mounted=%d, name=%s, total=%lluMB, free=%lluMB, rw_ok=%d",
             s_sdcard_mounted, s_sdcard_name,
             (unsigned long long)s_sdcard_total_mb,
             (unsigned long long)s_sdcard_free_mb,
             s_sdcard_rw_ok);
}

/* ============================================================================
 * Tool 04: 1Hz GNSS Trajectory Logger (WGS-84 'W*.gpx' & GCJ-02 'G*.gpx')
 * ============================================================================ */

#define TRACK_LOG_NVS_NAMESPACE "trk_cfg"
#define TRACK_LOG_NVS_KEY       "active"
#define TRACK_LOG_MIN_FREE_BYTES (2ULL * 1024ULL * 1024ULL) /* 2 MB safety margin */
#define TRACK_LOG_SEGMENT_SECS   3600U                      /* 1 hour per file pair */
#define GPX_CLOSING_TAGS         "</trkseg></trk></gpx>\n"
#define GPX_CLOSING_LEN          22

static volatile bool s_track_log_enabled = false;
static volatile ui_track_log_status_t s_track_log_status = UI_TRACK_LOG_OFF;
static volatile uint32_t s_track_log_elapsed_sec = 0;

static void track_log_save_nvs_state(bool active)
{
    nvs_handle_t h;
    if (nvs_open(TRACK_LOG_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, TRACK_LOG_NVS_KEY, active ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool track_log_load_nvs_state(void)
{
    nvs_handle_t h;
    uint8_t val = 0;
    if (nvs_open(TRACK_LOG_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, TRACK_LOG_NVS_KEY, &val);
        nvs_close(h);
    }
    return (val != 0);
}

static void on_ui_track_log_ctrl(bool start)
{
    s_track_log_enabled = start;
    track_log_save_nvs_state(start);
    if (!start) {
        s_track_log_status = UI_TRACK_LOG_OFF;
        s_track_log_elapsed_sec = 0;
    } else if (!s_sdcard_mounted) {
        s_track_log_status = UI_TRACK_LOG_NO_CARD;
    } else {
        s_track_log_status = UI_TRACK_LOG_WAIT_FIX;
    }
}

/* Standard Krasovsky 1940 ellipsoid parameters for WGS-84 -> GCJ-02 conversion */
static const double kPi = 3.1415926535897932384626;
static const double kKrasovskyA = 6378245.0;
static const double kKrasovskyEE = 0.00669342162296594323;

static bool is_out_of_china(double lat, double lon)
{
    if (lon < 72.004 || lon > 137.8347) {
        return true;
    }
    if (lat < 0.8293 || lat > 55.8271) {
        return true;
    }
    return false;
}

static double transform_lat(double x, double y)
{
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * sqrt(fabs(x));
    ret += (20.0 * sin(6.0 * x * kPi) + 20.0 * sin(2.0 * x * kPi)) * 2.0 / 3.0;
    ret += (20.0 * sin(y * kPi) + 40.0 * sin(y / 3.0 * kPi)) * 2.0 / 3.0;
    ret += (160.0 * sin(y / 12.0 * kPi) + 320.0 * sin(y * kPi / 30.0)) * 2.0 / 3.0;
    return ret;
}

static double transform_lon(double x, double y)
{
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * sqrt(fabs(x));
    ret += (20.0 * sin(6.0 * x * kPi) + 20.0 * sin(2.0 * x * kPi)) * 2.0 / 3.0;
    ret += (20.0 * sin(x * kPi) + 40.0 * sin(x / 3.0 * kPi)) * 2.0 / 3.0;
    ret += (150.0 * sin(x / 12.0 * kPi) + 300.0 * sin(x / 30.0 * kPi)) * 2.0 / 3.0;
    return ret;
}

static void wgs84_to_gcj02(double wgs_lat, double wgs_lon, double *gcj_lat, double *gcj_lon)
{
    if (is_out_of_china(wgs_lat, wgs_lon)) {
        *gcj_lat = wgs_lat;
        *gcj_lon = wgs_lon;
        return;
    }
    double d_lat = transform_lat(wgs_lon - 105.0, wgs_lat - 35.0);
    double d_lon = transform_lon(wgs_lon - 105.0, wgs_lat - 35.0);
    double rad_lat = wgs_lat / 180.0 * kPi;
    double magic = sin(rad_lat);
    magic = 1.0 - kKrasovskyEE * magic * magic;
    double sqrt_magic = sqrt(magic);
    d_lat = (d_lat * 180.0) / ((kKrasovskyA * (1.0 - kKrasovskyEE)) / (magic * sqrt_magic) * kPi);
    d_lon = (d_lon * 180.0) / (kKrasovskyA / sqrt_magic * cos(rad_lat) * kPi);
    *gcj_lat = wgs_lat + d_lat;
    *gcj_lon = wgs_lon + d_lon;
}

static bool is_track_log_filename(const char *name, const char **out_ts_part)
{
    size_t len = strlen(name);
    if (len < 5) {
        return false;
    }
    const char *ext = name + len - 4;
    if (strcasecmp(ext, ".gpx") != 0 && strcasecmp(ext, ".gps") != 0) {
        return false;
    }
    if ((name[0] == 'W' || name[0] == 'G' || name[0] == 'w' || name[0] == 'g') && isdigit((unsigned char)name[1])) {
        *out_ts_part = name + 1;
    } else {
        *out_ts_part = name;
    }
    return true;
}

/* Delete the oldest trajectory log file(s) in FIFO order (by timestamp in filename) */
static bool track_log_delete_oldest_fifo(const char *exclude_w, const char *exclude_g)
{
    DIR *dir = opendir(BSP_SD_MOUNT_POINT);
    if (!dir) {
        return false;
    }

    char oldest_ts[64] = {0};
    char oldest_name[260] = {0};
    struct dirent *ent;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        const char *ts_part = NULL;
        if (!is_track_log_filename(ent->d_name, &ts_part)) {
            continue;
        }

        char full_path[320];
        snprintf(full_path, sizeof(full_path), "%s/%s", BSP_SD_MOUNT_POINT, ent->d_name);
        if ((exclude_w && strcmp(full_path, exclude_w) == 0) ||
            (exclude_g && strcmp(full_path, exclude_g) == 0)) {
            continue;
        }

        if (oldest_name[0] == '\0' || strcmp(ts_part, oldest_ts) < 0) {
            snprintf(oldest_ts, sizeof(oldest_ts), "%.60s", ts_part);
            snprintf(oldest_name, sizeof(oldest_name), "%s", ent->d_name);
        }
    }
    closedir(dir);

    if (oldest_name[0] == '\0') {
        return false;
    }

    char del_path[320];
    snprintf(del_path, sizeof(del_path), "%s/%s", BSP_SD_MOUNT_POINT, oldest_name);
    ESP_LOGW(TAG, "FIFO space cleanup: removing oldest track file %s", del_path);
    unlink(del_path);

    /* If it was a W/G paired file, also remove its twin with the same timestamp */
    if ((oldest_name[0] == 'W' || oldest_name[0] == 'G') && isdigit((unsigned char)oldest_name[1])) {
        char twin_name[260];
        snprintf(twin_name, sizeof(twin_name), "%s", oldest_name);
        twin_name[0] = (oldest_name[0] == 'W') ? 'G' : 'W';
        snprintf(del_path, sizeof(del_path), "%s/%s", BSP_SD_MOUNT_POINT, twin_name);
        if ((!exclude_w || strcmp(del_path, exclude_w) != 0) &&
            (!exclude_g || strcmp(del_path, exclude_g) != 0)) {
            if (unlink(del_path) == 0) {
                ESP_LOGW(TAG, "FIFO space cleanup: removed paired track file %s", del_path);
            }
        }
    }
    return true;
}

static void track_log_ensure_space_fifo(const char *exclude_w, const char *exclude_g)
{
    for (int attempt = 0; attempt < 16; attempt++) {
        uint64_t total_bytes = 0, free_bytes = 0;
        if (esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total_bytes, &free_bytes) != ESP_OK) {
            break;
        }
        s_sdcard_total_mb = total_bytes / (1024ULL * 1024ULL);
        s_sdcard_free_mb = free_bytes / (1024ULL * 1024ULL);
        if (free_bytes >= TRACK_LOG_MIN_FREE_BYTES) {
            break;
        }
        if (!track_log_delete_oldest_fifo(exclude_w, exclude_g)) {
            break;
        }
    }
}

static bool track_log_open_pair(const gps_state_t *gps,
                                uint32_t boot_seq,
                                FILE **out_fp_w,
                                FILE **out_fp_g,
                                long *out_off_w,
                                long *out_off_g,
                                char *w_path_buf,
                                size_t w_path_sz,
                                char *g_path_buf,
                                size_t g_path_sz)
{
    char ts_base[32];
    if (gps->date_valid && gps->time_valid) {
        snprintf(ts_base, sizeof(ts_base), "%04d%02d%02d-%02d%02d%02d",
                 gps->local_year, gps->local_month, gps->local_day,
                 gps->local_hour, gps->local_minute, gps->local_second);
    } else if (gps->time_valid) {
        snprintf(ts_base, sizeof(ts_base), "20260101-%02d%02d%02d",
                 gps->local_hour, gps->local_minute, gps->local_second);
    } else {
        uint32_t s = boot_seq;
        snprintf(ts_base, sizeof(ts_base), "00000000-%02u%02u%02u",
                 (unsigned)((s / 3600U) % 24U),
                 (unsigned)((s / 60U) % 60U),
                 (unsigned)(s % 60U));
    }

    snprintf(w_path_buf, w_path_sz, "%s/W%s.gpx", BSP_SD_MOUNT_POINT, ts_base);
    snprintf(g_path_buf, g_path_sz, "%s/G%s.gpx", BSP_SD_MOUNT_POINT, ts_base);

    /* Avoid truncating an existing file if a rapid reboot occurs within the same second */
    struct stat st_chk;
    if (stat(w_path_buf, &st_chk) == 0 || stat(g_path_buf, &st_chk) == 0) {
        for (int suf = 1; suf <= 99; ++suf) {
            snprintf(w_path_buf, w_path_sz, "%s/W%s_%d.gpx", BSP_SD_MOUNT_POINT, ts_base, suf);
            snprintf(g_path_buf, g_path_sz, "%s/G%s_%d.gpx", BSP_SD_MOUNT_POINT, ts_base, suf);
            if (stat(w_path_buf, &st_chk) != 0 && stat(g_path_buf, &st_chk) != 0) {
                break;
            }
        }
    }

    /* Ensure enough free space on TF card before creating new segment files */
    track_log_ensure_space_fifo(NULL, NULL);

    FILE *fw = fopen(w_path_buf, "w+");
    if (!fw) {
        /* Try freeing oldest files once in case root/clusters were full */
        if (track_log_delete_oldest_fifo(NULL, NULL)) {
            fw = fopen(w_path_buf, "w+");
        }
    }
    if (!fw) {
        ESP_LOGE(TAG, "Failed to open WGS-84 track file: %s", w_path_buf);
        return false;
    }

    FILE *fg = fopen(g_path_buf, "w+");
    if (!fg) {
        fclose(fw);
        unlink(w_path_buf);
        ESP_LOGE(TAG, "Failed to open GCJ-02 track file: %s", g_path_buf);
        return false;
    }

    char hdr_w[256];
    int nw = snprintf(hdr_w, sizeof(hdr_w),
                      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<gpx version=\"1.1\" creator=\"ESP32-S3-LC76G\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
                      "<trk><name>W%s (WGS-84)</name><trkseg>\n",
                      ts_base);

    char hdr_g[256];
    int ng = snprintf(hdr_g, sizeof(hdr_g),
                      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<gpx version=\"1.1\" creator=\"ESP32-S3-LC76G\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
                      "<trk><name>G%s (GCJ-02)</name><trkseg>\n",
                      ts_base);

    fwrite(hdr_w, 1, (size_t)nw, fw);
    fwrite(GPX_CLOSING_TAGS, 1, GPX_CLOSING_LEN, fw);
    fflush(fw);
    fsync(fileno(fw));
    fseek(fw, (long)nw, SEEK_SET);

    fwrite(hdr_g, 1, (size_t)ng, fg);
    fwrite(GPX_CLOSING_TAGS, 1, GPX_CLOSING_LEN, fg);
    fflush(fg);
    fsync(fileno(fg));
    fseek(fg, (long)ng, SEEK_SET);

    *out_fp_w = fw;
    *out_fp_g = fg;
    *out_off_w = (long)nw;
    *out_off_g = (long)ng;

    ESP_LOGI(TAG, "Track log segment started: %s and %s", w_path_buf, g_path_buf);
    return true;
}

static bool track_log_append_point(FILE *fp, long *io_offset, double lat, double lon,
                                   const gps_state_t *gps, bool commit_xml_and_flush)
{
    char iso_time[40];
    if (gps->date_valid && gps->time_valid) {
        snprintf(iso_time, sizeof(iso_time), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 gps->utc_year, gps->utc_month, gps->utc_day,
                 gps->utc_hour, gps->utc_minute, gps->utc_second);
    } else if (gps->time_valid) {
        snprintf(iso_time, sizeof(iso_time), "2026-01-01T%02d:%02d:%02dZ",
                 gps->utc_hour, gps->utc_minute, gps->utc_second);
    } else {
        snprintf(iso_time, sizeof(iso_time), "2026-01-01T00:00:00Z");
    }

    char pt_buf[448];
    int pt_len = snprintf(pt_buf, sizeof(pt_buf),
                          "<trkpt lat=\"%.7f\" lon=\"%.7f\">"
                          "<ele>%.1f</ele>"
                          "<time>%s</time>"
                          "<sat>%d</sat>"
                          "<hdop>%.2f</hdop>"
                          "<vdop>%.2f</vdop>"
                          "<pdop>%.2f</pdop>"
                          "<extensions><speed>%.2f</speed><course>%.1f</course></extensions>"
                          "</trkpt>\n",
                          lat, lon,
                          gps->smoothed_altitude_m,
                          iso_time,
                          gps->satellites_used,
                          gps->hdop,
                          gps->vdop,
                          gps->pdop,
                          gps->speed_kmh,
                          gps->course_deg);
    if (pt_len <= 0 || (size_t)pt_len >= sizeof(pt_buf)) {
        return false;
    }

    if (fwrite(pt_buf, 1, (size_t)pt_len, fp) != (size_t)pt_len) {
        return false;
    }
    *io_offset += (long)pt_len;

    /* Batch FAT sector writes every 5 seconds (reducing SD flash wear by 80% while maintaining XML closure) */
    if (commit_xml_and_flush) {
        if (fwrite(GPX_CLOSING_TAGS, 1, GPX_CLOSING_LEN, fp) != GPX_CLOSING_LEN) {
            return false;
        }
        if (fflush(fp) != 0) {
            return false;
        }
        fsync(fileno(fp));
        if (fseek(fp, *io_offset, SEEK_SET) != 0) {
            return false;
        }
    }

    return true;
}

static void track_log_close_pair(FILE **fp_w, FILE **fp_g, long off_w, long off_g)
{
    if (*fp_w) {
        fseek(*fp_w, off_w, SEEK_SET);
        fwrite(GPX_CLOSING_TAGS, 1, GPX_CLOSING_LEN, *fp_w);
        fflush(*fp_w);
        fsync(fileno(*fp_w));
        fclose(*fp_w);
        *fp_w = NULL;
    }
    if (*fp_g) {
        fseek(*fp_g, off_g, SEEK_SET);
        fwrite(GPX_CLOSING_TAGS, 1, GPX_CLOSING_LEN, *fp_g);
        fflush(*fp_g);
        fsync(fileno(*fp_g));
        fclose(*fp_g);
        *fp_g = NULL;
    }
}

static void track_logger_task(void *arg)
{
    (void)arg;
    FILE *fp_w = NULL;
    FILE *fp_g = NULL;
    long off_w = 0;
    long off_g = 0;
    char cur_w_path[128] = {0};
    char cur_g_path[128] = {0};
    uint32_t segment_secs = 0;
    uint32_t total_rec_secs = 0;
    uint32_t boot_seq = 0;

    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
        boot_seq++;

        /* Pause and close files safely if TF card formatting is underway */
        if (s_sdcard_formatting) {
            if (fp_w || fp_g) {
                if (s_sdcard_mutex) xSemaphoreTake(s_sdcard_mutex, portMAX_DELAY);
                track_log_close_pair(&fp_w, &fp_g, off_w, off_g);
                if (s_sdcard_mutex) xSemaphoreGive(s_sdcard_mutex);
            }
            continue;
        }

        if (!s_track_log_enabled) {
            if (fp_w || fp_g) {
                if (s_sdcard_mutex) xSemaphoreTake(s_sdcard_mutex, portMAX_DELAY);
                track_log_close_pair(&fp_w, &fp_g, off_w, off_g);
                if (s_sdcard_mutex) xSemaphoreGive(s_sdcard_mutex);
                ESP_LOGI(TAG, "Track logger stopped by user");
            }
            segment_secs = 0;
            total_rec_secs = 0;
            s_track_log_elapsed_sec = 0;
            s_track_log_status = UI_TRACK_LOG_OFF;
            continue;
        }

        if (!s_sdcard_mounted) {
            if (fp_w || fp_g) {
                if (s_sdcard_mutex) xSemaphoreTake(s_sdcard_mutex, portMAX_DELAY);
                track_log_close_pair(&fp_w, &fp_g, off_w, off_g);
                if (s_sdcard_mutex) xSemaphoreGive(s_sdcard_mutex);
            }
            s_track_log_status = UI_TRACK_LOG_NO_CARD;
            continue;
        }

        gps_state_t gps;
        gps_lc76g_get_state(&gps);

        if (s_sdcard_mutex) {
            xSemaphoreTake(s_sdcard_mutex, portMAX_DELAY);
        }

        /* Requirement: Wait for valid 3D low-VDOP GPS fix before starting recording to avoid unfixed drift */
        if (!fp_w || !fp_g) {
            if (!gps.has_fix || !gps.alt_quality_ok) {
                if (s_sdcard_mutex) xSemaphoreGive(s_sdcard_mutex);
                s_track_log_status = UI_TRACK_LOG_WAIT_FIX;
                continue;
            }
            if (!track_log_open_pair(&gps, boot_seq, &fp_w, &fp_g, &off_w, &off_g,
                                     cur_w_path, sizeof(cur_w_path), cur_g_path, sizeof(cur_g_path))) {
                if (s_sdcard_mutex) xSemaphoreGive(s_sdcard_mutex);
                s_track_log_status = UI_TRACK_LOG_NO_CARD;
                continue;
            }
            segment_secs = 0;
        }

        /* Hourly file rotation (3600 seconds per segment) */
        if (segment_secs >= TRACK_LOG_SEGMENT_SECS) {
            track_log_close_pair(&fp_w, &fp_g, off_w, off_g);
            if (!gps.has_fix || !gps.alt_quality_ok) {
                if (s_sdcard_mutex) xSemaphoreGive(s_sdcard_mutex);
                s_track_log_status = UI_TRACK_LOG_WAIT_FIX;
                continue;
            }
            if (!track_log_open_pair(&gps, boot_seq, &fp_w, &fp_g, &off_w, &off_g,
                                     cur_w_path, sizeof(cur_w_path), cur_g_path, sizeof(cur_g_path))) {
                if (s_sdcard_mutex) xSemaphoreGive(s_sdcard_mutex);
                s_track_log_status = UI_TRACK_LOG_NO_CARD;
                continue;
            }
            segment_secs = 0;
        }

        /* Only write valid low-VDOP 3D fixed points to prevent drift spikes during temporary tunnel/obstruction */
        if (gps.has_fix && gps.alt_quality_ok) {
            /* Periodic FIFO free space check every 30 seconds */
            if ((segment_secs % 30U) == 0U) {
                track_log_ensure_space_fifo(cur_w_path, cur_g_path);
            }

            double gcj_lat = gps.latitude;
            double gcj_lon = gps.longitude;
            wgs84_to_gcj02(gps.latitude, gps.longitude, &gcj_lat, &gcj_lon);

            /* Flush closing XML tags & sync FAT directory entry every 5s (and on first point of segment) */
            bool flush_now = (segment_secs == 0U) || ((segment_secs % 5U) == 4U);
            bool ok_w = track_log_append_point(fp_w, &off_w, gps.latitude, gps.longitude, &gps, flush_now);
            bool ok_g = track_log_append_point(fp_g, &off_g, gcj_lat, gcj_lon, &gps, flush_now);

            if (!ok_w || !ok_g) {
                /* Attempt FIFO cleanup on write error and retry on next second */
                ESP_LOGW(TAG, "Track log write error, invoking FIFO cleanup");
                track_log_ensure_space_fifo(cur_w_path, cur_g_path);
            }
        }

        if (s_sdcard_mutex) {
            xSemaphoreGive(s_sdcard_mutex);
        }

        segment_secs++;
        total_rec_secs++;
        s_track_log_elapsed_sec = total_rec_secs;
        s_track_log_status = UI_TRACK_LOG_RECORDING;
    }
}

static void ui_update_task(void *arg)
{
    (void)arg;
    uint32_t loop_cnt = 0;
    bool cached_wifi_conn = false;
    int cached_wifi_pct = 0;

    while (1) {
        /* Handle deferred SoftAP + HTTP portal / File Manager start/stop outside LVGL display lock */
        if (s_portal_cmd_pending) {
            s_portal_cmd_pending = false;
            ap_http_set_mode_impl(s_target_ap_http_mode);
        }

        /* Handle auto-closing QR modal 1.8s after new Wi-Fi connects and gets IP */
        if (s_portal_active && s_ap_http_mode == AP_HTTP_MODE_WIFI_CFG && s_portal_auto_close_tick != 0) {
            if ((int32_t)(xTaskGetTickCount() - s_portal_auto_close_tick) >= 0) {
                s_portal_auto_close_tick = 0;
                s_pending_qr_close = true;
                s_pending_qr_update = true;
                ap_http_set_mode_impl(AP_HTTP_MODE_OFF);
            }
        }

        /* Exponential-backoff Wi-Fi auto-reconnect when SoftAP is off and STA is disconnected:
         * avoids continuous 2.4GHz channel-scan RF interference & power ripple during outdoor GNSS use */
        if (!s_portal_active && !s_wifi_connected && !s_sta_connecting) {
            TickType_t now = xTaskGetTickCount();
            uint32_t backoff_ms = 5000U;
            if (s_wifi_disconnect_streak >= 6) {
                backoff_ms = 60000U;
            } else if (s_wifi_disconnect_streak >= 4) {
                backoff_ms = 30000U;
            } else if (s_wifi_disconnect_streak >= 2) {
                backoff_ms = 15000U;
            }
            if ((uint32_t)(now - s_last_reconnect_tick) >= pdMS_TO_TICKS(backoff_ms)) {
                wifi_sta_try_reconnect(false);
            }
        }

        gps_state_t gps_snap;
        gps_lc76g_get_state(&gps_snap);

        /* Restore full Wi-Fi TX power (19.5 dBm) once GNSS fix is acquired or SoftAP is active */
        if (!s_wifi_full_tx_power && (gps_snap.has_fix || s_portal_active)) {
            esp_wifi_set_max_tx_power(78);
            s_wifi_full_tx_power = true;
        }

        /* Sample Wi-Fi RSSI at 1 Hz (every 10 loops at 100ms) instead of 10 Hz */
        if ((loop_cnt % 10U) == 0U || !s_wifi_connected) {
            cached_wifi_pct = get_wifi_signal_percent(&cached_wifi_conn);
        }

        if (bsp_display_lock(50) == ESP_OK) {
            if (s_pending_qr_update) {
                s_pending_qr_update = false;
                ui_framework_set_wifi_qr_stage(s_pending_qr_stage, s_pending_qr_close);
                s_pending_qr_close = false;
            }
            ui_framework_set_track_log_state(s_track_log_enabled, s_track_log_status, s_track_log_elapsed_sec);
            ui_framework_update(&gps_snap, cached_wifi_pct, cached_wifi_conn);
            bsp_display_unlock();
        }

        loop_cnt++;
        if (loop_cnt % 30 == 0) {
            ESP_LOGI(TAG, "TF Diag: mounted=%d, name=%s, total=%lluMB, free=%lluMB, rw_ok=%d",
                     s_sdcard_mounted, s_sdcard_name,
                     (unsigned long long)s_sdcard_total_mb,
                     (unsigned long long)s_sdcard_free_mb,
                     s_sdcard_rw_ok);
        }

        /* 10 Hz (100ms) UI telemetry refresh eliminates 250ms display quantization lag */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    s_default_vprintf = esp_log_set_vprintf(nonblocking_usb_vprintf);
    s_sdcard_mutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "  ESP32-S3 1.75\" AMOLED Multi-Page App Starting ");
    ESP_LOGI(TAG, "================================================");

    /* 1. Initialize NVS (preserving any stored calibration or Wi-Fi config) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. Initialize I2C, QSPI CO5300 AMOLED (466x466), CST9217 Touch, and LVGL task FIRST to secure internal DMA memory */
    ESP_LOGI(TAG, "Starting AMOLED display & touch subsystem...");
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "Failed to start display!");
        return;
    }
    bsp_display_backlight_on();

    /* 3. Build Multi-Page UI Framework + Top-Layer Signal Arcs + Page 1..3 */
    if (bsp_display_lock(-1) == ESP_OK) {
        ui_framework_init();
        ui_framework_set_tf_format_cb(on_ui_tf_format_requested);
        ui_framework_set_wifi_portal_cb(on_ui_wifi_portal_ctrl);
        ui_framework_set_file_mgr_cb(on_ui_file_mgr_ctrl);
        ui_framework_set_track_log_cb(on_ui_track_log_ctrl);
        bsp_display_unlock();
    }

    /* 4. Mount TF / microSD card and verify read/write */
    sdcard_init_and_verify();

    /* 5. Restore trajectory logger state from NVS (power-loss protection) */
    if (track_log_load_nvs_state()) {
        s_track_log_enabled = true;
        s_track_log_status = s_sdcard_mounted ? UI_TRACK_LOG_WAIT_FIX : UI_TRACK_LOG_NO_CARD;
        ESP_LOGI(TAG, "Power-loss recovery: resuming trajectory logger (wait_fix=%d)", s_sdcard_mounted);
        if (bsp_display_lock(200) == ESP_OK) {
            ui_framework_set_track_log_state(true, s_track_log_status, 0);
            bsp_display_unlock();
        }
    }

    /* 6. Initialize Wi-Fi Station for RSSI signal monitoring */
    wifi_monitor_init();

    /* 7. Start Quectel LC76G GNSS receiver over I2C (Core 0) */
    gps_lc76g_start();

    /* 8. Start 1Hz GNSS Trajectory Logger task (Core 1) */
    xTaskCreatePinnedToCore(track_logger_task, "track_log", 6144, NULL, 3, NULL, 1);

    /* 9. Start periodic UI telemetry refresh task (4 Hz on Core 1) */
    xTaskCreatePinnedToCore(ui_update_task, "ui_update", 6144, NULL, 2, NULL, 1);
}
