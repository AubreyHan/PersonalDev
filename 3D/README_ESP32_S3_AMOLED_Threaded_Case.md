# 微雪 ESP32-S3-Touch-AMOLED-1.75-G 螺纹旋合版外壳 (带 12 点钟天线仓与内部穿线孔)

本工程专为 **微雪 (Waveshare) ESP32-S3-Touch-AMOLED-1.75-G**（带 LC76G 卫星定位模块）设计，严格满足以下定制要求：
1. **两部件采用精密机械螺纹连接 (Threaded Connection)**：告别脆弱易断的塑料卡扣，采用 $M52.0 \times 2.0$ 专为 3D 打印优化的大螺距抗滑牙螺纹，徒手旋紧即实现刚性锁死。
2. **12 点钟方向设有独立一体化天线仓**：内腔 $25.6\text{ mm} \times 24.4\text{ mm} \times 7.8\text{ mm}$，完美容纳标准 $25\times 25\text{ mm}$ GNSS 陶瓷天线。
3. **顶部隔墙设有专用穿线小孔**：$3.6\text{ mm} \times 3.0\text{ mm}$ 穿线通道，直通主板 PCB 仓，IPEX / U.FL 射频微同轴电缆可轻松穿入插接。

---

## 1. 结构设计与硬件配合参数

| 结构特征 | 尺寸与几何参数 | 工程原理与使用说明 |
| :--- | :--- | :--- |
| **主机外壳外径** | $\varnothing 55.00\text{ mm}$ | 黄金手感尺寸，与原盘比例完美匹配 |
| **螺纹连接副** | **$M52.0 \times 2.0$ 梯形/三角形 45° 螺纹** | **Body 设内螺纹，Lid 设外螺纹**。螺纹小径 $\varnothing 49.8\text{ mm} >$ 主板外径 $\varnothing 49.0\text{ mm}$，主板可顺畅从底部放入。单边预留 $0.25\text{ mm}$ 打印配合公差，丝滑顺畅不卡死 |
| **后盖操作防滑特征** | **36 齿精密边缘滚花 + 硬币凹槽** | 后盖外缘设计有 36 道垂直防滑条纹，手指即可轻松旋入旋紧；背部设 $24\text{ mm} \times 2.4\text{ mm}$ 一字硬币槽，也可用硬币或钥匙快速开闭 |
| **12 点钟天线仓** | 内腔 $25.6\text{ mm} \times 24.4\text{ mm} \times 7.8\text{ mm}$ | 外置向上净空，彻底避开金属电池与内部电路对卫星信号的屏蔽与反射 |
| **天线穿线小孔** | $3.6\text{ mm} \times 3.0\text{ mm}$ 贯通孔 | 位于天线仓下沿隔墙正中，直通主板正面天线底座，同轴线无折角平顺穿入 |
| **9 点钟方向接口** | $11.5\text{ mm} \times 5.8\text{ mm}$ Type-C 接口 | 位于左侧中轴线，轻松插拔各类加粗 Type-C 数据线与充电线 |
| **3 点钟方向窗口** | **$-45^\circ \sim +45^\circ$ 一体化大开窗** | 跨度 $90^\circ$ 全景开窗，直接露出板载 PWR 开关、TF 卡插槽与 BOOT(Reset) 按键，指尖直接按压操作，TF 卡免拆壳插拔 |
| **正面屏幕保护视窗** | 开口 $\varnothing 44.20\text{ mm}$，台阶 $\varnothing 49.60\text{ mm}$ | 完整露出 $43.76\text{ mm}$ AMOLED 显示区，内圈阶梯将屏幕黑色盖板牢牢压实 |

---

## 2. 交付工程文件列表

| 文件名 | 格式 | 说明 |
| :--- | :--- | :--- |
| [`ESP32_S3_AMOLED_Threaded_Case_X1C_PETG-HF.3mf`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_Threaded_Case_X1C_PETG-HF.3mf) | **拓竹 X1C 3MF 项目包** | **主力打印文件（推荐）**：单盘同时排版 Body 与 Lid，已固化 PETG-HF/纹理 PEI 预设，直接在 Bambu Studio 中打开即可打印。 |
| [`ESP32_S3_AMOLED_Body_Threaded.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_Body_Threaded.stl) | 二进制 STL | 100% 水密流形主体舱（含内螺纹、天线仓、穿线孔、Type-C 口与侧边大开窗）。 |
| [`ESP32_S3_AMOLED_Lid_Threaded.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_Lid_Threaded.stl) | 二进制 STL | 100% 水密流形螺纹后盖（含外螺纹公头、防滑滚花外圈与一字硬币旋槽）。 |
| [`ESP32_S3_AMOLED_Assembly_Threaded.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/ESP32_S3_AMOLED_Assembly_Threaded.stl) | 二进制 STL | 整体螺纹旋合态装配三维模型。 |
| [`build_gps_threaded_case.py`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/build_gps_threaded_case.py) | Python 源码 | 100% 水密流形几何建模与参数化生成脚本。 |
| [`generate_threaded_case_3mf.py`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/generate_threaded_case_3mf.py) | Python 源码 | 拓竹切片参数注入与 3MF 打包脚本。 |

---

## 3. 推荐 3D 打印工艺与装配流程

1. **打印摆放姿态**：
   * **Body (主体舱)**：建议正面（屏幕开口端面）朝下贴紧热床打印，正面获得纹理 PEI 的平整磨砂哑光质感，螺纹与内腔无悬垂向上成型，**无需任何支撑 (No Supports Required)**！
   * **Lid (螺纹后盖)**：底面（平整面）朝下贴紧热床打印，外螺纹向上成型，牙型最饱满清晰。
2. **装配与接线次序**：
   1. 将 GNSS 陶瓷天线贴入 12 点钟天线仓，将微同轴接线头（IPEX 端子）穿过隔墙中部的 **$3.6 \times 3.0\text{ mm}$ 小孔** 引入主机舱内；
   2. 将 AMOLED 屏幕与 PCB 板从主体底部的内螺纹大口推入主舱（螺纹小径为 $\varnothing 49.8\text{ mm}$，大于主板 $\varnothing 49.0\text{ mm}$，毫无阻碍一次落位）；
   3. 将天线 IPEX 端子扣接到板载插座上；
   4. 拿取 Lid（后盖），对准底部螺纹顺时针旋转约 1.5 ~ 2 圈，外缘滚花可用指尖或一枚硬币顺畅旋到底锁死！
