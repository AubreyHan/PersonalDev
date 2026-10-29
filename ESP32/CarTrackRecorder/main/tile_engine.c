#include "tile_engine.h"
#include <stdio.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"

static const char *TAG = "TILE_ENGINE";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 速度自适应自动缩放
int tile_engine_calc_auto_zoom(float speed_kmh)
{
    if (speed_kmh < 15.0f) {
        return 16; // 低速/停止: 放大看街道细节
    } else if (speed_kmh < 45.0f) {
        return 15; // 城市中速道路
    } else if (speed_kmh < 80.0f) {
        return 14; // 城市快速路/主干道
    } else {
        return 12; // 高速行驶: 广域远景
    }
}

// 经典 Web 墨卡托投影换算 (经纬度 -> 瓦片坐标)
tile_coord_t tile_engine_geo_to_tile(double lat, double lon, int zoom)
{
    tile_coord_t coord;
    coord.z = zoom;

    double n = pow(2.0, zoom);
    double x_float = ((lon + 180.0) / 360.0) * n;

    double lat_rad = lat * M_PI / 180.0;
    double y_float = (1.0 - asinh(tan(lat_rad)) / M_PI) / 2.0 * n;

    coord.x = (int)floor(x_float);
    coord.y = (int)floor(y_float);

    coord.pixel_x = (int)((x_float - coord.x) * 256.0);
    coord.pixel_y = (int)((y_float - coord.y) * 256.0);

    return coord;
}

void tile_engine_get_path(int z, int x, int y, char *out_path, size_t max_len)
{
    snprintf(out_path, max_len, "%s/%d_%d_%d.png", MAP_STORAGE_DIR, z, x, y);
}

bool tile_engine_is_cached(int z, int x, int y)
{
    char path[128];
    tile_engine_get_path(z, x, y, path, sizeof(path));
    struct stat st;
    return (stat(path, &st) == 0 && st.st_size > 0);
}

void tile_engine_request_download(int z, int x, int y)
{
    // 如果已连接到提供互联网的 Wi-Fi，可在后台队列加入下载任务
    // 若当前未联网或处于 AP 模式，则静默跳过，使用暗色网格底图兜底
    ESP_LOGD(TAG, "Tile %d/%d/%d download requested (Cache miss)", z, x, y);
}
