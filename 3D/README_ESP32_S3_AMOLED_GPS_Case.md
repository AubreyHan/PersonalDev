# 微雪 ESP32-S3-Touch-AMOLED-1.75-G (官方真实硬件布局版) MagSafe 磁吸轻薄卡扣外壳

本工程专为 **微雪 (Waveshare) ESP32-S3-Touch-AMOLED-1.75-G**（LC76G GNSS 卫星定位版，SKU: 31264）设计。**严格遵照微雪官方工程尺寸图及实机生产元器件方位** 构建，已完成 **Bambu Lab X1 Carbon (X1C)**、**Bambu PETG-HF / PLA** 耗材及 **纹理 PEI 弹簧钢板** 的全套切片调优与 Gcode 内嵌，提供双击即打的免调参 `.3mf` 工程文件。

---

## 1. 真实物理接口与结构设计布局

以屏幕正面朝上、12 点钟方向为正上方基准：

| 物理方位 | 对应接口 / 硬件组件 | 工程结构设计说明 |
| :--- | :--- | :--- |
| **9 点钟方向 (West / 左侧)** | **USB Type-C 供电/数据接口** | $11.2\text{ mm} \times 5.6\text{ mm}$ 大倒角插口，位于左侧水平中轴线，轻松拔插各类粗外壳线缆 |
| **8 点与 10 点钟 (左下 / 左上)** | **ES7210 双麦克风孔 (MIC1 / MIC2)** | $\varnothing 1.5\text{ mm}$ 传声通孔，对称分布在左侧 Type-C 接口上下两侧，保障语音交互灵敏度 |
| **3 点钟方向 (East / 右侧: 320°~40°)** | **一体化全景大镂空窗 (PWR / TF卡槽 / BOOT键)** | 跨度约 $80^\circ$ 的侧边一体贯通开放窗口。无需额外按键帽，指尖可直接触按 PWR、BOOT 微动按键，TF 存储卡亦可轻松插拔 |
| **12 点钟方向 (North / 正上方)** | **$25 \times 25\text{ mm}$ GNSS 天线隔离仓** | 外宽 $29.2\text{ mm}$，内腔 $25.6 \times 25.6 \times 7.6\text{ mm}$，彻底隔离底部 MagSafe 磁吸干扰，天线朝天净空搜星 |
| **6 点钟方向 (South / 正下方)** | **8-Pin 2.54mm 排母保护区** | 封闭式内嵌避空保护，避免排针外露刮擦同时保持外壳整体强度 |
| **底壳背面外表面** | **MagSafe 磁吸平齐嵌入槽** | 外径 $54.2\text{ mm}$、内径 $45.8\text{ mm}$、深 $1.2\text{ mm}$，引磁片贴合后与底壳绝对平齐 |
| **分型面 (50° / 130° / 230° / 310°)** | **4 组免螺丝 Snap-fit 弹性卡扣** | 避开所有接口与按键区域，清脆咬合锁定，并配有隐形拆卸微撬槽 |

---

## 2. 关键几何尺寸

* **主机圆盘外径**：$\varnothing 55.0\text{ mm}$（与 54mm MagSafe 磁吸圈比例完美衔接）
* **整机闭合厚度**：仅 **$10.8\text{ mm}$** 超薄机身（无冗余大电池仓，轻巧贴合）
* **内部 PCB 仓直径**：$\varnothing 49.60\text{ mm}$（单边 $0.32\text{ mm}$ 装配公差）
* **正面屏幕视窗开口**：$\varnothing 44.20\text{ mm}$（露出完整 $43.76\text{ mm}$ AMOLED 显示区，边缘黑色玻璃严密压固）
* **天线仓净空**：$25.6\text{ mm} \times 25.6\text{ mm} \times 7.6\text{ mm}$（适配标准 $25\times 25\text{ mm}$ 陶瓷天线及带屏蔽罩有源天线）

---

## 3. 交付工程文件列表

| 文件名 | 格式 | 说明 |
| :--- | :--- | :--- |
| [`ESP32_S3_AMOLED_GPS_Case_X1C_PETG-HF.3mf`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_GPS_Case_X1C_PETG-HF.3mf) | **拓竹 X1C 3MF 项目包** | **主力打印文件（最推荐）**：单盘同时排版上壳与底壳，已固化 PETG-HF/纹理 PEI 板参数并**内嵌完整切片 Gcode**（切片预估耗时 59 分钟），双击直接打印。 |
| [`ESP32_S3_AMOLED_GPS_Case_Bottom.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_GPS_Case_Bottom.stl) | 二进制 STL 模型 | 100% 封闭流形底壳（含 3点钟 Type-C、9点钟 TF、10/8点钟按键、12点钟天线座及 MagSafe 沉槽）。 |
| [`ESP32_S3_AMOLED_GPS_Case_Top.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_GPS_Case_Top.stl) | 二进制 STL 模型 | 100% 封闭流形上壳（含屏幕保护圈、天线护罩、2/4点钟双麦克风孔、卡扣公扣）。 |
| [`ESP32_S3_AMOLED_GPS_Case_Assembly.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_GPS_Case_Assembly.stl) | 二进制 STL 模型 | 完整扣合装配态模型。 |
| [`ESP32_S3_AMOLED_GPS_MagSafe_Case.scad`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_GPS_MagSafe_Case.scad) | OpenSCAD 源码 | 参数化建模源代码。 |
| [`build_gps_magsafe_case.py`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/build_gps_magsafe_case.py) | Python 自动化脚本 | 100% 水密多面体结构 STL 生成脚本。 |
| [`generate_gps_case_3mf.py`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/generate_gps_case_3mf.py) | Python 3MF 调优脚本 | 调用拓竹 CLI 实现排版与参数注入。 |

---

## 4. 拓竹 X1C / P1S 打印工艺参数清单

* **耗材类型**：**Bambu PETG-HF** 或 **Bambu PLA Basic**
* **打印热床**：**Textured PEI Plate (纹理 PEI 弹簧钢板)**
* **热床温度**：首层 **70℃** / 后续层 **70℃**
* **喷嘴温度**：**245℃**
* **外壁圈数 (Wall Loops)**：**4 圈**（实心壁厚，强度高）
* **顶层/底层数**：**5 顶 / 5 底**
* **填充方案**：**25% Gyroid (螺旋线填充)**
* **辅助风扇 (Aux Fan)**：**0% (完全关闭)**（防止圆盘单边起翘）
* **打印总耗时**：**约 59 分钟**
