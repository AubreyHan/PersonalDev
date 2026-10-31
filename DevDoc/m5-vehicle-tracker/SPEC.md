# 详细技术规范与数据契约 (SPEC) - M5Track-Tab5

---

## 1. 核心数据模型 (Data Models)

### 1.1 GNSS 实时定位数据模型 (`GnssData`)
```cpp
#pragma once
#include <Arduino.h>

enum GnssFixType : uint8_t {
    FIX_NONE      = 0, // 未定位
    FIX_DEAD_REC  = 1, // 纯惯性推算
    FIX_2D        = 2, // 2D 平面定位
    FIX_3D        = 3, // 3D 立体定位
    FIX_GNSS_DR   = 4, // 组合导航
    FIX_TIME_ONLY = 5  // 仅授时
};

struct SatInfo {
    uint8_t gnssId;    // 0: GPS, 1: SBAS, 2: Galileo, 3: BeiDou, 5: QZSS, 6: GLONASS
    uint8_t svId;      // 卫星编号
    uint8_t cno;       // 载噪比 (dBHz, 0~63)
    int8_t  elev;      // 仰角 (度, -90~90)
    int16_t azim;      // 方位角 (度, 0~360)
    bool    used;      // 是否参与当前定位解算
};

struct GnssData {
    bool        isValid;        // 定位是否有效
    GnssFixType fixType;        // 定位类型 (>= FIX_3D 视为可信)
    int32_t     latE7;          // 纬度 (度 * 1e7)
    int32_t     lonE7;          // 经度 (度 * 1e7)
    double      latitude;       // 纬度 (度, 浮点)
    double      longitude;      // 经度 (度, 浮点)
    float       altitudeMeters; // 海拔高度 (米)
    float       speedKmh;       // 地面航速 (km/h)
    float       headingDeg;     // 地面航向角 (0.0~359.9 度, 0为正北)
    float       hdop;           // 水平精度衰减因子 (越低越好, < 1.5 极佳)
    float       pdop;           // 空间精度衰减因子
    uint8_t     numSats;        // 参与解算的总卫星数
    uint8_t     numBdsSats;     // 北斗卫星数
    uint8_t     numGpsSats;     // GPS 卫星数
    uint8_t     numGloSats;     // GLONASS 卫星数
    uint8_t     numGalSats;     // Galileo 卫星数
    
    // 盲区惯性推算 (Dead Reckoning) 与特长隧道约束
    bool        isDeadReckoning; // 当前是否处于 6 轴 IMU 盲区推测状态
    uint32_t    drDurationSec;   // 处于推测状态的累计秒数
    bool        isInTunnel;      // 是否正处于特长隧道轴线约束模式
    char        tunnelName[24];  // 当前隧道名称 (如 "天山胜利隧道")
    float       tunnelProgressKm;// 隧道内已行进距离 (km)
    float       tunnelLengthKm;  // 隧道设计全长 (km)
    
    // 射频与硬件诊断 (UBX-MON-RF)
    uint8_t     jammingState;    // 0: 正常, 1: 警告, 2: 严重射频干扰 (车内无线充/记录仪压制)
    uint8_t     antennaStatus;   // 1: 正常, 2: 天线开路未插紧 (Open), 3: 天线短路 (Short)
    uint16_t    rfNoiseLevel;    // 射频底噪 (noisePerMS)
    
    // UTC 纳秒级精准时间
    uint16_t    year;
    uint8_t     month;
    uint8_t     day;
    uint8_t     hour;
    uint8_t     minute;
    uint8_t     second;
    uint16_t    millisecond;
    
    // 卫星信噪比列表 (最多缓存 32 颗活跃卫星)
    uint8_t     activeSatCount;
    SatInfo     sats[32];
};
```

### 1.2 特长公路隧道模型 (`TunnelInfo`)
```cpp
struct TunnelWaypoint {
    int32_t latE7; // 经纬度 * 1e7
    int32_t lonE7;
};

struct TunnelInfo {
    const char*    name;             // 隧道名称 (如 "天山胜利隧道")
    int32_t        entryLatE7;       // 入口经度 * 1e7
    int32_t        entryLonE7;       // 入口纬度 * 1e7
    int32_t        exitLatE7;        // 出口经度 * 1e7
    int32_t        exitLonE7;        // 出口纬度 * 1e7
    float          entryBearingDeg;  // 入口基准航向角 (0~360度)
    float          lengthMeters;     // 隧道全长 (米)
    uint8_t        waypointCount;    // 关键拐点数 (2~8点)
    const TunnelWaypoint* waypoints; // 轴线折线点
};
```

### 1.3 行程统计模型 (`TripStats`)
```cpp
struct HistoryBuffer {
    static constexpr size_t CAPACITY = 120; // 30秒采样1次，120个点覆盖 60 分钟 (1小时)
    float    samples[CAPACITY];
    uint16_t head;
    uint16_t count;

    void push(float val) {
        samples[head] = val;
        head = (head + 1) % CAPACITY;
        if (count < CAPACITY) count++;
    }

    void reset() {
        head = 0;
        count = 0;
    }
};

struct TripStats {
    float    tripDistanceKm;   // 本次行程总里程 (km)
    uint32_t movingDurationSec;// 车速 > 1.8km/h 的纯行驶用时 (秒)
    uint32_t idleDurationSec;  // 车辆静止怠速用时 (秒)
    float    maxSpeedKmh;      // 本次行程最高车速 (km/h, 暖橙/红色高光)
    float    avgSpeedKmh;      // 本次行程平均车速 (km/h)
    float    currentAltitudeM; // 当前海拔高度 (米)
    float    maxAltitudeM;     // 本次行程最高海拔 (米)
    float    minAltitudeM;     // 本次行程最低海拔 (米)
    float    cumulativeClimbM; // 本次累计爬升高度 (米)
    float    verticalSpeedMps; // 实时垂直升降速度 (m/s)
    
    // G-Force 赛车级极值记忆
    float    maxLateralG;      // 本次最大横向转弯离心 G
    float    maxLongitudinalG; // 本次最大纵向推背/制动 G
    
    // 过去 1 小时历史采样环形缓冲区 (覆盖 1050px 宽幅发光折线图)
    HistoryBuffer altitudeHistory;
    HistoryBuffer speedHistory;
};
```

### 1.4 全链路行程重置广播契约 (`FullTripResetPayload`)
```cpp
struct FullTripResetPayload {
    uint32_t timestampUtc;     // 重置时刻 UTC 时间戳
    double   newStartLatitude; // 重置时刻当前车位纬度 (作为 Page 4 矢量轨迹新起点)
    double   newStartLongitude;// 重置时刻当前车位经度
    float    currentAltitudeM; // 当前海拔 (作为 Page 2 极值新起点)
    float    currentSpeedKmh;  // 当前车速
};

namespace TripResetConfig {
    // Page 1 仪表盘右下角 TRIP 卡片大触控热区 (1280x720 视口坐标)
    constexpr int16_t  HITBOX_TRIP_X      = 740;
    constexpr int16_t  HITBOX_TRIP_Y      = 460;
    constexpr int16_t  HITBOX_TRIP_W      = 500;
    constexpr int16_t  HITBOX_TRIP_H      = 220;
    constexpr uint32_t LONG_PRESS_MS      = 2000; // 长按 2.0 秒触发清零
    constexpr uint32_t VIBRATION_FEEDBACK_MS = 100; // 触觉震动马达反馈持续时间
}

// 左侧常驻 Dock 触控功能枚举 (解耦无冲突设计)
enum DockTouchAction : uint8_t {
    DOCK_ACTION_NONE    = 0,
    DOCK_ACTION_HOME    = 1, // 点击 🏠 仪表: 秒级返回 Page 1 核心仪表盘
    DOCK_ACTION_TIME    = 2, // 点击 14:35 时钟: 秒级跳转至 Page 7 世界时钟
    DOCK_ACTION_GNSS    = 3, // 点击 🛰️ 18 定位: 秒级跳转至 GPS 详情
    DOCK_ACTION_WIFI    = 4, // 点击 📶 热点: 秒级跳转至 Wi-Fi 配网管理
    DOCK_ACTION_BATTERY = 5, // 点击 🔋 95% 电源: 查看 INA226 实时功耗与电量
    DOCK_ACTION_WEATHER = 6, // 点击 ☀️ 24℃ 气象: 秒级跳转至 Page 6 天气预报
    DOCK_ACTION_TOOLS   = 7  // 点击 ⚙️ 工具: 秒级跳转至 Page 8 系统工具箱
};

// -----------------------------------------------------------------------------
// I2C 硬件外设物理总线正交地址分配表 (Tab5 + Module GNSS)
// -----------------------------------------------------------------------------
// 0x10: BMM150 (Module GNSS 板载 3 轴地磁罗盘 - 因 MagSafe 磁吸环在软件中彻底旁路)
// 0x32: RX8130CE (Tab5 主板板载硬件实时时钟 RTC，带 70000uF 超级电容)
// 0x40: INA226 (Tab5 主板板载高精度电流/电压/功率计)
// 0x68: BMI270 Master (Tab5 主板板载 6 轴主 IMU 姿态传感器 - 直连 P4 RTC 唤醒)
// 0x69: BMI270 Slave  (Module GNSS 板载副 IMU - SDO 硬件上拉，双冗余融合降噪与容灾)
// 0x76: BMP280 (Module GNSS 板载高精度气压计传感器)
// -----------------------------------------------------------------------------

namespace Viewport {
    constexpr int16_t SCREEN_W   = 1280;
    constexpr int16_t SCREEN_H   = 720;
    
    // 左侧 160px 常驻触控侧边栏 (Left Dock)
    constexpr int16_t DOCK_X             = 0;
    constexpr int16_t DOCK_Y             = 0;
    constexpr int16_t DOCK_W             = 160;
    constexpr int16_t DOCK_H             = 720;
    
    // 侧边栏 7 大垂直触控功能热区
    constexpr int16_t DOCK_HOME_Y        = 0;
    constexpr int16_t DOCK_HOME_H        = 90;
    constexpr int16_t DOCK_TIME_Y        = 90;
    constexpr int16_t DOCK_TIME_H        = 110;
    constexpr int16_t DOCK_GNSS_Y        = 200;
    constexpr int16_t DOCK_GNSS_H        = 105;
    constexpr int16_t DOCK_WIFI_Y        = 305;
    constexpr int16_t DOCK_WIFI_H        = 105;
    constexpr int16_t DOCK_BATTERY_Y     = 410;
    constexpr int16_t DOCK_BATTERY_H     = 105;
    constexpr int16_t DOCK_WEATHER_Y     = 515;
    constexpr int16_t DOCK_WEATHER_H     = 105;
    constexpr int16_t DOCK_TOOLS_Y       = 620;
    constexpr int16_t DOCK_TOOLS_H       = 100;
    
    // 右侧全高主内容安全视口 (Main Content Safe Area)
    constexpr int16_t SAFE_X     = 160;
    constexpr int16_t SAFE_Y     = 0;
    constexpr int16_t SAFE_W     = 1120;
    constexpr int16_t SAFE_H     = 720; // 纵向全高 720px
    
    // 悬浮式底部指示器 (Floating OSD Overlay)
    constexpr int16_t  INDICATOR_OSD_Y        = 690;
    constexpr int16_t  INDICATOR_OSD_H        = 20;
    constexpr uint32_t INDICATOR_AUTO_HIDE_MS = 3000; // 3 秒无交互自动彻底隐藏
}

// Tab5 板载 INA226 高精度功耗/电压/电流监测配置
namespace Ina226Config {
    constexpr uint8_t  I2C_ADDR           = 0x40;   // Tab5 板载 INA226 物理地址
    constexpr float    SHUNT_RESISTOR_OHM = 0.02f;  // 20 毫欧精密分流采样电阻 (R_shunt)
    constexpr uint32_t SAMPLE_INTERVAL_MS = 500;   // 500ms (2Hz) 刷新一次功率与电流
    
    // INA226 内部寄存器地址映射
    constexpr uint8_t  REG_CONFIG         = 0x00;   // 配置寄存器
    constexpr uint8_t  REG_SHUNTVOLTAGE   = 0x01;   // 分流电压寄存器 (2.5 uV/LSB)
    constexpr uint8_t  REG_BUSVOLTAGE     = 0x02;   // 总线电压寄存器 (1.25 mV/LSB)
    constexpr uint8_t  REG_POWER          = 0x03;   // 功率寄存器 (25 * Current_LSB = 2.5 mW/LSB)
    constexpr uint8_t  REG_CURRENT        = 0x04;   // 电流寄存器 (Current_LSB = 0.1 mA/LSB)
    constexpr uint8_t  REG_CALIBRATION    = 0x05;   // 校准寄存器
    constexpr uint8_t  REG_MANUF_ID       = 0xFE;   // 制造商 ID (固定 0x5449)
    constexpr uint8_t  REG_DIE_ID         = 0xFF;   // 芯片 ID (固定 0x2260)
    
    // CAL = trunc(0.00512 / (Current_LSB * R_shunt)) = trunc(0.00512 / (0.0001 * 0.02)) = 2560 (0x0A00)
    constexpr uint16_t CALIBRATION_VAL    = 2560;   // 0x0A00
    constexpr float    CURRENT_LSB_MA     = 0.1f;   // 0.1 mA / bit
    constexpr float    POWER_LSB_MW       = 2.5f;   // 2.5 mW / bit
    constexpr float    BUS_VOLT_LSB_MV    = 1.25f;  // 1.25 mV / bit
    constexpr uint16_t CONFIG_WORD_DEFAULT= 0x4127; // 16次平均采样, 1.1ms 转换周期
}

// NP-F550 (2S 7.4V) 电池组与软件库仑计参数规范
namespace CoulombCounterConfig {
    constexpr float    BATTERY_RATED_MAH      = 2000.0f; // 快拆 NP-F550 标准容量 (2000 mAh)
    constexpr float    CHARGE_EFFICIENCY_ETA  = 0.95f;   // 锂电池充电库仑效率 (95%)
    constexpr float    DISCHARGE_EFFICIENCY   = 1.00f;   // 放电效率因数 (100%)
    constexpr float    STANDSTILL_CURRENT_MA  = 20.0f;   // 静置低电流门限 (|I| < 20mA 触发 OCV 校准)
    constexpr uint32_t STANDSTILL_SETTLE_MS   = 5000;    // 静置平稳维持时间 (5 秒无大动态)
    constexpr float    CUTOFF_VOLTAGE_V       = 6.00f;   // 2串锂电放电截止电压 (0%)
    constexpr float    FULL_VOLTAGE_V         = 8.40f;   // 2串锂电满电浮充上限电压 (100%)
}

// 7.4V (2S) 锂电池 11 节点分段开路电压 (OCV) 查表校准契约
struct OcvLookupPoint {
    uint16_t voltageMv;    // 开路总线电压 (mV)
    uint8_t  socPercent;   // 对应电量百分比 (0 ~ 100%)
};

constexpr OcvLookupPoint OCV_TABLE[11] = {
    { 8400, 100 },
    { 8200,  90 },
    { 8000,  80 },
    { 7800,  70 },
    { 7640,  60 },
    { 7520,  50 },
    { 7400,  40 },
    { 7300,  30 },
    { 7160,  20 },
    { 6900,  10 },
    { 6000,   0 }
};

enum BatteryPowerSource : uint8_t {
    POWER_SOURCE_BATTERY_NPF550 = 0, // NP-F550 电池单独供电 (放电状态, 无外接电源 -> 显红色)
    POWER_SOURCE_EXTERNAL_USB   = 1, // 车载 USB-C / HVIN 外部供电充电中 (有外接电源 -> 显绿色)
    POWER_SOURCE_EXT_NO_BATTERY = 2  // 外部直供但未装载 NP-F550 电池 (有外接电源 -> 显绿色)
};

// 电池与外接供电状态颜色指示契约
namespace PowerColorConfig {
    constexpr uint16_t COLOR_EXTERNAL_POWER_GREEN = 0x07E0; // 有外接电源时显示绿色 (TFT_GREEN / #00FFAA)
    constexpr uint16_t COLOR_BATTERY_ONLY_RED     = 0xF800; // 无外接电源 (纯电池) 时显示红色 (TFT_RED / #FF3B30)
}

struct PowerMonitorData {
    bool               isAvailable;        // INA226 是否在线 (0x40 ACK)
    bool               hasExternalPower;   // 是否接通外接电源 (VBUS > 4.5V 或 Current > 0)
    bool               isCharging;         // 是否处于外部电源充电状态 (Current > +15mA)
    BatteryPowerSource powerSource;        // 供电源类型
    uint16_t           uiDisplayColor;     // 状态颜色: 有外接电源时为绿色，无外接电源时为红色
    float              busVoltageV;        // 实时总线电压 (V, 正常范围 6.0 ~ 8.4V)
    float              shuntVoltageMv;     // 分流电阻压降 (mV)
    float              currentMa;          // 实时瞬态电流 (mA，正值充电，负值放电)
    float              avgCurrentMa;       // 30秒滑动平均电流 (mA，用于平滑预测续航)
    float              powerMw;            // 即时功耗 (mW)
    float              remainingMah;       // 库仑计积分维护的当前剩余容量 (mAh, 0~2000)
    float              batterySocPercent;  // 软件库仑计与 OCV 复合计算的电量百分比 (0~100 %)
    uint16_t           remainingMinutes;   // 结合耗电预测的剩余可用运行分钟数 (充电时为充满倒计时)
};

// 手势识别阈值规范
namespace GestureConfig {
    constexpr int16_t SWIPE_HORIZ_THRESHOLD_PX = 60;  // 左右滑动手势最小位移
    constexpr float   DIRECTION_RATIO          = 1.5f;// 主向位移/副向位移判定比率
}

// 车辆运动唤醒、静止休眠与存储配额参数规范
namespace MotionPowerConfig {
    constexpr uint32_t HOURLY_SPLIT_POINTS        = 3600;    // 每小时截断生成新文件 (3600点)
    constexpr uint32_t STANDSTILL_TIMEOUT_MS      = 1200000; // 车辆停止移动满 20 分钟 (1200s) 自动停止轨迹、停止行程并休眠
    constexpr uint32_t STANDSTILL_STOP_RECORD_MS  = 1200000; // 20 分钟静止自动闭合 GPX 文件 (1200s)
    constexpr uint32_t SILENT_ASSESS_WINDOW_MS    = 3000;    // 3 秒静默评估窗口 (防误开机)
    constexpr float    STATIC_VARIANCE_THRESH     = 0.005f;  // 地库无GPS时IMU静止方差门限 (m/s^2)
    constexpr float    SUSTAINED_ACCEL_THRESH     = 0.12f;   // 真实起步持续推背加速度 (G)
    constexpr uint32_t SUSTAINED_ACCEL_MS         = 1500;    // 持续推背时间门限 (ms)
    constexpr uint16_t BATTERY_CAPACITY_MAH       = 2000;    // NP-F550 标准容量 (mAh)
    constexpr float    BATTERY_CHARGE_MAX_TEMP_C  = 55.0f;   // 高温停止充电保护温度 (°C)
    constexpr float    BATTERY_CHARGE_RESUME_TEMP = 45.0f;   // 降温恢复充电温度 (°C)
    
    // TF 卡存储配额与循环覆盖规范 (严格限制不超过卡容量一半)
    constexpr float    TRACK_STORAGE_QUOTA_RATIO  = 0.50f;   // 轨迹数据总容量严格限制不超过 TF 卡总容量的 50%
    constexpr float    TRACK_RECOVERY_QUOTA_RATIO = 0.40f;   // 触发循环清理时成对删除最旧 W/C 轨迹，回退释放至 40%
    constexpr float    KINEMATIC_MAX_SPEED_KMH    = 180.0f;  // 动力学最大合理车速突变门限 (km/h)
    constexpr float    KINEMATIC_MAX_ACCEL_G      = 1.2f;    // 动力学最大合理加速度门限 (G)
    
    // MagSafe 强磁免疫航向融合参数规范 (完全旁路地磁计)
    constexpr bool     BYPASS_GEOMAGNETIC_BMM150  = true;    // 屏蔽地磁计，彻底免疫 MagSafe 磁吸环强磁干扰
    constexpr float    STANDSTILL_HEADING_LOCK_KMH= 1.8f;    // 车速 <= 1.8km/h 触发静态航向物理锁死
}

// 双 BMI270 冗余互验与加权均值降噪参数规范 (Option A)
namespace DualImuConfig {
    constexpr uint8_t  I2C_ADDR_MASTER             = 0x68;    // Tab5 主板 Master IMU (RTC 唤醒源)
    constexpr uint8_t  I2C_ADDR_SLAVE              = 0x69;    // Module GNSS Slave IMU (SDO 硬件拉高)
    constexpr float    CROSS_CHECK_MAX_GYRO_DIFF   = 0.087f;  // 两芯片 Z 轴角速度最大允许互验偏差 (~5 deg/s)
    constexpr float    CROSS_CHECK_MAX_ACCEL_DIFF  = 0.35f;   // 两芯片纵向加速度最大允许互验偏差 (~0.35 G)
    constexpr uint32_t FAILOVER_TIMEOUT_MS         = 20;      // Master 故障判定与热切换至 Slave 超时门限 (ms)
    constexpr float    DUAL_FUSION_WEIGHT_MASTER   = 0.50f;   // 主 IMU 融合权重 (0.50)
    constexpr float    DUAL_FUSION_WEIGHT_SLAVE    = 0.50f;   // 副 IMU 融合权重 (0.50)
}

struct DualImuStatus {
    bool     masterOnline;         // Tab5 板载 0x68 是否正常响应 ACK
    bool     slaveOnline;          // Module GNSS 0x69 是否正常响应 ACK
    bool     isDualFused;          // 当前是否处于双芯片均值融合工作模式 (降噪 ~30%)
    bool     failoverActive;       // 是否因 Master 故障切换至单 Slave 工作模式
    bool     crossCheckWarning;    // 交叉互验差值是否超出安全阈值
    float    fusedAccelX;          // 融合后纵向加速度 (m/s^2)
    float    fusedAccelY;          // 融合后横向加速度 (m/s^2)
    float    fusedAccelZ;          // 融合后天向加速度 (m/s^2)
    float    fusedGyroZ;           // 融合后航向角速度 (rad/s, 供航位推测与罗盘使用)
    float    pitchDeg;             // 俯仰角 (车身前后倾斜, 度)
    float    rollDeg;              // 侧倾角 (车身左右倾斜, 度)
};
```

---

## 2. u-blox NEO-M9N 通信与 AGPS 协议规范

### 2.1 串口与协议配置
- **通信接口**：ESP32-P4 UART (M5-Bus RX0: GPIO 38, TX0: GPIO 37)。
- **初始波特率**：38400 bps（上电初始化后通过 UBX-CFG-PRT 提升至 115200 bps 以支撑 5Hz/10Hz 高频推送）。
- **启用报文**：
  - `UBX-NAV-PVT` (0x01, 0x07)：高频导航位置、速度、时间综合报文（输出频率 5Hz）。
  - `UBX-NAV-SIG` (0x01, 0x43)：多星载噪比与信号质量报文（输出频率 1Hz）。
  - 关闭繁重的 NMEA 文本输出，仅保留纯二进制 UBX 协议以节省 CPU 解析负担。

### 2.2 Wi-Fi 触发的 NTP 时间辅助注入 (`UBX-MGA-INI-TIME_UTC`)
当 Wi-Fi 取得 NTP 授时后，构建该报文下发：
- **Class / ID**：`0x13`, `0x40` (UBX-MGA-INI)。
- **报文负载**：
  - `type` = 0x10 (Time UTC)
  - `version` = 0x00
  - `ref` = 0 (无外置 PPS，直接生效)
  - `leapSecs` = 18 (当前 GPS 闰秒)
  - `year`, `month`, `day`, `hour`, `minute`, `second`
  - `accuracyS` = 0, `accuracyNs` = 50000000 (假定 NTP 精度 $\pm 50\text{ms}$)
- **效果**：模组立即锁定参考时间，大幅缩窄多普勒频移搜索窗口。

### 2.3 u-blox AssistNow Online 星历下载规范
- **请求端点**：`http://online-live1.services.u-blox.com/GetOnlineData.ashx`
- **请求参数**：
  - `token=<User_Thingstream_Token>`
  - `gnss=gps,bds,glo,gal`
  - `datatype=eph,alm,aux`
- **处理方式**：ESP32-P4 以 128~512 字节分块流式读取 AssistNow 二进制流，调用官方 API `myGNSS.pushRawData(buffer, bytesRead)` 直接注入 NEO-M9N 串口解算链路。

### 2.4 车载环境专项 UBX 配置规范 (In-Cabin UBX-CFG)
- **动态平台模型 (`UBX-CFG-NAV5`)**：
  - `dynModel = 4` (`DYN_MODEL_AUTOMOTIVE`，车载模式，抗 4G 极限加减速)。
  - `fixMode = 3` (Auto 2D/3D)。
- **静止死区锁定 (`staticHoldThresh`)**：
  - `staticHoldThresh = 50` ($0.5\text{ m/s} \approx 1.8\text{ km/h}$)。
  - 触发行为：车速低于门限时，水平航向固定、车速强制报告为 0，经纬度锁止不动。
- **两阶段动态信号质量门限与室内转室外快速重捕获 (`UBX-CFG-NAVSPG` & `UBX-CFG-RST`)**：
  - **阶段 1（未定位搜星态 Acquisition Mode）**：`minElev = 5°`, `cnoThresh = 14 dBHz`，避免刚从室内/地库驶出室外时弱星被一刀切拦截导致无法收敛；
  - **阶段 2（稳定 3D Fix 巡航态 Tracking Mode）**：连续 3D Fix 满 10 秒且卫星数 $\ge 6$ 后，动态切换为 `minElev = 10°`, `cnoThresh = 20 dBHz` 抑制机盖与立交桥多径杂波；一旦失锁立即退回阶段 1；
  - **搜星发散自动无损热启动看门狗 (`UBX-CFG-RST`, `0x06 0x04`)**：未定位持续每满 45 秒，自动下发 `navBbrMask = 0x0000 (Hotstart)` + `resetMode = 0x02 (Controlled GNSS-only software reset)`，清理室内积攒的伪距残差与发散频偏窗口，随后立即重发 `UBX-MGA-INI-TIME_UTC` 与 `UBX-MGA-INI-POS_LLH`。
- **多星系统配置 (`UBX-CFG-GNSS`)**：
  - `GPS (L1C/A)`：启用 (min 8, max 16 追踪通道)
  - `BeiDou (B1I)`：启用 (min 8, max 16 追踪通道)
  - `GLONASS (L1OF)`：启用 (min 6, max 12 追踪通道)
  - `Galileo (E1B/C)`：启用 (min 6, max 12 追踪通道)

### 2.5 复合模块板载 BMP280 气压/高程融合规范
- **硬件通信**：硬件 I2C，7 位从机地址 `0x76`。
- **采样参数**：
  - 温度过采样：`osrs_t = ×2`
  - 气压过采样：`osrs_p = ×16` (超高分辨率模式，噪声降低至 $0.16\text{ Pa} \approx 1.3\text{ cm}$)
  - IIR 滤波时间常数：`filter = 16`
- **高程互补滤波公式**：
  $$H_{\text{baro}} = 44330 \times \left(1.0 - \left(\frac{P}{P_0}\right)^{0.1903}\right)$$
  当 GNSS 处于 3D Fix 且 HDOP $< 1.2$ 时，海平面基准气压动态校准：
  $$P_0(t) = P_0(t-1) + \alpha \cdot \left[ P \cdot \left(1.0 - \frac{H_{\text{GNSS}}}{44330}\right)^{-\frac{1}{0.1903}} - P_0(t-1) \right]$$
  式中增益系数 $\alpha = 0.005$（超低频渐进平滑），彻底融合出分米级平稳高程。

### 2.6 极限工况防御与硬件诊断报文规范 (Edge Case & Diagnostic Protocols)
- **天线回路与射频干扰监测 (`UBX-MON-RF`)**：
  - 轮询周期：每 2 秒下发一次请求查询射频状态。
  - 提取字段：
    - `jamFlags`：低 2 位表示干扰程度（0=OK，1=Warning，2=Critical）；
    - `antStatus`：天线供电回路（1=正常，2=断线/未插好，3=短路）；
    - `noisePerMS`：高频环境噪声本底强度。
- **地库出库极速温启动位置注入 (`UBX-MGA-INI-POS_LLH`)**：
  - 触发条件：开机自检时若从 NVS 中读出有效的上次熄火坐标。
  - Class/ID: `0x13`, `0x40`；
  - 报文结构：
    - `type` = 0x01 (Position LLH)；
    - `lat` = `last_lat_e7`，`lon` = `last_lon_e7`，`alt` = `last_alt_cm`；
    - `posAcc` = 50000 (假定粗位置精度 500 米)。
  - 效果：大幅加速卫星可见性预测与多普勒频偏锁定，将出库冷启从 35s+ 缩短至 10~15s。

---

## 3. GPX 1.1 全维度双坐标系轨迹文件格式规范 (Dual Coordinates: WGS-84 & GCJ-02)

保存在 MicroSD 卡 `/tracks/` 目录中，成对并行生成：
- **WGS-84 原始轨迹**：`W-yyyymmdd-hhmmss.gpx`（直出 GPS 真实坐标）
- **GCJ-02 火星坐标轨迹**：`C-yyyymmdd-hhmmss.gpx`（China 国测局加密转换，国内地图无偏移）

```xml
<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.1" creator="M5Track-Tab5" 
     xmlns="http://www.topografix.com/GPX/1/1"
     xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
     xmlns:gpxtpx="http://www.garmin.com/xmlschemas/TrackPointExtension/v1"
     xmlns:m5="http://m5stack.com/xmlschemas/VehicleTrack/v1"
     xsi:schemaLocation="http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd">
  <metadata>
    <name>Track 2026-10-30 15:45:00</name>
    <desc>M5Track-Tab5 Vehicle High-Precision Log (1Hz)</desc>
    <time>2026-10-30T07:45:00.000Z</time>
  </metadata>
  <trk>
    <name>M5Tab5 Vehicle Drive</name>
    <trkseg>
      <trkpt lat="31.2304168" lon="121.4737012">
        <!-- 1. 空间与大地三维坐标 (Standard GPX) -->
        <ele>128.45</ele>
        <time>2026-10-30T07:45:00.000Z</time>
        <course>185.3</course>
        <speed>25.8</speed>
        <fix>3d</fix>
        <sat>24</sat>
        <hdop>0.82</hdop>
        <vdop>1.15</vdop>
        <pdop>1.41</pdop>

        <!-- 2. 车载与环境复合扩展数据 (Garmin & M5 Extensions) -->
        <extensions>
          <gpxtpx:TrackPointExtension>
            <gpxtpx:atemp>24.6</gpxtpx:atemp>
          </gpxtpx:TrackPointExtension>
          <m5:pressure_hpa>1012.35</m5:pressure_hpa>
          <m5:sats_bds>10</m5:sats_bds>
          <m5:sats_gps>8</m5:sats_gps>
          <m5:sats_glo>4</m5:sats_glo>
          <m5:sats_gal>2</m5:sats_gal>
          <m5:lateral_g>0.18</m5:lateral_g>
          <m5:long_g>-0.05</m5:long_g>
          <m5:pitch_deg>2.1</m5:pitch_deg>
          <m5:roll_deg>-0.8</m5:roll_deg>
          <m5:road_slope_percent>+3.7</m5:road_slope_percent>
          <m5:is_tunnel>false</m5:is_tunnel>
          <m5:battery_pct>95</m5:battery_pct>
        </extensions>
      </trkpt>
      <!-- 追加持续 1Hz 写入点位 -->
    </trkseg>
  </trk>
</gpx>
```

---

## 4. Web 配网与文件管理 API 及二维码规范 (Captive Portal, QR Codes & SoftAP)

系统所有 Web 交互均以按需启动的独立 SoftAP 热点模式提供，并配合 **DNS Captive Portal (端口 53 全域名解析至 `192.168.4.1`)** 与 **`M5GFX` 原生屏幕二维码 (`canvas.qrcode()`)**：
- **配网模式 (`PageWifiConfig`)**：
  - AP SSID：`M5Track-Setup`，IP：`192.168.4.1`；
  - **二维码 1（扫码自动连热点）**：`WIFI:T:nopass;S:M5Track-Setup;;`
  - **二维码 2（扫码直达配网界面）**：`http://192.168.4.1/setup`（连上热点后手机 Captive Portal 也会自动唤起该界面）。
- **TF 卡文件下载模式 (`ToolTfManager`)**：
  - AP SSID：`M5Track-Share`，IP：`192.168.4.1`（仅在触发“通过 Wi-Fi 下载”时开启，退出菜单即关闭）；
  - **二维码 1（扫码自动连热点）**：`WIFI:T:nopass;S:M5Track-Share;;`
  - **二维码 2（扫码直达轨迹下载界面）**：`http://192.168.4.1/tracks`（连上热点后手机 Captive Portal 也会自动打开轨迹下载列表）。

| 方法 | 路径 | 描述 | 请求格式 | 响应格式 (JSON) |
| :--- | :--- | :--- | :--- | :--- |
| `GET` | `/` | 配网与管理单页应用 | 无 | HTML5 + CSS (全黑夜视风格，适配移动端) |
| `GET` | `/api/scan` | 扫描周围可用 Wi-Fi | 无 | `{"aps":[{"ssid":"Home","rssi":-55,"auth":3},...]}` |
| `GET` | `/api/saved` | 获取已保存的 Wi-Fi 清单 | 无 | `{"saved":[{"ssid":"Home"},{"ssid":"Car_Hotspot"}]}` |
| `POST` | `/api/connect` | 保存并尝试连接指定网络 | `{"ssid":"...","pwd":"..."}` | `{"success":true,"message":"Connecting..."}` |
| `POST` | `/api/delete` | 删除已保存的指定 Wi-Fi | `{"ssid":"..."}` | `{"success":true}` |
| `GET` | `/api/tracks` | 查询 TF 卡内所有 GPX 文件及坐标系属性 | 无 | `{"files":[{"name":"W-20261030-090000.gpx","size":654320,"type":"wgs84","badge":"国际/Google Earth版"},{"name":"C-20261030-090000.gpx","size":654320,"type":"gcj02","badge":"国内/高德/两步路版"}]}` |
| `GET` | `/api/download`| 下载指定轨迹文件 | Query: `?file=W-20261030-090000.gpx` | 二进制流 / `application/gpx+xml` |
| `POST`| `/api/sd/format`| 格式化 TF 卡并重建 tracks 目录 | `{"confirm":true}` | `{"success":true,"message":"SD Formatted"}` |
| `GET` | `/api/tunnel/info` | 查询片上 Flash 隧道库版本与条目数 | 无 | `{"version":"2026.10","count":82,"source":"LittleFS"}` |
| `POST`| `/api/tunnel/upload`| 上传更新片上 Flash 隧道数据库 | Multipart / Binary | `{"success":true,"count":85,"message":"Tunnel DB Reloaded to PSRAM"}` |

---

## 5. 片上 Flash 分区表规范 (`partitions.csv` - 16MB NOR Flash)

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x640000,
app1,     app,  ota_1,   0x650000,0x640000,
littlefs, data, spiffs,  0xc90000,0x300000,
```
- `littlefs` 分区分配 **3MB**，挂载路径 `/littlefs`；
- 隧道数据库文件：`/littlefs/tunnels.bin`；
- 启动时全量读入 32MB PSRAM 内存缓存，运行时零 Flash/TF I/O 耗时。

---

## 6. NVS (Non-Volatile Storage) 键值规范

使用 ESP32 `Preferences`，Namespace: `"m5track"`：

| Key 名称 | 类型 | 默认值 | 说明 |
| :--- | :--- | :--- | :--- |
| `wifi_count` | `uint8_t` | 0 | 当前已保存的 Wi-Fi 数量 (最大 5 组) |
| `ssid_0` ~ `ssid_4` | `String` | `""` | 各插槽的 Wi-Fi SSID |
| `pwd_0` ~ `pwd_4` | `String` | `""` | 各插槽的 Wi-Fi 密码 |
| `auto_record` | `bool` | `true` | 是否开机/定位后车速 > 3km/h 自动录制 |
| `tz_offset_min`| `int16_t` | 480 | 时区偏移分钟数 (480 对应 UTC+8，负值对应西区，支持自动经度推算) |
| `screen_bright`| `uint8_t` | 180 | 屏幕背光亮度 (0~255) |
| `last_lat_e7`  | `int32_t` | 0   | 熄火断电时最后已知有效纬度 (*1e7)，用于地库开机极速温启动 |
| `last_lon_e7`  | `int32_t` | 0   | 熄火断电时最后已知有效经度 (*1e7) |
| `last_alt_cm`  | `int32_t` | 0   | 熄火断电时最后已知海拔高程 (厘米) |
| `rec_trip_mode`| `uint8_t` | 0   | 轨迹分卷记录模式 (0: 单次切卷, 1: 单日归档, 2: 小记专属) |
| `act_trip_view`| `uint8_t` | 0   | 仪表盘当前查看的行程类别 (0: 单次, 1: 单日, 2: 小记) |
| `man_trip_act` | `bool`    | false | 小记行程是否处于活动记录状态 |
| `sub_idx`      | `uint16_t`| 0   | 小记行程总计数器 (1, 2, 3...)，用于 `sub001` 文件命名 |
| `sub_s_idx`    | `uint16_t`| 1   | 本次小记当前 10MB 自动分卷序号 (1, 2, 3...)，用于 `-001` 分卷命名 |
| `last_day_int` | `uint32_t`| 0   | 上次记录的本地日期整数 (YYYYMMDD，用于检测跨天午夜清零) |
| `day_dist`     | `double`  | 0.0 | 当日累计行驶里程 (km) |
| `man_dist`     | `double`  | 0.0 | 小记累计行驶里程 (km) |

---

## 7. TF 卡系统事件与开关机日志规范 (Power Event FIFO Log Specification)

### 7.1 文件路径与尺寸契约
- **目标文件**：`/tracks/power.log`
- **临时转存文件**：`/tracks/power.tmp`
- **尺寸硬约束**：`POWER_LOG_MAX_BYTES = 100 * 1024` (102,400 字节，100KB)
- **FIFO 淘汰步长**：`POWER_LOG_TRIM_BYTES = 20 * 1024` (20,480 字节，20KB)

### 7.2 日志格式规范（采用 GPS 定位时区时间）
每条记录占一行，时间采用车辆 GPS 定位推算出的当地时区时间（包含时区偏置 `±HH:MM`）：
```text
[<YYYY-MM-DD HH:MM:SS ±HH:MM>] <EVENT_TYPE>: <DETAILS>
```
示例：
```text
[2026-10-30 22:15:20 +08:00] BOOT: System startup (Reset: 1, Batt: 8.15V, 95%, Pwr: ExtUSB, TF: Mounted)
[2026-10-30 22:15:25 +08:00] TIME_SYNC_GNSS: GNSS 3D Fix calibrated -> 2026-10-30 14:15:25 UTC
[2026-10-30 22:35:25 +08:00] STANDBY_ENTER: Standstill 20min timeout (Pwr: ExternalUSB, Batt: 8.20V, 98%)
[2026-10-30 23:02:10 +08:00] STANDBY_WAKEUP: Sustained vehicle motion detected (Batt: 8.20V, 98%)
[2026-10-30 23:40:00 +08:00] SHUTDOWN_LOW_BATT: Critical low battery (5.95V < 6.00V), auto shutdown
```

### 7.3 FIFO 淘汰算法与断电安全
1. 追加写入新日志前，若当前文件大小 + 新行长度 $> 100\text{ KB}$，触发裁剪；
2. 读取原文件，Seek 至 `POWER_LOG_TRIM_BYTES` 偏移处，继续向后扫描直至第一个换行符 `\n`，保证淘汰行完整性；
3. 将剩余最新数据块（约 80KB）流式写入 `/tracks/power.tmp`，调用 `sync()` 刷盘并关闭；
4. 移除原文件，并将临时文件原子重命名为 `/tracks/power.log`；
5. 追加新日志行，调用 `file.sync()` 完成落盘。全流程由递归互斥锁 `_sdMutex` 严格保护。

---

## 8. 三类行程统计与轨迹分卷技术契约 (Trip Categories & Track Splitting Contract)

### 8.1 行程枚举契约 (`TripCategory`)
```cpp
enum TripCategory : uint8_t {
    TRIP_SINGLE = 0, // 单次行程：随车辆通电/唤醒开始，连续静止 20min 待机或长按 2s 清零
    TRIP_DAILY  = 1, // 单日行程：每日 00:00:00 至 23:59:59，跨段全天累加，次日自动清零
    TRIP_MANUAL = 2, // 小记行程：手动 START / STOP，跨天跨次长期累计，手动终结
    TRIP_CATEGORY_COUNT = 3
};
```

### 8.2 轨迹文件命名规则与分卷逻辑
| 记录模式 (`recordTripMode`) | WGS-84 文件名 | GCJ-02 文件名 | 分卷与闭合时机 |
| :--- | :--- | :--- | :--- |
| **0. 单次切卷 (`TRIP_SINGLE`)** | `W-Single-YYYYMMDD-HHMMSS.gpx` | `C-Single-YYYYMMDD-HHMMSS.gpx` | 车辆起步新建，静止 20min 或单次清零时闭合存盘 |
| **1. 单日归档 (`TRIP_DAILY`)** | `W-Day-YYYYMMDD.gpx` | `C-Day-YYYYMMDD.gpx` | 当日多次行程以 `<trkseg>` 拼接追加，午夜跨天换新卷 |
| **2. 小记专属 (`TRIP_MANUAL`)** | `W-Manual-YYYYMMDD-HHMMSS.gpx` | `C-Manual-YYYYMMDD-HHMMSS.gpx` | 仅在小记处于活动状态时记录，多次起停以 `<trkseg>` 缝合，手动点击 STOP 彻底闭合 |
