#pragma once

#include "app_config.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_view_altitude_create(lv_obj_t *parent);
void ui_view_altitude_update(const altitude_stats_t *stats);

#ifdef __cplusplus
}
#endif
