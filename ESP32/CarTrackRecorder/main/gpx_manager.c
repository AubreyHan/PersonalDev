#include "gpx_manager.h"
#include "bsp_board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "GPX_MANAGER";
static FILE *s_gpx_file = NULL;
static uint32_t s_points_in_file = 0;
static const char *NVS_NAMESPACE = "gpx_state";
static const char *NVS_KEY_ACTIVE_FILE = "act_file";
static const char *NVS_KEY_MODE = "rec_mode";

static void write_gpx_header(FILE *f, const char *track_name)
{
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<gpx version=\"1.1\" creator=\"ESP32-S3-Car-Track-Recorder\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n");
    fprintf(f, "  <trk>\n");
    fprintf(f, "    <name>%s</name>\n", track_name);
    fprintf(f, "    <trkseg>\n");
    fflush(f);
}

static void close_gpx_file(FILE *f)
{
    if (!f) return;
    fprintf(f, "    </trkseg>\n");
    fprintf(f, "  </trk>\n");
    fprintf(f, "</gpx>\n");
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}

// 检查并执行 FIFO 循环淘汰
esp_err_t gpx_manager_check_fifo(void)
{
    bsp_sdcard_update_space();
    uint64_t free_mb = g_app_state.sd_free_bytes / (1024 * 1024);

    if (free_mb >= SD_MIN_FREE_SPACE_MB) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "SD Card space low (%llu MB < %d MB). Triggering FIFO cleanup...", free_mb, SD_MIN_FREE_SPACE_MB);

    DIR *dir = opendir(GPX_STORAGE_DIR);
    if (!dir) return ESP_FAIL;

    struct dirent *entry;
    char oldest_file[256] = {0};

    // 寻找字母序最小的 gpx 文件 (即时间戳最早的最旧文件)
    while ((entry = readdir(dir)) != NULL) {
        if ((entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) && entry->d_name[0] != '.' && strstr(entry->d_name, ".gpx")) {
            // 跳过当前正在写入的文件
            char full_path[320];
            snprintf(full_path, sizeof(full_path), "%s/%s", GPX_STORAGE_DIR, entry->d_name);
            if (strcmp(full_path, g_app_state.current_gpx_path) == 0) {
                continue;
            }

            if (oldest_file[0] == '\0' || strcmp(entry->d_name, oldest_file) < 0) {
                snprintf(oldest_file, sizeof(oldest_file), "%s", entry->d_name);
            }
        }
    }
    closedir(dir);

    if (oldest_file[0] != '\0') {
        char del_path[320];
        snprintf(del_path, sizeof(del_path), "%s/%s", GPX_STORAGE_DIR, oldest_file);
        ESP_LOGI(TAG, "FIFO: Removing oldest GPX file: %s", del_path);
        unlink(del_path);
        bsp_sdcard_update_space();
    }

    return ESP_OK;
}

// 创建并打开新的切片文件
static esp_err_t open_new_slice_file(void)
{
    time_t now_time = time(NULL);
    struct tm t;
    localtime_r(&now_time, &t);

    char filename[96];
    snprintf(filename, sizeof(filename), "%04d%02d%02d-%02d%02d%02d.gpx",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);

    snprintf(g_app_state.current_gpx_path, sizeof(g_app_state.current_gpx_path), "%s/%s", GPX_STORAGE_DIR, filename);

    s_gpx_file = fopen(g_app_state.current_gpx_path, "w+");
    if (!s_gpx_file) {
        ESP_LOGE(TAG, "Failed to open GPX file: %s", g_app_state.current_gpx_path);
        return ESP_FAIL;
    }

    write_gpx_header(s_gpx_file, filename);
    s_points_in_file = 0;
    ESP_LOGI(TAG, "New GPX file created: %s", g_app_state.current_gpx_path);

    // 若为持续模式，持久化当前文件路径到 NVS
    if (g_app_state.record_mode == REC_MODE_CONTINUOUS) {
        nvs_handle_t nvs_h;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_h) == ESP_OK) {
            nvs_set_str(nvs_h, NVS_KEY_ACTIVE_FILE, g_app_state.current_gpx_path);
            nvs_set_u8(nvs_h, NVS_KEY_MODE, (uint8_t)REC_MODE_CONTINUOUS);
            nvs_commit(nvs_h);
            nvs_close(nvs_h);
        }
    }

    return ESP_OK;
}

esp_err_t gpx_manager_start(record_mode_t mode)
{
    if (!g_app_state.sd_mounted) {
        ESP_LOGE(TAG, "Cannot start GPX recording: SD card not mounted.");
        return ESP_ERR_INVALID_STATE;
    }

    // 检查并执行 FIFO 空间检查
    gpx_manager_check_fifo();

    if (mode == REC_MODE_CONTINUOUS) {
        // 尝试从 NVS 恢复未完成的持续记录文件
        nvs_handle_t nvs_h;
        char prev_file[320] = {0};
        size_t len = sizeof(prev_file);
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_h) == ESP_OK) {
            if (nvs_get_str(nvs_h, NVS_KEY_ACTIVE_FILE, prev_file, &len) == ESP_OK) {
                // 检查该文件在 SD 卡是否存在
                struct stat st;
                if (stat(prev_file, &st) == 0 && st.st_size > 0) {
                    // 找到了断电前的原文件，以读写模式打开
                    s_gpx_file = fopen(prev_file, "r+");
                    if (s_gpx_file) {
                        fseek(s_gpx_file, 0, SEEK_END);
                        long fsize = ftell(s_gpx_file);
                        long search_len = (fsize > 256) ? 256 : fsize;
                        fseek(s_gpx_file, fsize - search_len, SEEK_SET);
                        char tail_buf[257] = {0};
                        fread(tail_buf, 1, search_len, s_gpx_file);

                        // 检查尾部是否包含闭合标签 </trkseg> 或 </gpx>
                        char *pos_trkseg = strstr(tail_buf, "</trkseg>");
                        char *pos_gpx = strstr(tail_buf, "</gpx>");

                        if (pos_trkseg || pos_gpx) {
                            // 先前已被正常封存闭合，回退到 </trkseg> 处，截断并开启新段追加
                            char *target = pos_trkseg ? pos_trkseg : pos_gpx;
                            long offset = (tail_buf + search_len) - target;
                            long new_len = fsize - offset;
                            ftruncate(fileno(s_gpx_file), new_len);
                            fseek(s_gpx_file, new_len, SEEK_SET);
                            fprintf(s_gpx_file, "    <trkseg>\n");
                            fflush(s_gpx_file);
                        } else {
                            // 未闭合 (突发掉电)，文件指针定位于文件末尾直接继续记录
                            fseek(s_gpx_file, 0, SEEK_END);
                        }
                        snprintf(g_app_state.current_gpx_path, sizeof(g_app_state.current_gpx_path), "%s", prev_file);
                        ESP_LOGI(TAG, "Resumed continuous GPX file: %s", prev_file);
                        nvs_close(nvs_h);
                        return ESP_OK;
                    }
                }
            }
            nvs_close(nvs_h);
        }
    }

    // 分段模式，或者未能恢复持续旧文件：直接新建
    return open_new_slice_file();
}

esp_err_t gpx_manager_feed_point(const gps_data_t *gps)
{
    if (!s_gpx_file || !gps || !gps->valid_fix) return ESP_ERR_INVALID_STATE;

    // 格式化 UTC 时间戳 (ISO8601)
    char time_iso[80];
    snprintf(time_iso, sizeof(time_iso), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             gps->utc_time.tm_year + 1900, gps->utc_time.tm_mon + 1, gps->utc_time.tm_mday,
             gps->utc_time.tm_hour, gps->utc_time.tm_min, gps->utc_time.tm_sec);

    // 写入标准 <trkpt>
    fprintf(s_gpx_file, "      <trkpt lat=\"%.6f\" lon=\"%.6f\">\n", gps->latitude, gps->longitude);
    fprintf(s_gpx_file, "        <ele>%.1f</ele>\n", gps->altitude);
    fprintf(s_gpx_file, "        <time>%s</time>\n", time_iso);
    fprintf(s_gpx_file, "        <speed>%.2f</speed>\n", gps->speed_kmh / 3.6f); // 换算为 m/s
    fprintf(s_gpx_file, "      </trkpt>\n");

    s_points_in_file++;

    // 断电保护：每次写入均刷新文件系统缓冲区
    fflush(s_gpx_file);

    // 每 10 个点调用一次底层 fsync，平衡写入安全与 Flash/SD 寿命
    if (s_points_in_file % 10 == 0) {
        fsync(fileno(s_gpx_file));
    }

    // 1 小时自动切片逻辑 (3600 秒)
    if (s_points_in_file >= GPX_AUTO_SLICE_SECONDS) {
        ESP_LOGI(TAG, "Current GPX slice full (1 hour reached). Auto slicing new file...");
        close_gpx_file(s_gpx_file);
        s_gpx_file = NULL;

        // 检查空间并开启下一个文件
        gpx_manager_check_fifo();
        open_new_slice_file();
    }

    return ESP_OK;
}

esp_err_t gpx_manager_stop(void)
{
    if (s_gpx_file) {
        close_gpx_file(s_gpx_file);
        s_gpx_file = NULL;
        ESP_LOGI(TAG, "GPX file closed and safely synced.");
    }

    // 清除 NVS 中可能保存的持续记录标记
    nvs_handle_t nvs_h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_h) == ESP_OK) {
        nvs_erase_key(nvs_h, NVS_KEY_ACTIVE_FILE);
        nvs_set_u8(nvs_h, NVS_KEY_MODE, (uint8_t)REC_MODE_NONE);
        nvs_commit(nvs_h);
        nvs_close(nvs_h);
    }

    g_app_state.current_gpx_path[0] = '\0';
    return ESP_OK;
}

esp_err_t gpx_manager_init(void)
{
    return ESP_OK;
}
