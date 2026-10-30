# 技术架构设计 (ARCHITECTURE) - M5Track-Tab5

---

## 1. 硬件连接与引脚定义 (Hardware Interconnect)

系统主控平台为 **M5Stack Tab5**（主控芯片为高性能双核 RISC-V 400MHz 的 **ESP32-P4**，搭配 16MB Flash 与 **32MB 超大 PSRAM**；无线协处理器为 **ESP32-C6**），外部扩展模组选用 **M5Stack Module GNSS (M135 / u-blox NEO-M9N)**。

### 1.1 硬件通信拓扑与引脚映射

| 外部模块 / 内部外设 | 接口协议 | ESP32-P4 引脚映射 | 说明 |
| :--- | :--- | :--- | :--- |
| **u-blox NEO-M9N (M135)** | 高速 UART | **GPIO 38 (RX0)** $\leftarrow$ M9N TXD<br>**GPIO 37 (TX0)** $\rightarrow$ M9N RXD | 经由 M5-Bus 高速串口总线互联，波特率 38400 / 115200bps；SMA 标准外置天线接口引向前挡玻璃 |
| **BMP280 (气压/高程计)** | 硬件 I2C | GPIO 7 (SDA), GPIO 8 (SCL) | Module GNSS 板载集成，I2C 地址 `0x76`；用于 10Hz 气压定高互补融合与道路坡度计算 |
| **BMM150 (3轴地磁罗盘)** | 硬件 I2C | GPIO 7 (SDA), GPIO 8 (SCL) | Module GNSS 板载集成，I2C 地址 `0x10`；用于 0 车速静止状态下绝对磁北航向保持 |
| **BMI270 (6轴 IMU 惯导)** | 硬件 I2C | GPIO 7 (SDA), GPIO 8 (SCL) | Tab5 主板板载集成，I2C 地址 `0x68`；用于车身姿态角、G-Force 摩擦力靶图、隧道惯导推算与静默微动唤醒 |
| **RX8130CE (硬件 RTC)** | 硬件 I2C | GPIO 7 (SDA), GPIO 8 (SCL) | Tab5 主板板载集成，I2C 地址 `0x32`；由 $70000\mu\text{F}$ 超级电容维持掉电走时，实现冷启动秒级注入 |
| **INA226 (高精度库仑计)** | 硬件 I2C | GPIO 7 (SDA), GPIO 8 (SCL) | Tab5 主板板载集成，I2C 地址 `0x40`；经 $20\text{m}\Omega$ 精密分流电阻监测电压、充放电电流与整机功耗 |
| **IP2326 + MP4560** | 电源与充放电管理 | 内部硬件管理电路 | 管理背部快拆 **NP-F550** (约 2000mAh/7.4V) 锂电池，支持 USB-C (PD/QC) 及 6~24V HVIN 宽压车载供电 |
| **5.0" IPS 高清屏** | MIPI-DSI (双通道) | ESP32-P4 专用 MIPI-DSI 接口 | 1280×720 分辨率，60Hz 刷新率，支持全贴合大画幅双缓冲渲染 |
| **电容触控控制器** | 专用触控 I2C / INT | 专用触控总线 | 支持多点触控、大热区点选与全屏滑动手势识别 |
| **MicroSD (TF 卡槽)** | 专用硬件高速 SPI | 专用 SPI 接口 | 采用 `SdFat` 库，原生支持长文件名 (LFN) 与机内 FAT32 格式化，双流记录 GPX |
| **触觉马达 / 蜂鸣器** | 专用 PWM / GPIO | 专用控制引脚 | Page 1 里程卡片长按 2 秒归零短震触觉确认，超速/陡坡声音警示 |

> [!TIP]
> **I2C 物理总线正交架构**：
> - Tab5 板载芯片：`BMI270` (`0x68`)、`RX8130CE` (`0x32`)、`INA226` (`0x40`)；
> - Module GNSS 堆叠芯片：`BMM150` (`0x10`)、`BMP280` (`0x76`)；
> 5 大传感器地址完全独立，I2C 总线零冲突，物理寻址极其轻量稳定。

---

## 2. 屏幕朝向与车规宽屏视口划分 (Viewport & Left Dock)

依据 M5GFX 官方硬件标准定义：
- M5Stack Tab5 在 `M5GFX` 库中采用标准横屏车载仪表盘模式：
  - `M5.Display.setRotation(1)`：**标准宽屏 (1280 × 720)**；
  - **物理布局**：机身侧面集成标准 **1/4"-20 摄影/车载螺孔**，直连车载吸盘球头支架；背部支持 NP-F550 电池卡扣快拆；SMA 外置天线接头由 Module GNSS 稳固引出；正面为整块全贴合高清防刮触控玻璃。
- **全屏坐标划分布局**：

```text
+------+-------------------------------------------------------------+ (1280 x 720)
|  🏠  |                                                             |
| 仪表 |                                                             |
|------|                                                             |
|14:35 |                                                             |
|28 秒 |                                                             |
|------|                                                             |
| 🛰️   |                                                             |
|18 3D |                   【 主内容安全绘制视口 】                  |
|------|                                                             |
| 📶   |                       1120 x 720 像素                       |
|热点  |                   (X: 160~1279, Y: 0~719)                   |
|------|                                                             |
| 🔋   |                   (BasePage 独占 720px 全高)                |
| 95%  |                                                             |
|------|                                                             |
| ☀️   |                                                             |
|24.5℃ |                                                             |
|------|                                                             |
|  ⚙️  |                       ● ○ ○ ○ ○ ○ ○ ○                       |
| 工具 |                 (切页浮现，3秒无操作自动隐藏)               |
+------+-------------------------------------------------------------+
(左侧160px 常驻Dock)                  (右侧1120px 全高主视口)
```

1. **左侧常驻触控 Dock (`X: 0~159, Y: 0~719`, 宽 160px, 全高 720px)**：
   - 彻底解放纵向像素，为驾驶员右手提供 6 大高频触控方块（每个方块约 $160 \times 105\text{px}$，触控面积极大，盲操容错率 100%）：
     - `🏠 仪表`：专职回首页；
     - `14:35`：专职跳 Page 7 世界时钟；
     - `🛰️ 18`：专职跳 GPS 详情；
     - `📶 热点`：专职跳网络配网；
     - `🔋 95%`：INA226 库仑计实时电量与充放电；
     - `☀️ 24.5℃`：专职跳 Page 6 天气预报；
     - `⚙️ 工具`：专职跳 Page 8 系统工具箱。
2. **右侧主内容安全视口 (`X: 160~1279, Y: 0~719`, 宽 1120px, 高 720px)**：
   - 比例为接近 16:10 的黄金视窗（1120×720），所有子页面均在此区域内进行全画幅独立绘制。

---

## 3. 系统整体架构拓扑图 (System Architecture)

```mermaid
flowchart TB
    subgraph Hardware Layer ["硬件外设层 (Hardware Layer)"]
        N9N["u-blox NEO-M9N<br>(M5-Bus UART 38/37)"]
        BARO["BMP280 Baro<br>(I2C 0x76)"]
        MAG["BMM150 Compass<br>(I2C 0x10)"]
        IMU["BMI270 6-Axis<br>(I2C 0x68)"]
        RTC["RX8130CE RTC<br>(I2C 0x32)"]
        INA["INA226 Fuel Gauge<br>(I2C 0x40)"]
        LCD["5.0寸 IPS LCD<br>(MIPI-DSI 1280x720)"]
        SD["MicroSD Slot<br>(Hardware SPI)"]
        C6["ESP32-C6 Co-SoC<br>(Wi-Fi 6 / BLE 5)"]
    end

    subgraph Driver & HAL ["驱动与硬件抽象层 (M5Unified / HAL)"]
        drv_gnss["SparkFun u-blox GNSS Lib<br>(UBX-NAV-PVT / UBX-MGA)"]
        drv_i2c["I2C Bus Manager<br>(0x68/0x32/0x40/0x76/0x10)"]
        drv_rtc["RX8130CE RTC Driver"]
        drv_ina["INA226 Driver & Regs"]
        drv_gfx["M5GFX MIPI Engine<br>(32MB PSRAM Double Buffering)"]
        drv_tp["M5.Touch Gesture Manager"]
        drv_fs["SdFat File System Driver"]
    end

    subgraph Service Engines ["核心业务引擎层 (Core Services)"]
        engine_gnss["GNSS 数据解算器<br>(车载动态模式/静止死区锁/四星)"]
        engine_baro["气压-GNSS 高程互补滤波引擎<br>(BMP280 / 分米级平滑定高)"]
        engine_time["双源自适应时钟引擎<br>(GPS原子钟 ⇋ NTP ⇋ RX8130CE)"]
        engine_agps["u-blox AGPS (AssistNow) 极速注入服务"]
        engine_pwr["INA226 复合库仑计服务<br>(安时积分 + OCV 静态曲线校准)"]
        engine_weather["GPS 坐标反查实时气象引擎<br>(Open-Meteo REST / 12h双时段)"]
        engine_track["GPX 轨迹双流记录器<br>(WGS84+GCJ02/连续切片/自愈)"]
        engine_dr["隧道盲区 IMU 航位推测与单维轴线锁死"]
        engine_net["Wi-Fi 6 管理器 & SoftAP 导出<br>(Multi-SSID / 独立热点分享)"]
    end

    subgraph Presentation ["UI 展示与触控路由 (Presentation Layer)"]
        dock["左侧 160px 常驻 Dock<br>(主页/时钟/GPS/WiFi/电池/天气/工具)"]
        subgraph MainPages ["右侧 1120x720 全高视口 (左右滑动路由)"]
            p1["Page 1: 驾驶主仪表盘 (360px大罗盘/车速热区/长按2s清零)"]
            p2["Page 2: 海拔高度与1050px宽连续起伏折线图"]
            p3["Page 3: 车速变化与1050px宽连续折线图 (MAX/AVG)"]
            p4["Page 4: 矢量自驾轨迹 (全画幅自适应铺满+悬浮HUD)"]
            p5["Page 5: 车辆动态 G 值靶图 (400px) 与越野姿态水平仪"]
            p6["Page 6: 12小时大字双时段天气预报 (前后12h一键切)"]
            p7["Page 7: 多时区世界时钟 (2x2宽幅530x320大卡片)"]
            p8["Page 8: 2x2动态扩展工具箱 (TF管理/WiFi/GPS/设置)"]
        end
        dock -.->|点击🏠| p1
        dock -.->|点击时钟| p7
        dock -.->|点击天气| p6
        dock -.->|点击工具| p8
    end

    N9N --> drv_gnss --> engine_gnss
    BARO --> drv_i2c --> engine_baro
    MAG --> drv_i2c --> engine_dr
    IMU --> drv_i2c --> engine_dr
    RTC --> drv_rtc --> engine_time
    INA --> drv_ina --> engine_pwr
    SD --> drv_fs --> engine_track
    C6 --> engine_net
    engine_gnss --> MainPages
    engine_baro --> MainPages
    engine_time --> dock
    engine_pwr --> dock
    engine_weather --> MainPages
    engine_dr --> p4
