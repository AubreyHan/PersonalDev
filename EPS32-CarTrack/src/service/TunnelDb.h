#pragma once
#include <Arduino.h>
#include "model/SystemState.h"

// Sample waypoint polylines for national super-long highway tunnels
static const TunnelWaypoint WP_TIANSHAN[] = {
    { 428056000, 866532000 }, // North Portal
    { 427500000, 866700000 }, // Midpoint
    { 426065000, 867120000 }  // South Portal
};

static const TunnelWaypoint WP_ZHONGNANSHAN[] = {
    { 338520000, 1089300000 }, // North Portal
    { 337700000, 1089450000 }, // Midpoint
    { 336900000, 1089600000 }  // South Portal
};

static const TunnelInfo BUILTIN_TUNNELS[] = {
    {
        "天山胜利隧道 (22.1km)",
        428056000, 866532000,
        426065000, 867120000,
        175.0f,
        22100.0f,
        3,
        WP_TIANSHAN
    },
    {
        "秦岭终南山公路隧道 (18.0km)",
        338520000, 1089300000,
        336900000, 1089600000,
        168.0f,
        18020.0f,
        3,
        WP_ZHONGNANSHAN
    }
};

static constexpr size_t BUILTIN_TUNNEL_COUNT = sizeof(BUILTIN_TUNNELS) / sizeof(BUILTIN_TUNNELS[0]);
