# 微雪 ESP32-S3-Touch-AMOLED-1.75-G (GPS版) MagSafe 磁吸轻薄卡扣外壳

专为 **微雪 (Waveshare) ESP32-S3-Touch-AMOLED-1.75-G**（LC76G GNSS 卫星定位版，SKU: 31264）量身打造的高精度 3D 打印外壳工程。已针对 **Bambu Lab X1 Carbon (X1C)**、**Bambu PETG-HF / PLA** 耗材与 **纹理 PEI 弹簧钢板** 完成全套切片调优，提供免调参一键打印的 `.3mf` 工程文件。

---

## 1. 核心设计亮点与工程考量

### ① 12 点钟一体化外置天线隔离仓 (Integrated RF Antenna Pod)
* **抗干扰隔离**：由于 MagSafe 磁吸配件内部包含强磁场钕铁硼环与金属引磁导磁片，若将 GNSS 天线置于底部或主板附近，会造成严重的射频屏蔽与涡流损耗。
* **一体化无缝延伸**：外壳在 12 点钟方向（正上方）设计了流线型一体化天线保护仓，将随板标配的 **GNSS 陶瓷贴片天线** 独立容纳在外壳顶端。
* **高透射 RF 保护顶罩**：天线正上方外壳壁厚精调为 $1.2\text{ mm}$（PETG/PLA 介电常数 $\varepsilon_r \approx 2.8$），在防刮耐磨的同时保证 100% 卫星信号净空穿透，无论车载立式吸附还是骑行手持，天线始终朝向天空天顶（Zenith），实现全星座快速搜星。
* **内藏走线管道**：主仓与天线仓之间贯通有专门的 IPEX 微同轴线束过线隧道，线缆走线完全内嵌隐藏。

### ② 底部全平整 MagSafe 磁吸嵌入槽 (Flush MagSafe Ring Recess)
* **标准规格沉槽**：底壳外径与 MagSafe 磁吸圈形成比例协调，底部精雕有 **外径 $54.2\text{ mm}$、内径 $45.8\text{ mm}$、深度 $1.2\text{ mm}$** 的环形沉槽。
* **完全平齐贴合**：市售标准 MagSafe 引磁片/磁吸铁圈粘贴后，与外壳底面**绝对平齐**，吸附在车载磁吸支架、充电底座或桌面支架上时绝不刮伤底座，吸附平稳牢固。
* **中心全封闭保护**：磁吸环内部底板完整封闭实心，保护开发板背面芯片与焊点。

### ③ 极致轻薄纯 Type-C 供电构型 (Ultra-Slim Profile)
* **贴合背板**：由于采用外部 Type-C 供电，无需额外预留厚重电池仓空间，底壳内壁紧贴 PCB 背面元器件，整机厚度压缩至 **仅 $10.8\text{ mm}$**。
* **6 点钟方向 Type-C 避空大开口**：采用 $11.2\text{ mm} \times 4.8\text{ mm}$ 扩口设计，兼具 $45^\circ$ 拔插导角，兼容市面上绝大多数粗胶壳数据线与车载弯头线。

### ④ 免螺丝卡扣式拼合系统 (Snap-Fit Quick Assembly)
* **无五金件装配**：采用 4 处高韧性环形分布卡扣（$35^\circ$、$145^\circ$、$215^\circ$、$325^\circ$），配备 $45^\circ$ 入位斜导角与 $75^\circ$ 锁止自锁肩，按压手感清脆、扣合稳固。
* **隐形拆卸撬槽 (Pry Notches)**：在 3 点钟与 9 点钟分型面两侧各预留一处 $3.0\text{ mm} \times 1.0\text{ mm}$ 隐形微槽，需要维护或升级时，用吉他拨片或平口工具即可无损开合。

### ⑤ 完整传感器与接口避空
* **正面 AMOLED 视窗**：$\varnothing 45.4\text{ mm}$ 正圆开孔，露出完整的 1.75 英寸圆形显示屏，同时周边 $1.8\text{ mm}$ 压边框紧贴屏幕保护玻璃，实现平齐视网膜边框质感。
* **侧面 MicroSD (TF) 卡槽**：9 点钟方向预留插拔槽，无需拆壳即可取放存储卡。
* **双麦克风拾音开孔**：针对 ES7210 芯片与双硅麦阵列设有导音通孔，保障小智 AI / 语音助手拾音灵敏度。

---

## 2. 关键几何参数与装配公差

| 参数项目 | 设计尺寸 (mm) | 设计说明 |
| :--- | :--- | :--- |
| **主体圆盘外径** | $\varnothing 55.0\text{ mm}$ | 与 MagSafe 磁吸圈（54mm）形成自然视觉衔接 |
| **整机闭合厚度** | $10.8\text{ mm}$ | 超薄便携、车载不占空间 |
| **内部 PCB 仓内径** | $\varnothing 49.6\text{ mm}$ | 单边预留 $0.3\text{ mm}$ 3D 打印装配公差 |
| **屏幕正面视窗直径** | $\varnothing 45.4\text{ mm}$ | 完整露出圆形显示区，保护边缘玻璃 |
| **MagSafe 沉槽尺寸** | 外径 $54.2$ / 内径 $45.8$ / 深 $1.2\text{ mm}$ | 适配通用磁吸引磁片/磁吸圈，贴合后全平整 |
| **外置天线仓容积** | $18.6\text{ mm} \times 18.6\text{ mm} \times 4.6\text{ mm}$ | 完美容纳标配 $15\times 15$ 或 $18\times 18$ 陶瓷天线 |
| **Type-C 开口尺寸** | $11.2\text{ mm} \times 4.8\text{ mm}$ | 带倒角扩展，兼容车载粗外壳线缆 |
| **卡扣锁紧行程** | 伸出 $0.45\text{ mm}$ / 高度 $2.1\text{ mm}$ | PETG/PLA 弹性极限安全区内，抗疲劳断裂 |

---

## 3. 交付工程文件列表

| 文件名 | 类型 | 说明 |
| :--- | :--- | :--- |
| [`ESP32_S3_AMOLED_GPS_Case_X1C_PETG-HF.3mf`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_GPS_Case_X1C_PETG-HF.3mf) | **拓竹 X1C 3MF 项目包** | **主力打印文件（最推荐）**：单盘同时排版上壳与底壳，上壳预设翻转 $180^\circ$ 贴合 PEI 纹理板，固化全套 PETG-HF 工艺，双击 Bambu Studio 一键打印。 |
| [`ESP32_S3_AMOLED_GPS_Case_Bottom.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_GPS_Case_Bottom.stl) | 二进制 STL 模型 | 100% 封闭流形底壳模型（含 MagSafe 槽、走线槽、接口开孔）。 |
| [`ESP32_S3_AMOLED_GPS_Case_Top.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_GPS_Case_Top.stl) | 二进制 STL 模型 | 100% 封闭流形上盖模型（含屏幕保护圈、天线护罩、卡扣公扣）。 |
| [`ESP32_S3_AMOLED_GPS_Case_Assembly.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_GPS_Case_Assembly.stl) | 二进制 STL 模型 | 扣合装配态三维预览模型。 |
| [`ESP32_S3_AMOLED_GPS_MagSafe_Case.scad`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_GPS_MagSafe_Case.scad) | OpenSCAD 参数化源码 | 完整参数化建模代码，方便自定义修改外径或天线尺寸。 |
| [`build_gps_magsafe_case.py`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/build_gps_magsafe_case.py) | Python 自动化生成器 | 纯 Python 标准库生成的 100% 水密多面体几何模型生成脚本。 |
| [`generate_gps_case_3mf.py`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/generate_gps_case_3mf.py) | 3MF 打包调优脚本 | 调用拓竹 CLI，自动排版并注入工艺参数。 |

---

## 4. 推荐打印参数 (Bambu Lab X1C / P1S)

工程文件 `.3mf` 内部已预先配置好以下最佳工艺参数：

* **耗材选择**：首选 **Bambu PETG-HF** 或 **Bambu PLA Basic / PLA-CF**（PETG 具备更好的抗紫外线耐温与卡扣韧性）。
* **打印热床**：**Textured PEI Plate**（纹理 PEI 弹簧钢板）。
* **热床温度**：首层 **70℃** / 后续层 **70℃**（PETG-HF）。
* **喷嘴温度**：**245℃**。
* **外壁圈数 (Wall Loops)**：**4 圈**（实心壁厚约 $1.6\text{ mm}$，确保外壳圆弧紧致细腻且卡扣刚度饱满）。
* **顶层/底层数**：**顶层 5 层 / 底层 5 层**（密实不透光）。
* **填充方案**：**25% Gyroid (螺旋线填充)**。
* **辅助风扇 (Aux Fan)**：**0% (关闭)**（圆盘薄壁件严禁单侧吹强风，以避免单边收缩应力起翘）。
* **摆盘朝向策略**：
  * **底壳 (Bottom)**：正放（底面贴床），底部 MagSafe 凹槽由纹理板赋予磨砂哑光质感。
  * **上壳 (Top)**：**翻转 180°**（正面视窗朝下贴床），使得手表正面表圈呈现高级纹理质感，且卡扣柱沿 Z 轴垂直生长，具备极高抗折弯疲劳强度，全程无需任何支撑。

---

## 5. 装配指南

1. **粘贴 MagSafe 引磁片**：将标准 MagSafe 引磁贴片撕开背胶，对准底壳背部的环形凹槽平整压入，引磁片表面将与外壳底面完美平齐。
2. **连接 GPS 天线**：将随板提供的 GNSS 陶瓷贴片天线接好 IPEX 1 代微同轴接头，线缆顺入走线隧道中，天线片正面朝上平卧推入 12 点钟外置天线仓。
3. **主板入槽**：将开发板对准 6 点钟的 Type-C 接口方向斜向滑入底壳，确认背面无干涉并完全落到底部台阶。
4. **扣合上壳**：将上壳对齐四周的卡扣位置与天线罩，轻柔向下压合，听到清脆的 "咔哒" 声即完成装配锁定。
5. **拆卸说明**：如需开壳维护，在 3 点钟或 9 点钟分型面两侧的微型撬槽处，使用非金属撬棒或拨片轻轻向外挑起即可打开。
