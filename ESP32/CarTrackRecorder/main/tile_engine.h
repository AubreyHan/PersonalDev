#pragma once

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// 经纬度转瓦片坐标结构
typedef struct {
    int z;  // 缩放层级
    int x;  // 瓦片横坐标
    int y;  // 瓦片纵坐标
    int pixel_x; // 瓦片内像素偏移 (0 ~ 255)
    int pixel_y; // 瓦片内像素偏移 (0 ~ 255)
} tile_coord_t;

// 根据当前车速自适应计算最适缩放级别
int tile_engine_calc_auto_zoom(float speed_kmh);

// 经纬度换算为瓦片编号与内部像素偏移
tile_coord_t tile_engine_geo_to_tile(double lat, double lon, int zoom);

// 检查指定瓦片是否已缓存在 SD 卡中
bool tile_engine_is_cached(int z, int x, int y);

// 获取瓦片在 SD 卡上的完整文件路径
void tile_engine_get_path(int z, int x, int y, char *out_path, size_t max_len);

// 请求后台异步下载缺失的瓦片
void tile_engine_request_download(int z, int x, int y);

#ifdef __cplusplus
}
#endif
