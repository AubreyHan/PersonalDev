#include "gps_engine.h"
#include "gpx_manager.h"
#include "bsp_board.h"
#include "bsp/esp-bsp.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_io_expander.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "GPS_ENGINE";
static SemaphoreHandle_t s_gps_mutex = NULL;
static uint32_t s_last_alt_update_ms = 0;
static i2c_master_dev_handle_t s_lc76g_dev_wr = NULL;
static i2c_master_dev_handle_t s_lc76g_dev_rd = NULL;

static double parse_nmea_coord(const char *str, char dir)
{
    if (!str || strlen(str) < 4) return 0.0;
    double raw = atof(str);
    int degrees = (int)(raw / 100.0);
    double minutes = raw - (degrees * 100.0);
    double dec = degrees + (minutes / 60.0);
    if (dir == 'S' || dir == 'W') dec = -dec;
    return dec;
}

// 智能时区判定 (中国国境判定 + 经度标准时区)
static int calculate_timezone(double lat, double lon)
{
    // 中国疆域粗略边界: 纬度 3.86°N ~ 53.55°N，经度 73.66°E ~ 135.05°E
    if (lat >= 3.8 && lat <= 54.0 && lon >= 73.0 && lon <= 136.0) {
        return 8; // 北京时间 UTC+8
    }
    // 国际标准时区
    return (int)round(lon / 15.0);
}

// NMEA 逐行解析 (采用 strsep 精准保留空字段分隔，避免 strtok 跳过空值导致字段错位)
static void parse_nmea_line(const char *line)
{
    if (!line || line[0] != '$' || strlen(line) < 6) return;

    // 打印关键语句以便用户了解当前天空卫星接收情况
    if (strncmp(line + 3, "RMC", 3) == 0 || strncmp(line + 3, "GGA", 3) == 0 || strncmp(line + 3, "GSV", 3) == 0) {
        ESP_LOGI(TAG, "NMEA: %s", line);
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "%s", line);
    char *p = buf;
    char *token = NULL;

    if (strncmp(line + 3, "RMC", 3) == 0) {
        int field = 0;
        char status = 'V';
        char time_str[16] = {0};
        char lat_str[16] = {0};
        char lat_dir = 'N';
        char lon_str[16] = {0};
        char lon_dir = 'E';
        char spd_str[16] = {0};
        char trk_str[16] = {0};
        char date_str[16] = {0};

        while ((token = strsep(&p, ",")) != NULL) {
            switch (field) {
                case 1: snprintf(time_str, sizeof(time_str), "%s", token); break;
                case 2: if (token[0]) status = token[0]; break;
                case 3: snprintf(lat_str, sizeof(lat_str), "%s", token); break;
                case 4: if (token[0]) lat_dir = token[0]; break;
                case 5: snprintf(lon_str, sizeof(lon_str), "%s", token); break;
                case 6: if (token[0]) lon_dir = token[0]; break;
                case 7: snprintf(spd_str, sizeof(spd_str), "%s", token); break;
                case 8: snprintf(trk_str, sizeof(trk_str), "%s", token); break;
                case 9: snprintf(date_str, sizeof(date_str), "%s", token); break;
                default: break;
            }
            field++;
        }

        if (status == 'A') { // 有效定位
            xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
            g_app_state.gps.valid_fix = true;
            g_app_state.gps.latitude = parse_nmea_coord(lat_str, lat_dir);
            g_app_state.gps.longitude = parse_nmea_coord(lon_str, lon_dir);
            g_app_state.gps.speed_kmh = (float)(atof(spd_str) * 1.852); // 节转公里/小时
            if (strlen(trk_str) > 0) {
                g_app_state.gps.heading_deg = (float)atof(trk_str);
            }

            // 解析时间与日期 (hhmmss & ddmmyy)
            if (strlen(time_str) >= 6 && strlen(date_str) >= 6) {
                int hour = (time_str[0] - '0') * 10 + (time_str[1] - '0');
                int min  = (time_str[2] - '0') * 10 + (time_str[3] - '0');
                int sec  = (time_str[4] - '0') * 10 + (time_str[5] - '0');
                int day  = (date_str[0] - '0') * 10 + (date_str[1] - '0');
                int mon  = (date_str[2] - '0') * 10 + (date_str[3] - '0');
                int yr   = (date_str[4] - '0') * 10 + (date_str[5] - '0') + 2000;

                g_app_state.gps.utc_time.tm_hour = hour;
                g_app_state.gps.utc_time.tm_min  = min;
                g_app_state.gps.utc_time.tm_sec  = sec;
                g_app_state.gps.utc_time.tm_mday = day;
                g_app_state.gps.utc_time.tm_mon  = mon - 1;
                g_app_state.gps.utc_time.tm_year = yr - 1900;

                // 自动计算时区
                g_app_state.gps.timezone_offset = calculate_timezone(g_app_state.gps.latitude, g_app_state.gps.longitude);

                // 计算本地时间
                time_t utc_epoch = mktime(&g_app_state.gps.utc_time);
                time_t local_epoch = utc_epoch + (g_app_state.gps.timezone_offset * 3600);
                localtime_r(&local_epoch, &g_app_state.gps.local_time);

                // 校准 ESP32 系统时钟
                struct timeval tv = { .tv_sec = local_epoch, .tv_usec = 0 };
                settimeofday(&tv, NULL);
            }

            // 检查定位门禁：若处于等待定位待命 (ARMED)，立即自动触发录制！
            if (g_app_state.record_status == REC_STATUS_ARMED) {
                ESP_LOGI(TAG, "GPS Fix acquired! Auto-triggering armed recording (Mode: %d)", g_app_state.record_mode);
                g_app_state.record_status = REC_STATUS_ACTIVE;
                gpx_manager_start(g_app_state.record_mode);
            }

            xSemaphoreGive(s_gps_mutex);
        }
    } else if (strncmp(line + 3, "GGA", 3) == 0) {
        int field = 0;
        int sats = 0;
        float hdop = 0.0f;
        float alt = 0.0f;

        while ((token = strsep(&p, ",")) != NULL) {
            switch (field) {
                case 7: if (token[0]) sats = atoi(token); break;
                case 8: if (token[0]) hdop = (float)atof(token); break;
                case 9: if (token[0]) alt = (float)atof(token); break;
                default: break;
            }
            field++;
        }

        xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
        g_app_state.gps.satellites = sats;
        g_app_state.gps.hdop = hdop;
        g_app_state.gps.altitude = alt;

        // 计算信号强度等级 (0~4格)
        if (!g_app_state.gps.valid_fix || sats < 4) {
            g_app_state.gps.signal_bars = 0;
        } else if (sats >= 12 && (g_app_state.gps.avg_snr >= 35 || (hdop > 0.0f && hdop <= 1.2f))) {
            g_app_state.gps.signal_bars = 4;
        } else if (sats >= 8 && (g_app_state.gps.avg_snr >= 28 || (hdop > 0.0f && hdop <= 2.0f))) {
            g_app_state.gps.signal_bars = 3;
        } else if (sats >= 5) {
            g_app_state.gps.signal_bars = 2;
        } else {
            g_app_state.gps.signal_bars = 1;
        }
        xSemaphoreGive(s_gps_mutex);
    } else if (strncmp(line + 3, "GSV", 3) == 0) {
        int field = 0;
        int snr_sum = 0;
        int snr_cnt = 0;
        while ((token = strsep(&p, ",")) != NULL) {
            if (field == 7 || field == 11 || field == 15 || field == 19) {
                if (token[0]) {
                    int snr = atoi(token);
                    if (snr > 0 && snr < 99) {
                        snr_sum += snr;
                        snr_cnt++;
                    }
                }
            }
            field++;
        }
        if (snr_cnt > 0) {
            xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
            g_app_state.gps.avg_snr = snr_sum / snr_cnt;
            xSemaphoreGive(s_gps_mutex);
        }
    }
}

// 5秒更新一次高度统计与历史曲线
static void update_altitude_stats(void)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - s_last_alt_update_ms >= ALTITUDE_UPDATE_INTERVAL_MS) {
        s_last_alt_update_ms = now;

        xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
        if (g_app_state.gps.valid_fix) {
            float alt = g_app_state.gps.altitude;
            g_app_state.alt_stats.current_alt = alt;

            if (g_app_state.alt_stats.history_count == 0) {
                g_app_state.alt_stats.max_alt = alt;
                g_app_state.alt_stats.min_alt = alt;
            } else {
                if (alt > g_app_state.alt_stats.max_alt) g_app_state.alt_stats.max_alt = alt;
                if (alt < g_app_state.alt_stats.min_alt) g_app_state.alt_stats.min_alt = alt;
            }

            // 写入环形缓冲区
            g_app_state.alt_stats.history[g_app_state.alt_stats.history_head] = alt;
            g_app_state.alt_stats.history_head = (g_app_state.alt_stats.history_head + 1) % ALT_HISTORY_POINTS;
            if (g_app_state.alt_stats.history_count < ALT_HISTORY_POINTS) {
                g_app_state.alt_stats.history_count++;
            }
        }
        xSemaphoreGive(s_gps_mutex);
    }
}

// LC76G I2C 通信及接收任务
static void gps_task(void *pvParameters)
{
    ESP_LOGI(TAG, "LC76G GNSS Task running on Core 0...");

    uint8_t query_cmd[] = { 0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00 };
    uint8_t read_len_buf[4] = {0};

    char line_buf[256];
    int line_idx = 0;
    uint32_t last_gpx_feed_ms = 0;
    uint32_t last_diag_log_ms = 0;
    uint32_t last_success_len = 0;
    esp_err_t last_err = ESP_OK;

    while (1) {
        if (s_lc76g_dev_wr && s_lc76g_dev_rd) {
            // 1. 发送查询数据长度命令到 0x50
            esp_err_t err = i2c_master_transmit(s_lc76g_dev_wr, query_cmd, sizeof(query_cmd), pdMS_TO_TICKS(100));
            last_err = err;
            if (err == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(100));
                // 2. 从 0x54 读取 4 字节数据长度
                err = i2c_master_receive(s_lc76g_dev_rd, read_len_buf, 4, pdMS_TO_TICKS(100));
                last_err = err;
                if (err == ESP_OK) {
                    uint32_t len = (read_len_buf[0]) | (read_len_buf[1] << 8) | (read_len_buf[2] << 16) | (read_len_buf[3] << 24);
                    last_success_len = len;
                    if (len > 0 && len <= 2048) {
                        // 3. 发送带长度的读数据命令到 0x50
                        uint8_t fetch_cmd[8] = { 0x00, 0x20, 0x51, 0xAA, read_len_buf[0], read_len_buf[1], read_len_buf[2], read_len_buf[3] };
                        vTaskDelay(pdMS_TO_TICKS(100));
                        if (i2c_master_transmit(s_lc76g_dev_wr, fetch_cmd, sizeof(fetch_cmd), pdMS_TO_TICKS(100)) == ESP_OK) {
                            vTaskDelay(pdMS_TO_TICKS(100));
                            uint8_t *rx_buf = malloc(len + 1);
                            if (rx_buf) {
                                if (i2c_master_receive(s_lc76g_dev_rd, rx_buf, len, pdMS_TO_TICKS(300)) == ESP_OK) {
                                    rx_buf[len] = '\0';
                                    // 拆分每一行 NMEA 语句
                                    for (uint32_t i = 0; i < len; i++) {
                                        char c = (char)rx_buf[i];
                                        if (c == '\r' || c == '\n') {
                                            if (line_idx > 0) {
                                                line_buf[line_idx] = '\0';
                                                parse_nmea_line(line_buf);
                                                line_idx = 0;
                                            }
                                        } else {
                                            if (line_idx < sizeof(line_buf) - 1) {
                                                line_buf[line_idx++] = c;
                                            }
                                        }
                                    }
                                }
                                free(rx_buf);
                            }
                        }
                    }
                }
            }
        }

        // 定期输出诊断日志 (每 3 秒)
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_diag_log_ms >= 3000) {
            last_diag_log_ms = now;
            xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
            ESP_LOGI(TAG, "GNSS Diag: fix=%d, sats=%d, avg_snr=%d dB, hdop=%.1f, last_len=%lu, i2c_err=%s",
                     g_app_state.gps.valid_fix, g_app_state.gps.satellites,
                     g_app_state.gps.avg_snr, g_app_state.gps.hdop,
                     (unsigned long)last_success_len, esp_err_to_name(last_err));
            xSemaphoreGive(s_gps_mutex);
        }

        // 高度界面数据周期更新 (每 5 秒)
        update_altitude_stats();

        // 1Hz 轨迹写入喂点
        if (now - last_gpx_feed_ms >= 1000) {
            last_gpx_feed_ms = now;
            if (g_app_state.record_status == REC_STATUS_ACTIVE && g_app_state.gps.valid_fix) {
                gpx_manager_feed_point(&g_app_state.gps);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void gps_engine_request_record(record_mode_t mode)
{
    xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
    g_app_state.record_mode = mode;

    if (g_app_state.gps.valid_fix) {
        // 定位已就绪，立即开始
        g_app_state.record_status = REC_STATUS_ACTIVE;
        gpx_manager_start(mode);
        ESP_LOGI(TAG, "Recording started immediately (Mode: %d)", mode);
    } else {
        // 未定位，自动进入延迟待命状态
        g_app_state.record_status = REC_STATUS_ARMED;
        ESP_LOGW(TAG, "GPS not fixed yet. Entering ARMED state, will auto-start upon 3D Fix.");
    }
    xSemaphoreGive(s_gps_mutex);
}

void gps_engine_stop_record(void)
{
    xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
    if (g_app_state.record_status == REC_STATUS_ARMED) {
        // 取消预约
        g_app_state.record_status = REC_STATUS_IDLE;
        g_app_state.record_mode = REC_MODE_NONE;
        ESP_LOGI(TAG, "Armed recording canceled by user.");
    } else if (g_app_state.record_status == REC_STATUS_ACTIVE) {
        // 停止当前录制并安全封存
        gpx_manager_stop();
        g_app_state.record_status = REC_STATUS_IDLE;
        g_app_state.record_mode = REC_MODE_NONE;
        ESP_LOGI(TAG, "Recording stopped and saved.");
    }
    xSemaphoreGive(s_gps_mutex);
}

void gps_get_latest_data(gps_data_t *out_data)
{
    if (!out_data) return;
    xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
    memcpy(out_data, &g_app_state.gps, sizeof(gps_data_t));
    xSemaphoreGive(s_gps_mutex);
}

void gps_get_altitude_stats(altitude_stats_t *out_stats)
{
    if (!out_stats) return;
    xSemaphoreTake(s_gps_mutex, portMAX_DELAY);
    memcpy(out_stats, &g_app_state.alt_stats, sizeof(altitude_stats_t));
    xSemaphoreGive(s_gps_mutex);
}

esp_err_t gps_engine_init(void)
{
    s_gps_mutex = xSemaphoreCreateMutex();
    // 默认时区设为东八区
    g_app_state.gps.timezone_offset = 8;

    // 1. 初始化板载 TCA9554 扩展芯片并复位/使能 LC76G (P7 / EXIO7: GPS_RST)
    esp_io_expander_handle_t io_exp = bsp_io_expander_init();
    if (io_exp) {
        esp_io_expander_set_dir(io_exp, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT);
        // 拉低复位 50ms，随后拉高保持使能
        esp_io_expander_set_level(io_exp, IO_EXPANDER_PIN_NUM_7, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_io_expander_set_level(io_exp, IO_EXPANDER_PIN_NUM_7, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
        ESP_LOGI(TAG, "TCA9554 EXIO7: LC76G GNSS reset released (HIGH)");
    } else {
        ESP_LOGW(TAG, "Failed to initialize TCA9554 IO Expander!");
    }

    // 2. 从 BSP 获取共享的 I2C Master Bus Handle
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus) {
        i2c_device_config_t dev_wr_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ADDR_LC76G_WR,
            .scl_speed_hz = LC76G_I2C_FREQ_HZ,
        };
        esp_err_t ret = i2c_master_bus_add_device(bus, &dev_wr_cfg, &s_lc76g_dev_wr);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add LC76G write device to I2C bus: %s", esp_err_to_name(ret));
        }

        i2c_device_config_t dev_rd_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ADDR_LC76G_RD,
            .scl_speed_hz = LC76G_I2C_FREQ_HZ,
        };
        ret = i2c_master_bus_add_device(bus, &dev_rd_cfg, &s_lc76g_dev_rd);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add LC76G read device to I2C bus: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to get I2C bus handle from BSP!");
    }

    xTaskCreatePinnedToCore(gps_task, "gps_task", 4096, NULL, 4, NULL, 0);
    return ESP_OK;
}
