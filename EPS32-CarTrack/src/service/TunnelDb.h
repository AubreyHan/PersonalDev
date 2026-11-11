#pragma once

#include <stdint.h>
#include <stddef.h>
#include "../model/SystemState.h"

// =============================================================================
// TunnelDb.h - Binary File Header & Built-In Chinese Extra-Long Highway Tunnels
//
// Stored in on-chip LittleFS (/littlefs/tunnels.bin) and preloaded into PSRAM
// in < 2ms at boot. Supports both headered (TunnelBinHeader + TunnelInfo[]) and
// headerless raw TunnelInfo[] binary files for Web /api/tunnel/upload updates.
// =============================================================================
namespace TunnelDb {

constexpr uint32_t MAGIC_TUNL      = 0x4C4E5554UL; // 'TUNL' little-endian
constexpr uint16_t FORMAT_VERSION  = 1U;
constexpr const char* BUILTIN_VER  = "builtin-cn-2026.1";

struct __attribute__((packed)) TunnelBinHeader {
    uint32_t magic;          // MAGIC_TUNL (0x4C4E5554)
    uint16_t formatVersion;  // FORMAT_VERSION (1)
    uint16_t recordCount;    // Number of TunnelInfo records following header
    uint16_t recordStride;   // sizeof(TunnelInfo)
    uint16_t reserved;       // 0
    char     version[24];    // Null-terminated version tag
};

// Authoritative Built-In Extra-Long Highway Tunnel Fallback Table (WGS-84 * 1e7)
// Bidirectional entry/exit matching is supported automatically by TunnelService.
inline const TunnelInfo BUILTIN_TUNNELS[] = {
    // 1. 秦岭终南山隧道 (G65 包茂高速 西康段, 18.02 km)
    {
        "Qinling Zhongnanshan",
        339786000, 1089511000,
        338172000, 1089725000,
        173.5f, 18020.0f, 2,
        { { 339240000, 1089582000 }, { 338710000, 1089654000 } }
    },
    // 2. 天山胜利隧道 (G0711 乌尉高速 穿越天山, 22.13 km)
    {
        "Tianshan Shengli",
        431265000, 869850000,
        429312000, 870368000,
        168.8f, 22130.0f, 2,
        { { 430615000, 870022000 }, { 429965000, 870195000 } }
    },
    // 3. 锦屏山隧道 (雅砻江锦屏交通专用特长隧道, 17.50 km)
    {
        "Jinpingshan Tunnel",
        281825000, 1016402000,
        281280000, 1017985000,
        111.2f, 17500.0f, 1,
        { { 281552000, 1017193000 } }
    },
    // 4. 秦岭天台山隧道 (G85 银昆高速 宝汉段, 15.56 km)
    {
        "Qinling Tiantaishan",
        342845000, 1070312000,
        341468000, 1070035000,
        189.5f, 15560.0f, 2,
        { { 342386000, 1070220000 }, { 341927000, 1070128000 } }
    },
    // 5. 米仓山隧道 (G85 银昆高速 巴陕段, 13.83 km)
    {
        "Micangshan Tunnel",
        327298000, 1067245000,
        326092000, 1067612000,
        165.6f, 13833.0f, 1,
        { { 326695000, 1067428000 } }
    },
    // 6. 二郎山隧道 (G4218 雅叶高速 雅康段, 13.43 km)
    {
        "Erlangshan Tunnel",
        298735000, 1022980000,
        298422000, 1021640000,
        254.8f, 13433.0f, 1,
        { { 298578000, 1022310000 } }
    },
    // 7. 麦积山隧道 (G30 连霍高速 宝天段, 12.29 km)
    {
        "Maijishan Tunnel",
        344168000, 1060812000,
        344325000, 1059495000,
        278.2f, 12290.0f, 1,
        { { 344246000, 1060153000 } }
    },
    // 8. 太湖隧道 (S26 沪常高速 苏锡常南部通道, 10.79 km)
    {
        "Taihu Underlake",
        314412000, 1201885000,
        314286000, 1200758000,
        262.5f, 10790.0f, 1,
        { { 314349000, 1201321000 } }
    },
    // 9. 大相岭泥巴山隧道 (G5 京昆高速 雅西段, 10.01 km)
    {
        "Daxiangling Nibashan",
        296842000, 1026120000,
        296015000, 1025715000,
        203.2f, 10007.0f, 1,
        { { 296428000, 1025917000 } }
    },
    // 10. 巴朗山隧道 (G350 中国熊猫大道 卧龙-四姑娘山, 7.95 km)
    {
        "Balangshan Tunnel",
        309085000, 1029352000,
        309412000, 1028610000,
        297.2f, 7945.0f, 1,
        { { 309248000, 1028981000 } }
    },
    // 11. 胶州湾海底隧道 (青岛胶州湾隧道, 7.80 km)
    {
        "Jiaozhou Bay Tunnel",
        360495000, 1203125000,
        359985000, 1202532000,
        223.4f, 7797.0f, 2,
        { { 360320000, 1202920000 }, { 360150000, 1202725000 } }
    },
    // 12. 雀儿山隧道 (G317 川藏北线, 7.08 km)
    {
        "Queershan Tunnel",
        319025000, 990685000,
        318762000, 990005000,
        245.5f, 7079.0f, 1,
        { { 318893000, 990345000 } }
    },
    // 13. 雪峰山隧道 (G60 沪昆高速 邵怀段, 7.02 km)
    {
        "Xuefengshan Tunnel",
        273160000, 1104480000,
        272985000, 1103802000,
        253.8f, 7023.0f, 1,
        { { 273072000, 1104141000 } }
    },
    // 14. 港珠澳大桥海底沉管隧道 (G94 珠三角环线, 6.70 km)
    {
        "HZMB Undersea Tunnel",
        222815000, 1138252000,
        222782000, 1137602000,
        266.8f, 6700.0f, 1,
        { { 222798000, 1137927000 } }
    }
};

constexpr size_t BUILTIN_TUNNEL_COUNT = sizeof(BUILTIN_TUNNELS) / sizeof(BUILTIN_TUNNELS[0]);

} // namespace TunnelDb
