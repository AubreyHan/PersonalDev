#pragma once
#include "model/SystemState.h"

namespace TunnelDatabase {
    // Representative national ultra-long highway tunnels built-in fallback
    // (Also loaded/updated into 32MB PSRAM from /littlefs/tunnels.bin)
    static const TunnelWaypoint WP_TIANSHAN[] = {
        {429512000, 869123000}, {431489000, 869854000}
    };
    static const TunnelWaypoint WP_QINLING[] = {
        {338641000, 1088752000}, {337024000, 1088512000}
    };
    static const TunnelWaypoint WP_MICANGSHAN[] = {
        {326510000, 1066820000}, {325340000, 1066510000}
    };
    static const TunnelWaypoint WP_ERLANGSHAN[] = {
        {298610000, 1022510000}, {298290000, 1021240000}
    };
    static const TunnelWaypoint WP_TAIHU[] = {
        {314520000, 1201850000}, {314110000, 1202860000}
    };

    static const TunnelInfo BUILTIN_TUNNELS[] = {
        {"Tianshan Shengli",  429512000, 869123000,  431489000, 869854000,  15.0f,  22130.0f, 2, WP_TIANSHAN},
        {"Qinling Zhongnan",  338641000, 1088752000, 337024000, 1088512000, 187.0f, 18020.0f, 2, WP_QINLING},
        {"Micangshan Tunnel", 326510000, 1066820000, 325340000, 1066510000, 192.0f, 13810.0f, 2, WP_MICANGSHAN},
        {"New Erlangshan",    298610000, 1022510000, 298290000, 1021240000, 254.0f, 13433.0f, 2, WP_ERLANGSHAN},
        {"Taihu Tunnel",      314520000, 1201850000, 314110000, 1202860000, 115.0f, 10790.0f, 2, WP_TAIHU}
    };

    static constexpr size_t BUILTIN_TUNNEL_COUNT = sizeof(BUILTIN_TUNNELS) / sizeof(BUILTIN_TUNNELS[0]);
}
