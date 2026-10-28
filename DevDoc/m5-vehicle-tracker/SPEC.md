# 详细技术规范与数据契约 (SPEC) - M5Track-M9N

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
    size_t   headIdx;       // 环形缓冲区写入游标
    size_t   count;         // 当前有效点数
    uint32_t lastSampleEpoch;
};

struct TripStats {
    uint32_t startTimeSec;     // 行程开始时间戳
    uint32_t durationSec;      // 行驶总持续秒数
    float    distanceKm;       // 本次累计行驶里程 (km)
    float    currentSpeedKmh;  // 当前车速
    float    maxSpeedKmh;      // 本次最高时速 (km/h)
    float    avgSpeedKmh;      // 平均车速 (移动时间加权)
    float    startAltitude;    // 起点海拔 (米)
    float    maxAltitude;      // 本次最高海拔 (米)
    float    minAltitude;      // 本次最低海拔 (米)
    float    cumulativeClimb;  // 累计爬升 (米)
    
    // 过去 1 小时历史采样 (环形缓冲区)
    HistoryBuffer altHistory;    // 海拔 1 小时曲线
    HistoryBuffer speedHistory;  // 车速 1 小时曲线
};
```

### 1.4 车辆动态姿态模型 (`VehicleDynamics`)
```cpp
struct VehicleDynamics {
    float lateralG;        // 实时横向 G 值 (+右转离心, -左转离心)
    float longG;           // 实时纵向 G 值 (+推背加速, -急刹制动)
    float totalG;          // 实时合成 G 值 sqrt(Gx^2 + Gy^2)
    float maxLateralG;     // 本次行程峰值横向过弯 G
    float maxLongG;        // 本次行程峰值纵向加速 G
    float pitchDeg;        // 车身俯仰角 (-45° ~ +45°, 前倾/后仰)
    float rollDeg;         // 车身侧倾角 (-45° ~ +45°, 左右侧倾)
    float roadSlopePercent;// 道路真实坡度百分比 (如 +7.5% 陡坡)
};
```

### 1.5 矢量自驾轨迹模型 (`TrailState`)
```cpp
enum TrailViewMode : uint8_t {
    TRAIL_VIEW_FIT_ENTIRE = 0, // 远景全路径贴合 (自动外接矩形自适应，恰好显示整个路径)
    TRAIL_VIEW_FOLLOW_5KM = 1  // 近景 5km 弯道巡航跟随
};

struct TrailPoint {
    int32_t latE7;
    int32_t lonE7;
    int16_t altM;
};

struct TrailState {
    static constexpr size_t MAX_POINTS = 800; // Douglas-Peucker 空间抽稀后驻留 PSRAM
    TrailPoint points[MAX_POINTS];
    size_t     count;
    TrailViewMode viewMode;       // 当前缩放模式 (默认全貌整径贴合，点击一键切近景)
    int32_t    minLatE7;          // 动态外接包围盒
    int32_t    maxLatE7;
    int32_t    minLonE7;
    int32_t    maxLonE7;
    float      distanceToStartKm; // 直线距起点 (km)
    float      bearingToStartDeg; // 起点相对方位角 (度)
};
```

### 1.6 系统状态机与全局上下文 (`SystemContext`)
```cpp
enum AppState : uint8_t {
    STATE_BOOT_INIT,      // 开机自检与 GPS 搜星等待页
    STATE_WIFI_CONFIG,    // 配网 AP 与 WebPortal 交互模式
    STATE_MAIN_APP,       // 正式多页面系统运行模式
    STATE_PWR_OFF_WARN,   // 外部断电 10 秒倒计时模式
    STATE_SILENT_ASSESS,  // 移动唤醒静默评估状态 (纯黑防误触判定窗口)
    STATE_DEEP_SLEEP      // 超低功耗深度睡眠待机模式 (<35uA)
};

struct HourlyForecastItem {
    uint8_t hour;             // 小时 (0~23)
    int8_t  tempC;            // 预测气温 (-40 ~ 50 °C)
    uint8_t rainProbPercent;  // 降水概率 (0~100 %)
    uint8_t weatherCode;      // WMO 天气代码 (0:晴, 1~3:多云, 51~67:雨, 71~77:雪, 95:雷暴)
};

struct WeatherInfo {
    bool     hasData;          // 是否成功获取过气象数据
    float    temperatureC;     // 当前摄氏度 (如 24.5)
    uint8_t  relativeHumidity; // 相对湿度 (0~100 %)
    uint8_t  currentWeatherCode;
    float    windSpeedKmh;     // 风速 (km/h)
    uint32_t lastUpdateEpoch;  // 上次更新时间戳
    
    // 未来 24 小时逐小时预报 (常驻内存，离线全天可用)
    HourlyForecastItem hourly[24];
    uint8_t            currentSlotIdx; // 当前展示的时段切片 (0: 当前0~8h, 1: 8~16h, 2: 16~24h，通过上下滑动切换)
};

struct ToolItem {
    const char* id;          // 工具唯一标识 (如 "wifi", "gpx", "gnss", "settings")
    const char* title;       // 工具主标题 (如 "Wi-Fi 管理")
    const char* statusText;  // 状态小副标 (如 "192.168.1.10" 或 "12 Files")
    uint16_t    iconColor;   // 图标主题色 (RGB565 发光色)
    void (*onClick)();       // 点击回调处理函数
};

enum ClockSlotMode : uint8_t {
    CLOCK_MODE_GPS_AUTO    = 0, // GPS 经度自适应本地时区
    CLOCK_MODE_MANUAL_CITY = 1  // 预置世界城市列表点选
};

enum CityRegion : uint8_t {
    REGION_ASIA_PACIFIC = 0, // 亚太 / 国内
    REGION_AMERICAS     = 1, // 美洲
    REGION_EUROPE       = 2, // 欧洲 / 中东
    REGION_UTC_STANDARD = 3  // UTC / 世界标准
};

struct WorldCityDef {
    const char* cityNameCn;             // 城市中文名 (如 "北京", "纽约")
    const char* cityNameEn;             // 英文名或代号 (如 "Beijing", "New York")
    int8_t      standardUtcOffsetHours; // 标准 UTC 偏移小时 (-12 ~ +14)
    bool        hasDst;                 // 是否实行夏令时规则 (如美东夏令时+1h)
    uint8_t     region;                 // 所属大区 CityRegion
};

struct WorldClockSlotConfig {
    ClockSlotMode mode;                 // 模式: GPS自动 vs 城市点选
    uint8_t       cityIndex;            // 关联预置城市库索引 (0~31)
    int8_t        utcOffsetHours;       // 实际生效时区偏移小时 (-12 ~ +14)
    char          displayName[16];      // 卡片显示名称 (如 "北京 Beijing")
    bool          isDstActive;          // 当前是否处于夏令时生效状态
};

struct WorldClockState {
    WorldClockSlotConfig slots[4];      // 2x2 网格 4 个时钟槽位 (持久化于 NVS)
    uint8_t              activeEditSlot;   // 当前正在被点击编辑的槽位 (0~3)
    uint8_t              activeEditRegion; // 城市选择面板当前激活的大类标签 (0~3)
};

struct SystemContext {
    AppState    currentState;
    uint8_t     mainPageIdx;      // 当前页面索引 (0:Dash, 1:Alt, 2:Speed, 3:Trail, 4:Dynamics, 5:Weather, 6:WorldClock, 7:Tools...)
    uint8_t     totalMainPages;   // 动态总页数 (默认 8 页: 7业务页 + 1工具页)
    bool        isWifiConnected;
    int8_t      wifiRssi;         // Wi-Fi 信号强度 (dBm, -100 ~ -30)
    String      wifiSsid;
    IPAddress   localIp;
    bool        isSdMounted;
    bool        isLogging;        // 是否正在向 TF 卡记录 GPX
    String      currentGpxName;   // 当前写入的 GPX 文件名
    uint32_t    recordedPoints;   // 当前文件已记录点数
    
    // 动态姿态与自驾轨迹
    VehicleDynamics dynamics;
    TrailState      trail;
    
    // GPS 坐标反查的实时气象与24小时逐小时预报
    WeatherInfo weather;
    
    // 多时区世界时钟
    WorldClockState worldClock;
    
    // 电源管理
    bool        vbusConnected;    // Type-C 是否供电
    BatteryHardwareMode batteryMode; // 电池硬件自适应模式 (无底座为空 vs 有底座真实电量)
    uint8_t     batteryPercent;   // 锂电池电量 (未装底座固定为 0，装底座为 0~100)
    bool        isCharging;       // 是否正在充电
    int8_t      pwrCountdownSec;  // 倒计时剩余秒数 (初始 10)
};

// 屏幕视口常量定义

// Page 1 里程归零热区与手势常量

// 全系统全链路行程重置广播契约 (Full Trip Sync Reset Contract)
// 包含: Page 1 里程/时间, Page 2 海拔极值与折线, Page 3 车速极值与折线, Page 4 轨迹清空与新起点, Page 5 峰值G值
struct FullTripResetPayload {
    uint32_t resetEpoch;       // 归零发生时间戳
    int32_t  currentLatE7;     // 当前坐标 (作为 Page 4 轨迹新起点)
    int32_t  currentLonE7;
    float    currentAltM;      // 当前海拔 (作为 Page 2 海拔极值基准)
    float    currentSpeedKmh;  // 当前车速
};

namespace TripResetConfig {
    constexpr int16_t  HITBOX_TRIP_X      = 160;
    constexpr int16_t  HITBOX_TRIP_Y      = 180;
    constexpr int16_t  HITBOX_TRIP_W      = 152;
    constexpr int16_t  HITBOX_TRIP_H      = 50;  // X: 160~312, Y: 180~230
    constexpr uint32_t LONG_PRESS_MS      = 2000; // 长按 2 秒判定门限
    constexpr uint32_t VIBRATION_FEEDBACK_MS = 100; // 震动马达反馈持续时间
}


// -----------------------------------------------------------------------------
// I2C 硬件外设物理总线地址分配表 (三层长续航大电池版 - 含 Base Dual 16340)
// -----------------------------------------------------------------------------
// 0x34: AXP192 (PMIC 电源管理芯片)
// 0x38: FT6336U (电容式触摸屏控制器)
// 0x45: INA226 (Base Dual 16340 板载高精度电流/电压/功率计)
// 0x51: BM8563 (实时硬件时钟芯片 RTC)
// 0x68: BMI270 (Module GNSS 板载 6 轴 IMU 姿态传感器 - 独占标准地址，零冲突)
// 0x10: BMM150 (Module GNSS 板载 3 轴地磁罗盘 - 挂载于 BMI270 Aux 总线)
// 0x76: BMP280 (Module GNSS 板载气压计传感器)
// -----------------------------------------------------------------------------

namespace Viewport {
    constexpr int16_t SCREEN_W   = 320;
    constexpr int16_t SCREEN_H   = 240;
    
    // 顶部状态栏与全局触控热区
    constexpr int16_t TOP_BAR_X          = 0;
    constexpr int16_t TOP_BAR_Y          = 0;
    constexpr int16_t TOP_BAR_W          = 320;
    constexpr int16_t TOP_BAR_H          = 24;
    constexpr int16_t TOP_BAR_HIT_H      = 30;  // 纵向点击响应容差高度 (0~29)
    constexpr int16_t HITBOX_TIME_W      = 95;  // X: 0~95 点击直达 Page 7 世界时钟 // X: 0~110 点击直达 Page 7 世界时钟
    constexpr int16_t STATUS_BADGE_X     = 96;  // X: 96~225 常驻状态与电池区 // X: 111~208 中央微型状态徽章区
    constexpr int16_t STATUS_BADGE_W     = 130;
    constexpr int16_t HITBOX_WEATHER_X   = 226; // X: 226~319 点击直达 Page 6 天气预报 // X: 210~319 点击直达 Page 6 天气预报
    constexpr int16_t HITBOX_WEATHER_W   = 94;
    
    // 页面全宽主内容区 (Full-Width Safe Area - 彻底释放两侧 8px)
    constexpr int16_t SAFE_X     = 0;
    constexpr int16_t SAFE_Y     = 24;
    constexpr int16_t SAFE_W     = 320;
    constexpr int16_t SAFE_H     = 216; // 纵向全画幅 (240 - 24)
    
    // 悬浮式底部指示器 (Floating OSD Overlay)
    constexpr int16_t  INDICATOR_OSD_Y        = 228;
    constexpr int16_t  INDICATOR_OSD_H        = 8;
    constexpr uint32_t INDICATOR_AUTO_HIDE_MS = 3000; // 3 秒无交互自动彻底隐藏
}


// Base Dual 16340 板载 INA226 高精度功耗/电压/电流监测配置
namespace Ina226Config {
    constexpr uint8_t  I2C_ADDR           = 0x45; // INA226 7位 I2C 物理地址
    constexpr float    SHUNT_RESISTOR_OHM = 0.02f; // 20 毫欧精密分流采样电阻
    constexpr uint32_t SAMPLE_INTERVAL_MS = 500;  // 500ms 刷新一次功率与电流
}

// INA226 实时电源监测数据契约

enum BatteryHardwareMode : uint8_t {
    BATTERY_MODE_NO_BASE_EMPTY = 0, // 阶段一：未检测到底座/电池，屏幕电量显示为空 (🪫 0%)
    BATTERY_MODE_BASE_ATTACHED = 1  // 阶段二：自动探测到 Base Dual 16340 (🔋 85% 真实电量)
};

struct PowerMonitorData {
    bool  isAvailable;        // INA226 是否在线正常响应
    float busVoltageV;        // 实时总线电压 (V, 正常范围 3.0 ~ 4.2V)
    float shuntVoltageMv;     // 分流电阻电压降 (mV)
    float currentMa;          // 实时电流 (mA，正值表示充电输入，负值表示系统耗电)
    float powerMw;            // 即时功耗 (mW)
    float batterySocPercent;  // 结合电压与安时积分计算的电量百分比 (0~100 %)
    uint16_t remainingMinutes;// 结合当前耗电速度预测的剩余可用运行分钟数
};


// 手势识别阈值规范
namespace GestureConfig {
    constexpr int16_t SWIPE_HORIZ_THRESHOLD_PX = 50;  // 左右滑动手势最小位移
    constexpr int16_t SWIPE_UP_THRESHOLD_PX    = -40; // 向上滑动手势最小负向位移
    constexpr float   DIRECTION_RATIO          = 1.5f;// 主向位移/副向位移判定比率
}

// 车辆运动唤醒与静止休眠参数规范
namespace MotionPowerConfig {
    constexpr uint32_t HOURLY_SPLIT_POINTS       = 3600;    // 每小时截断生成新文件 (3600点 / 方案B)
    constexpr uint32_t STANDSTILL_STOP_RECORD_MS = 900000;  // 15 分钟静止自动停止轨迹记录并闭合文件 (900s)
    constexpr uint32_t STANDSTILL_TIMEOUT_MS     = 1800000; // 30 分钟无移动自动切入整机休眠 (1800s)
    constexpr uint32_t SILENT_ASSESS_WINDOW_MS   = 3000;    // 3 秒静默评估窗口 (防误开机)
    constexpr float    STATIC_VARIANCE_THRESH    = 0.005f;  // 地库无GPS时IMU静止方差门限 (m/s^2)
    constexpr float    SUSTAINED_ACCEL_THRESH    = 0.12f;   // 真实起步持续推背加速度 (G)
    constexpr uint32_t SUSTAINED_ACCEL_MS        = 1500;    // 持续推背时间门限 (ms)
    constexpr uint16_t BATTERY_CAPACITY_MAH      = 1400; // Base Dual 16340 并联双电芯标准容量     // M5GO Bottom2 独立供电容量 (Core2 390mAh底壳已拆除)
    constexpr float    BATTERY_CHARGE_MAX_TEMP_C = 55.0f;   // 高温暴晒停止给电池充电阈值 (°C)
    constexpr float    BATTERY_CHARGE_RESUME_TEMP= 45.0f;   // 降温恢复正常充电阈值 (°C)
    constexpr uint32_t STORAGE_WARN_MB           = 200;     // 存储空间安全低水位告警 (MB)
    constexpr uint32_t STORAGE_RECOVERY_MB       = 500;     // 循环清理后目标安全水位 (MB)
    constexpr float    KINEMATIC_MAX_SPEED_KMH   = 180.0f;  // 动力学最大合理车速突变门限 (km/h)
    constexpr float    KINEMATIC_MAX_ACCEL_G     = 1.2f;    // 动力学最大合理加速度门限 (G)
}
```

---

## 2. u-blox NEO-M9N 通信与 AGPS 协议规范

### 2.1 串口与协议配置
- **通信接口**：ESP32 UART2 (Serial2), RX: GPIO13, TX: GPIO14。
- **初始波特率**：38400 bps（上电初始化后通过 UBX-CFG-PRT 提升至 115200 bps 以支撑 5Hz/10Hz 高频推送）。
- **启用报文**：
  - `UBX-NAV-PVT` (0x01, 0x07)：高频导航位置、速度、时间综合报文（输出频率 5Hz）。
  - `UBX-NAV-SIG` (0x01, 0x43)：多星载噪比与信号质量报文（输出频率 1Hz）。
  - 关闭繁重的 NMEA 文本输出，仅保留纯二进制 UBX 协议以节省 ESP32 CPU 解析负担。

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
- **处理方式**：ESP32 以 128~512 字节分块流式读取 AssistNow 二进制流，调用官方 API `myGNSS.pushRawData(buffer, bytesRead)` 直接注入 NEO-M9N 串口解算链路。

### 2.4 车载环境专项 UBX 配置规范 (In-Cabin UBX-CFG)
- **动态平台模型 (`UBX-CFG-NAV5`)**：
  - `dynModel = 4` (`DYN_MODEL_AUTOMOTIVE`，车载模式)。
  - `fixMode = 3` (Auto 2D/3D)。
- **静止死区锁定 (`staticHoldThresh`)**：
  - `staticHoldThresh = 50` ($0.5\text{ m/s} \approx 1.8\text{ km/h}$)。
  - 触发行为：车速低于门限时，水平航向固定、车速强制报告为 0，经纬度锁止不动。
- **信号质量门限 (`UBX-CFG-NAV5` & `UBX-CFG-NMEA`)**：
  - `minElev = 12` (截断 $\le 12^\circ$ 低仰角地平线/机盖杂波)。
  - `cnoThresh = 24` (低于 24 dBHz 弱星不参与 PVT 最小二乘解算)。
- **多星系统配置 (`UBX-CFG-GNSS`)**：
  - `GPS (L1C/A)`：启用 (min 8, max 16 追踪通道)
  - `BeiDou (B1I)`：启用 (min 8, max 16 追踪通道)
  - `GLONASS (L1OF)`：启用 (min 6, max 12 追踪通道)
  - `Galileo (E1B/C)`：启用 (min 6, max 12 追踪通道)

### 2.5 复合模块板载 BMP280 气压/高程融合规范
- **硬件通信**：ESP32 硬件 I2C，7 位从机地址 `0x76`。
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
- **WGS-84 原始轨迹**：`W-yyyymmdd-hhmmss.gpx`（如 `W-20261028-090000.gpx`，直出 GPS 真实坐标）
- **GCJ-02 火星坐标轨迹**：`C-yyyymmdd-hhmmss.gpx`（如 `C-20261028-090000.gpx`，China 国测局加密转换，中国大陆道路无偏移贴合）

```xml
<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.1" creator="M5Track-M9N" 
     xmlns="http://www.topografix.com/GPX/1/1"
     xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
     xmlns:gpxtpx="http://www.garmin.com/xmlschemas/TrackPointExtension/v1"
     xmlns:m5="http://m5stack.com/xmlschemas/VehicleTrack/v1"
     xsi:schemaLocation="http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd">
  <metadata>
    <name>Track 2026-10-27 15:45:00</name>
    <desc>M5Track-M9N Vehicle High-Precision Log (1Hz)</desc>
    <time>2026-10-27T07:45:00.000Z</time>
  </metadata>
  <trk>
    <name>M5Core2 Vehicle Drive</name>
    <trkseg>
      <trkpt lat="31.2304168" lon="121.4737012">
        <!-- 1. 空间与大地三维坐标 (Standard GPX) -->
        <ele>128.45</ele>
        <time>2026-10-27T07:45:00.000Z</time>
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
          <m5:sats_bds>11</m5:sats_bds>
          <m5:sats_gps>7</m5:sats_gps>
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

## 4. Web 配网与文件管理 API 规范 (Captive Portal & SoftAP)

系统所有 Web 交互**均以按需启动的独立 SoftAP 热点模式提供**（不开放 STA 局域网端口，保障车载安全与低功耗）：
- **配网模式**：AP SSID `M5Track-Setup`，IP: `192.168.4.1`。
- **TF 卡文件下载模式**：AP SSID `M5Track-Share`，IP: `192.168.4.1`（仅在触发“通过 Wi-Fi 下载”时开启，退出菜单即关闭）。

| 方法 | 路径 | 描述 | 请求格式 | 响应格式 (JSON) |
| :--- | :--- | :--- | :--- | :--- |
| `GET` | `/` | 配网与管理单页应用 | 无 | HTML5 + CSS (全黑夜视风格，适配移动端) |
| `GET` | `/api/scan` | 扫描周围可用 Wi-Fi | 无 | `{"aps":[{"ssid":"Home","rssi":-55,"auth":3},...]}` |
| `GET` | `/api/saved` | 获取已保存的 Wi-Fi 清单 | 无 | `{"saved":[{"ssid":"Home"},{"ssid":"Car_Hotspot"}]}` |
| `POST` | `/api/connect` | 保存并尝试连接指定网络 | `{"ssid":"...","pwd":"..."}` | `{"success":true,"message":"Connecting..."}` |
| `POST` | `/api/delete` | 删除已保存的指定 Wi-Fi | `{"ssid":"..."}` | `{"success":true}` |
| `GET` | `/api/tracks` | 查询 TF 卡内所有 GPX 文件及坐标系属性 | 无 | `{"files":[{"name":"W-20261028-090000.gpx","size":654320,"type":"wgs84","badge":"国际/Google Earth版"},{"name":"C-20261028-090000.gpx","size":654320,"type":"gcj02","badge":"国内/高德/两步路版"}]}` |
| `GET` | `/api/download`| 下载指定轨迹文件 | Query: `?file=W-20261028-090000.gpx` | 二进制流 / `application/gpx+xml` |
| `POST`| `/api/sd/format`| 格式化 TF 卡并重建 tracks 目录 | `{"confirm":true}` | `{"success":true,"message":"SD Formatted"}` |
| `GET` | `/api/tunnel/info` | 查询片上 Flash 隧道库版本与条目数 | 无 | `{"version":"2026.10","count":82,"source":"LittleFS"}` |
| `POST`| `/api/tunnel/upload`| 上传更新片上 Flash 隧道数据库 | Multipart / Binary | `{"success":true,"count":85,"message":"Tunnel DB Reloaded to PSRAM"}` |

---

## 5. 片上 Flash 分区表规范 (`partitions.csv` - 16MB NOR Flash)

为保障特长隧道数据库与系统运行互不干扰，在板载 16MB Flash 中划分独立 `LittleFS` 分区：

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x640000,
app1,     app,  ota_1,   0x650000,0x640000,
littlefs, data, spiffs,  0xc90000,0x300000,
```
- `littlefs` 分区分配 **3MB**，挂载路径 `/littlefs`。
- 隧道数据库文件：`/littlefs/tunnels.bin`（约 30KB~60KB）。
- 启动时全量读入 8MB PSRAM 内存缓存，耗时 $<2\text{ms}$，运行时零 Flash/TF I/O 耗时。

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
