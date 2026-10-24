#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// =========================================================================
// 硬件引脚与外设定义 (对齐微雪 ESP32-S3-Touch-AMOLED-1.75-G)
// =========================================================================
#define BOARD_BOOT_BTN_GPIO         GPIO_NUM_0

// 共用 I2C 总线
#define BOARD_I2C_SCL_GPIO          GPIO_NUM_14
#define BOARD_I2C_SDA_GPIO          GPIO_NUM_15
#define BOARD_I2C_FREQ_HZ           400000

// LC76G I2C 速率 (对齐微雪官方示例 09_LC76G_I2C: 100kHz)
#define LC76G_I2C_FREQ_HZ           100000

// I2C 从机地址
#define ADDR_TCA9554                0x20
#define ADDR_AXP2101                0x34
#define ADDR_PCF85063               0x51
#define ADDR_CST9217                0x5A
#define ADDR_QMI8658                0x6B
#define ADDR_LC76G_WR               0x50
#define ADDR_LC76G_RD               0x54

// LC76G UART 备用连接 (若使用 UART 模式)
#define LC76G_UART_PORT             UART_NUM_1
#define LC76G_UART_TX_GPIO          GPIO_NUM_17
#define LC76G_UART_RX_GPIO          GPIO_NUM_18
#define LC76G_UART_BAUD             9600

// SDMMC 1-bit 接线
#define SDMMC_CMD_GPIO              GPIO_NUM_1
#define SDMMC_CLK_GPIO              GPIO_NUM_2
#define SDMMC_D0_GPIO               GPIO_NUM_3
#define SD_MOUNT_POINT              "/sdcard"
#define GPX_STORAGE_DIR             "/sdcard/gpx"
#define MAP_STORAGE_DIR             "/sdcard/map"

// 屏幕分辨率
#define LCD_WIDTH                   466
#define LCD_HEIGHT                  466

// =========================================================================
// 业务常量与配置
// =========================================================================
#define GPX_RECORD_FREQ_HZ          1               // 1 Hz 采样写入
#define ALTITUDE_UPDATE_INTERVAL_MS 5000            // 高度界面每 5 秒刷新
#define GPX_AUTO_SLICE_SECONDS      3600            // 1小时自动切片 (3600秒)
#define SD_MIN_FREE_SPACE_MB        200             // FIFO 保留的最小剩余可用容量 (MB)

// Wi-Fi AP 与 Web 服务
#define WEB_AP_SSID                 "Car-Tracker-GPX"
#define WEB_AP_PASS                 ""              // 空密码直接连接
#define WEB_AP_IP                   "192.168.4.1"
#define WEB_SERVER_PORT             80

// =========================================================================
// 核心状态枚举与数据结构
// =========================================================================

// 录制模式
typedef enum {
    REC_MODE_NONE = 0,
    REC_MODE_SEGMENTED,     // 单击：分段记录 (掉电开机建新文件)
    REC_MODE_CONTINUOUS     // 双击：持续记录 (掉电开机追加旧文件)
} record_mode_t;

// 录制运行状态机
typedef enum {
    REC_STATUS_IDLE = 0,    // 待机中
    REC_STATUS_ARMED,       // 预约待命中 (已按 Boot，等待 GPS 首次有效定位)
    REC_STATUS_ACTIVE       // 正在进行 1Hz GPX 文件记录
} record_status_t;

// 地图朝向视角模式
typedef enum {
    VIEW_HEADING_UP = 0,    // 航向朝上 (轨迹/行驶方向始终朝上，地图转动)
    VIEW_NORTH_UP           // 正北朝上 (北方始终朝上，车标根据航向转动)
} map_view_mode_t;

// GNSS 实时遥测数据
typedef struct {
    bool        valid_fix;          // 是否获得有效 3D Fix 定位
    int         satellites;         // 搜星与使用卫星颗数
    double      latitude;           // 纬度 (度)
    double      longitude;          // 经度 (度)
    float       altitude;           // 海拔高度 (米)
    float       speed_kmh;          // 航速 (km/h)
    float       heading_deg;        // 地面运动航向角 (0 ~ 359.9度)
    struct tm   utc_time;           // GPS 纳秒级 UTC 时间
    struct tm   local_time;         // 自动时区换算后的本地时间
    int         timezone_offset;    // 时区偏移小时 (例如 +8 为北京时间)
    float       hdop;               // 水平精度因子 (HDOP)
    int         avg_snr;            // 平均信号载噪比 (dB-Hz)
    int         signal_bars;        // 信号强度格数 (0 ~ 4格)
} gps_data_t;

// 高度统计与历史走势
#define ALT_HISTORY_POINTS          60  // 缓存 60 个历史点 (5秒/点 = 5分钟高频窗口)
typedef struct {
    float   current_alt;
    float   max_alt;
    float   min_alt;
    float   history[ALT_HISTORY_POINTS];
    int     history_count;
    int     history_head;
} altitude_stats_t;

// Wi-Fi 联网与配网状态
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,    // 未连接
    WIFI_STATE_PROVISIONING,        // 扫码配网中 (二维码显示)
    WIFI_STATE_CONNECTING,          // 正在连接 Wi-Fi
    WIFI_STATE_CONNECTED            // 已成功连接并获取 IP
} wifi_state_t;

// 全局应用状态上下文
typedef struct {
    record_mode_t       record_mode;
    record_status_t     record_status;
    map_view_mode_t     map_view_mode;
    bool                ap_web_running;     // AP 与 HTTP 服务是否开启
    bool                sd_mounted;         // SD 卡是否挂载成功
    uint64_t            sd_total_bytes;
    uint64_t            sd_free_bytes;
    gps_data_t          gps;
    altitude_stats_t    alt_stats;
    wifi_state_t        wifi_state;
    char                wifi_ssid[36];
    char                wifi_ip[20];
    char                current_gpx_path[320];
} app_state_t;

// 全局单例句柄
extern app_state_t g_app_state;

#ifdef __cplusplus
}
#endif
