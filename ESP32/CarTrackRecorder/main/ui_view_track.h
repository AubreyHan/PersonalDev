#pragma once

#include "app_config.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_view_track_create(lv_obj_t *parent);
void ui_view_track_update(const gps_data_t *gps);

#ifdef __cplusplus
}
#endif
