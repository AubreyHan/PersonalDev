# Vibe Coding 实施路线图 (IMPLEMENTATION PLAN) - M5Track-Tab5

本路线图专为 **Antigravity AI 编程助手** 进行逐步、原子化开发而设计。
每个阶段包含清晰的目标、修改的文件、原子化任务清单与**确定的验证命令与检查点**。

---

## 阶段概览 (Phases Overview)

- **Phase 1**：PlatformIO 项目基建、Tab5 (ESP32-P4) 硬件初始化与 1280×720 MIPI 宽屏验证
- **Phase 2**：NEO-M9N 高速串口驱动、车载工况调优与气压高程互补融合
- **Phase 3**：双源自适应时钟引擎 (RX8130CE RTC ⇋ GPS ⇋ NTP) 与 u-blox AGPS 星历注入
- **Phase 4**：MicroSD 卡挂载与双流 GPX (WGS-84 / GCJ-02) 增量写盘与 1 小时切片服务
- **Phase 5**：Tab5 板载 INA226 (`0x40`) 复合软硬件库仑计与 IP2326 充放电管理服务
- **Phase 6**：ESP32-C6 Wi-Fi 6 管理器、SoftAP Web 配网与轨迹无线极速导出
- **Phase 7**：全黑车规 UI、左侧 160px 常驻 Dock、初始化搜星页与 8 大宽屏主页面开发
- **Phase 8**：双核多任务全链路集成、系统联调与极限工况稳定性压力测试

---

## Phase 1: 项目脚手架与宽屏环境验证 (Scaffolding & Orientation)

### 目标
建立 PlatformIO 项目工程，引入 `M5Unified` 与 `M5GFX`，配置目标板为 `m5stack-tab5`（ESP32-P4），挂载片上 `LittleFS`，验证 1280×720 高清横屏纯黑背景绘制与电容触控坐标映射。

### 原子任务清单
- [ ] **Task 1.1**：创建 `platformio.ini` 与 `partitions.csv`，配置 `board = m5stack-tab5`，16MB Flash 分区表（包含 3MB 独立 `LittleFS` 分区），引入库依赖：`m5stack/M5Unified`, `m5stack/M5GFX`, `sparkfun/SparkFun u-blox GNSS Arduino Library`, `greiman/SdFat`。
- [ ] **Task 1.2**：编写 `src/config.h` 与 `src/main.cpp`，调用 `M5.begin()`，挂载片上 `LittleFS`。
- [ ] **Task 1.3**：配置屏幕方向 `M5.Display.setRotation(1)`（1280 × 720 标准高清横屏），全屏清屏为纯黑色 `TFT_BLACK`，绘制居中荧光绿文本 `"M5Track-Tab5 Ready"`，测试电容多点触控并打印串口日志。
- [ ] **Task 1.4**：创建 `src/driver/I2cBusManager.h/.cpp`，扫描 I2C 硬件物理总线，确认 Tab5 板载外设（BMI270 主 IMU `0x68`、RX8130CE `0x32`、INA226 `0x40`）与 Module GNSS 外设（BMI270 副 IMU `0x69` 出厂硬件上拉响应、BMM150 `0x10`、BMP280 `0x76`）均正常响应 ACK，验证双 IMU 正交就绪。

### 验收与验证指令
- **编译检查**：
  ```bash
  pio run
  ```
  *预期输出*：`SUCCESS`，无编译报错。
- **烧录运行**：
  ```bash
  pio run -t upload
  ```
- **视觉验证**：屏幕 1280×720 横屏黑底绿字，文字清晰高分无错位；触摸屏幕四角与中央，串口输出精准的 `(X, Y)` 触控坐标。

---

## Phase 2: NEO-M9N 串口驱动、车载工况调优与气压高程融合 (GNSS & Baro-Fusion)

### 目标
在独立 Core 0 任务中驱动 M5-Bus 高速串口（`GPIO 38 RX0 / 37 TX0`）与板载 BMP280 气压计，注入车载专用调优参数（车载模式、静止死区锁、四星并发），实现气压-GNSS 高程互补滤波融合算法。

### 原子任务清单
- [ ] **Task 2.1**：创建 `src/model/SystemState.h`，定义 `GnssData`、`SatInfo` 及互斥锁 `SemaphoreHandle_t`。
- [ ] **Task 2.2**：实现 `src/driver/GnssDriver.h/.cpp`，初始化 `Serial2.begin(38400, SERIAL_8N1, 38, 37)`，下发 UBX 命令切换模组波特率至 115200 bps 并重新握手。
- [ ] **Task 2.3**：配置 NEO-M9N 仅输出 UBX 二进制协议（关闭 NMEA），开启 5Hz 导航解算频度。
- [ ] **Task 2.4**：实现车内工况专项固件调优与 MagSafe 强磁免疫：
  - 动态平台切换为 `DYN_MODEL_AUTOMOTIVE`（车载模式，抗 4G 极限加减速）；
  - 设置 `setStaticHoldThreshold(50)`（0.5 m/s 静态门限，杜绝等红灯原地漂移）；
  - **MagSafe 强磁免疫**：完全旁路并屏蔽 BMM150 地磁计，由 GPS 地面航向与 BMI270 六轴陀螺仪 Z 轴角速度（50Hz 积分）互补融合，车速 $\le 1.8\text{km/h}$ 停稳时激活静态航向锁死；
  - 设置卫星最低仰角掩码为 $12^\circ$，载噪比门限为 $24\text{ dBHz}$；
  - 开启 RAIM 完好性监测与多径抗扰抑制；
  - 增加动力学抗跳变离群滤波：剔除 1 秒内速度突变 $>180\text{km/h}$ 或加速度 $>1.2\text{G}$ 的粗大飞点；
  - 锁定使能 BDS + GPS + GLO + GAL 四星并发。
- [ ] **Task 2.5**：开发 `src/driver/BaroDriver.h/.cpp`，通过硬件 I2C（地址 `0x76`）初始化模块板载 BMP280 传感器，实现 10Hz 超高分辨率气压与温度采样。
- [ ] **Task 2.6**：开发 `src/service/AltitudeFusion.h/.cpp`，实现高程互补滤波算法：以 GPS 3D 高度动态修正海平面基准气压 $P_0$，以气压高频相对位移滤波生成分米级平滑定高。
- [ ] **Task 2.7**：开发 `src/driver/ImuDriver.h/.cpp` 与 `src/service/DeadReckoningService.h/.cpp`：
  - 实现双 BMI270（主 `0x68` + 副 `0x69`）50Hz 并行采样、加权均值滤波（降噪 30%）与交叉互验；
  - 实现 Master 芯片故障时 20ms 内无缝容灾热切换至 Slave 芯片；
  - 监听 GNSS 信号状态，当 3D Fix 跌落至 No Fix 时无缝切入推算模式；
  - 结合双 IMU 融合后的角速度积分（Yaw $\omega_z$）与加速度进行平面航位递推（DR）；
  - 持续融合 BMP280 气压高度，出盲区后 2 秒低通平滑恢复。
- [ ] **Task 2.8**：开发 `src/service/TunnelService.h/.cpp` 与 `src/service/TunnelDb.h`：
  - 从板载 Flash `LittleFS` (`/littlefs/tunnels.bin`) 读取并在 2ms 内载入 32MB PSRAM 内存缓存；
  - 入口 200m 空间哈夫曼距离与航向自动匹配触发；
  - 进隧道后将 DR 轨迹完全锁死在真实单维矢量轴线上，彻底消除 15~20 分钟超长盲区的横向零漂。
- [ ] **Task 2.9**：实现射频抗干扰与天线回路诊断：周期性（每 2 秒）轮询 `UBX-MON-RF`，解析 `jamFlags` 干扰程度与 `antStatus` 开路/短路状态。
- [ ] **Task 2.10**：实现 FreeRTOS 任务 `gnssTask`，循环读取 `myGNSS.checkUblox()` 与气压计，输出经纬度、车速、航向角、融合海拔、四星状态、DR 推测与射频状态标记。

### 验收与验证指令
- **车载工况与高程融合检查**：
  ```bash
  pio device monitor -b 115200
  ```
  *预期输出*：每 200ms 打印一次：`[GNSS] Fix: 3D, Sats: 22 (BDS:10, GPS:8, GLO:4), Speed: 0.0 km/h, BaroAlt: 128.4m, FusionAlt: 128.5m`。

---

## Phase 3: 双源自适应时钟引擎与 AGPS 注入 (Time & AGPS)

### 目标
实现 Wi-Fi NTP 与 GPS 授时的自适应仲裁闭环，封装 Tab5 板载 RX8130CE 硬件 RTC（`0x32`），并将高精度时间与星历回哺注入 NEO-M9N。

### 原子任务清单
- [ ] **Task 3.1**：创建 `src/service/TimeService.h/.cpp`，封装 RX8130CE 硬件 RTC 读写与 `configTzTime()`。
- [ ] **Task 3.2**：实现 NTP 授时逻辑：当 Wi-Fi 连接且 GPS 未定位时，拉取网络时间更新系统时钟与硬件 RTC；构建 `UBX-MGA-INI-TIME_UTC` 报文直接写向 M9N 串口。
- [ ] **Task 3.3**：实现 GPS 授时反哺逻辑：当 `NAV-PVT` 报文显示 3D Fix 且时钟有效时，以 GPS 原子钟校准硬件 RTC。
- [ ] **Task 3.4**：开发 `src/service/AgpsService.h/.cpp`，从 u-blox AssistNow 服务器拉取多星星历，分块写入串口，实现 3~5 秒冷启定位。

---

## Phase 4: MicroSD 卡双流并行落盘与 1 小时切片服务 (Safe GPX Logging)

### 目标
基于 `SdFat` 库在专用 SPI 接口上驱动 TF 卡，每秒并行追加 WGS-84 与 GCJ-02 双 GPX 文件，实现整 1 小时自动切片与 4 级掉电安全保护。

### 原子任务清单
- [ ] **Task 4.1**：创建 `src/service/TrackLogger.h/.cpp`，基于 `SdFat` 驱动 TF 卡槽，开启长文件名 (LFN) 支持；实现轻量级 WGS-84 转 GCJ-02 坐标加密算法。
- [ ] **Task 4.2**：实现双流建档逻辑：当 `车速 > 3.0 km/h` 且 `fixType >= 3D Fix` 时，在 `/tracks/` 下并行创建 `W-yyyymmdd-hhmmss.gpx` 与 `C-yyyymmdd-hhmmss.gpx`。
- [ ] **Task 4.3**：实现 1 小时（3600 点）严格连续切卷机制与时序递增校验。
- [ ] **Task 4.4**：实现 50% 容量硬性限额、循环覆盖与 4 级掉电保护流水线：
  - 实时监测 `/tracks/` 目录总体积，严格限制不超过 TF 卡总容量的 50%（如 32GB 卡上限 16GB）；
  - 达 50% 上限时自动成对删除最旧的 `W-` 与 `C-` 历史轨迹，释放至 40% 以下；
  - 实现实时尾标签预封口、10 秒定时 `file.sync()`、开机自动扫描并修补未闭合文件。

---

## Phase 5: Tab5 板载 INA226 复合库仑计与 IP2326 充放电管理 (Power Service)

### 目标
驱动 Tab5 主板板载 INA226 芯片（I2C `0x40`），结合 $20\text{m}\Omega$ 分流电阻，开发软硬件复合库仑计（安时积分 + 11 节点 OCV 曲线校准），实现 NP-F550 电池状态与车载 USB-C / HVIN 供电监控。

### 原子任务清单
- [ ] **Task 5.1**：创建 `src/driver/Ina226Driver.h/.cpp`，初始化 INA226 寄存器（配置字 `0x4127`，校准字 `CAL = 2560`，电流分辨率 $0.1\text{mA/bit}$）。
- [ ] **Task 5.2**：开发 `src/service/PowerMonitorService.h/.cpp`，FreeRTOS 500ms 定时采样总线电压 (mV)、瞬态电流 (mA) 与即时功耗 (mW)。
- [ ] **Task 5.3**：实现动态安时积分器：按 $\Delta Q = I \times \Delta t \times \eta$ 维护 NP-F550 电池组（标称 2000mAh，2串 7.4V）剩余容量。
- [ ] **Task 5.4**：实现静态 OCV 零漂自动校准：当 $|I| < 20\text{mA}$ 维持超 5 秒时，根据 7.4V (2S) 11 节点 OCV 标定表强制对冲累积漂移。
- [ ] **Task 5.5**：输出精准电池百分比 (0~100%)、充放电状态与外电判定（`hasExternalPower`）；有外接电源输入时驱动电池颜色为绿色 (`#00FFAA`)，无外接电源时驱动为红色 (`#FF3B30`)。

---

## Phase 6: ESP32-C6 Wi-Fi 6 管理器与专属 SoftAP 导出服务 (Network & Web)

### 目标
驱动 ESP32-C6 协处理器管理 Multi-SSID 自动漫游，并提供仅在需要时按需启动的 SoftAP 专属热点（`M5Track-Share`），提供双坐标系 GPX 网页极速直连下载。

### 原子任务清单
- [ ] **Task 6.1**：开发 `src/service/NetworkManager.h/.cpp`，支持 NVS 存储 5 组已知车载 Wi-Fi。
- [ ] **Task 6.2**：开发 SoftAP Captive Portal 配网网页与 REST API（`/api/scan`, `/api/connect`, `/api/delete`）。
- [ ] **Task 6.3**：开发专属文件共享服务：一键启动 `M5Track-Share`（`192.168.4.1`），手机直连浏览器免拔卡批量下载 W/C 轨迹，退出二级菜单即刻销毁热点。

---

## Phase 7: 全黑车规 UI、左侧 160px 常驻 Dock 与 8 大宽屏主页面开发 (Presentation)

### 目标
基于 `M5GFX` 充分利用 32MB PSRAM 分配 1280×720 双缓冲画布（60fps 刷新），构建左侧 160px 常驻 Dock，开发开机自检搜星页与 8 个全高（1120×720）正式主页面。

### 原子任务清单
- [ ] **Task 7.1**：实现 `src/ui/UiManager.h/.cpp`，初始化 1280×720 纯黑画布，统一调度全屏左右滑动手势。
- [ ] **Task 7.2**：开发 `src/ui/components/LeftDock.h/.cpp`，实现最左侧 160px 常驻侧边栏：
  - 布局 7 大触控块（`🏠 仪表`、`14:35 时钟`、`🛰️ 18 定位`、`📶 热点`、`🔋 95% 电池`、`☀️ 24℃ 气象`、`⚙️ 工具`）；
  - 实现电池模块双色渲染：接通外接电源时电池图标与百分比高亮呈现绿色 (`#00FFAA`)，无外接电源（纯电池）时呈现红色 (`#FF3B30`)；
  - 实现各功能块专职跳转路由，彻底消除手势与功能打架。
- [ ] **Task 7.3**：实现 `src/ui/pages/BasePage.h`：统一封装右侧全高安全剪裁视口（`canvas.setClipRect(160, 0, 1120, 720)`）。
- [ ] **Task 7.4**：开发 **PageInit**（开机自检搜星页）：硬件自检状态、全景动态搜星雷达、TF 卡不兼容自动提示格式化、Wi-Fi 已连状态（SSID/信号/IP）与配网按钮。
- [ ] **Task 7.5**：开发 **Page1Dashboard**（核心主仪表盘）：左侧 360px 大圆形罗盘 + 右侧 120pt 发光数码主车速、实时海拔与 TRIP 里程卡片；**实现长按 TRIP 卡片 2 秒触发机身马达短震与全系统联动归零重置**。
- [ ] **Task 7.6**：开发 **Page2Altitude**（海拔起伏折线页）：顶部实时海拔与累计爬升；中部 1050px 超宽幅 1 小时连续起伏折线图；底部最高/最低海拔极值。
- [ ] **Task 7.7**：开发 **Page3Speed**（车速折线与极速详情页）：顶部实时车速与最高时速 (MAX)、平均时速 (AVG)，坚决不显示最低时速；中部 1050px 宽幅车速折线图；底部高速/城市/缓行工况分布与行驶/怠速用时。
- [ ] **Task 7.8**：开发 **Page4Trail**（矢量自驾轨迹与返航地图）：
  - 在 32MB PSRAM 中维护抽稀航迹链表；
  - **实现轨迹全画幅极限自适应铺满整个 1120×720 画布**，四角半透明悬浮 HUD 胶囊显示直线距起点距离与朝向。
- [ ] **Task 7.9**：开发 **Page5Dynamics**（车辆动态姿态仪）：左侧 400px 超大 G-Force 摩擦力靶图（带半秒动态拖尾与极限横向/纵向 G 记忆）；右侧道路实时坡度百分比（+7.5% 陡坡警示）与车身俯仰/侧倾仪。
- [ ] **Task 7.10**：开发 **Page6Weather**（12小时两段大字气象预报页）：顶部实时气温与天气图标；中部每小时 93px 宽大字发光温度折线与降水概率柱状条，右上角胶囊按键实现【前12小时】与【后12小时】一键秒切；底部行车预警与离线缓存。
- [ ] **Task 7.11**：开发 **Page7WorldClock**（多时区世界时钟页）：2×2 宽幅 530×320px 航空级大卡片（本地/UTC/纽约夏令时/东京）；单击进入 `SubPageEditClock` 编辑子页。
- [ ] **Task 7.12**：开发 **PageToolsGrid**（系统扩展工具箱）：2×2 宽幅卡片（TF 管理 / Wi-Fi 管理 / GPS 卫星详情 / 系统偏好与 INA226 库仑计功耗诊断）；支持工具多于 4 个时自动扩页。

---

## Phase 8: 多核多任务全链路集成与极限稳定性测试 (Integration & Testing)

### 目标
完成 ESP32-P4 双核任务绑定、传感器长时间压力测试与行车极限场景演练。

### 原子任务清单
- [ ] **Task 8.1**：多核调度联调：Core 0（`gnssTask` + `netTask`），Core 1（`uiTask` + `trackLoggerTask` + `pwrTask`）。
- [ ] **Task 8.2**：极限工况模拟与验证：
  - **TRIP 长按 2 秒归零全链路测试**：验证马达震动、Page 1 里程归零、Page 2 极值归位、Page 3 均速重计、Page 4 轨迹重锚、Page 5 峰值 G 清零及 GPX 写入新分段；
  - **高频并发稳定性测试**：连续 48 小时 10Hz 定位、1Hz GPX 双流写盘、60fps UI 渲染，监测 Free Heap 与 PSRAM 泄漏（泄漏为 0）；
  - **瞬态掉电自愈测试**：在行驶录制中强行拔掉车充，开机自检自动扫描并 100% 修复 GPX XML 标签，零文件损坏。
