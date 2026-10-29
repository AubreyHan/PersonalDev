# 外壳底盖 (Enclosure Bottom Cover) 拓竹 X1C 3D 打印项目工程

专为图纸设计的外壳底盖模型，现已针对 **Bambu Lab X1 Carbon (X1C)** + **Bambu PETG-HF** 耗材 + **纹理 PEI 弹簧钢板 (Textured PEI Plate)** 完成全套最佳切片调优，并打包为免调参即打的 `.3mf` 工程文件。

---

## 1. 核心规格与几何尺寸

* **外形尺寸**：$54.0\text{ mm} \times 54.0\text{ mm}$
* **底盖厚度**：$2.0\text{ mm}$
* **四角圆角**：$R = 5.0\text{ mm}$（与螺丝孔同轴同心）
* **固定孔位**：$44.0\text{ mm} \times 44.0\text{ mm}$ 矩形阵列，中心在 `(±22.0, ±22.0)`
* **螺丝规格**：M3 紧固螺丝
  * 通孔直径：$\varnothing 3.4\text{ mm}$（预留 0.4mm 3D打印孔径收缩公差）
  * 沉头规格（Countersunk 款）：DIN 7991 M3 标准，大径 $\varnothing 6.4\text{ mm}$，90° 沉头锥角，深度 $1.5\text{ mm}$

---

## 2. 交付工程文件列表

| 文件名 | 类型 | 说明 |
| :--- | :--- | :--- |
| [`Enclosure_Bottom_Cover_Countersunk_X1C_PETG-HF.3mf`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Enclosure_Bottom_Cover_Countersunk_X1C_PETG-HF.3mf) | **拓竹 X1C 3MF 项目包** | **主力款（最推荐）**：包含 M3 平头沉头孔款，已预设 X1C + PETG-HF + 纹理 PEI 板全部最佳参数，且内嵌切片 Gcode，双击导入 Bambu Studio **直接点“打印”即可**。 |
| [`Enclosure_Bottom_Cover_Flat_X1C_PETG-HF.3mf`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Enclosure_Bottom_Cover_Flat_X1C_PETG-HF.3mf) | **拓竹 X1C 3MF 项目包** | **直通孔款**：预设完全相同的高性能工艺，适用于使用盘头、外六角或带垫片 M3 螺丝的场景。 |
| [`Enclosure_Bottom_Cover_Countersunk.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Enclosure_Bottom_Cover_Countersunk.stl) | 二进制 3D STL 模型 | 100% 封闭流形沉头孔模型。 |
| [`Enclosure_Bottom_Cover_Flat.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Enclosure_Bottom_Cover_Flat.stl) | 二进制 3D STL 模型 | 100% 封闭流形直通孔模型。 |
| [`Enclosure_Bottom_Cover.scad`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Enclosure_Bottom_Cover.scad) | OpenSCAD 参数化源码 | 完全参数化建模源码。 |
| [`create_bambu_3mf.py`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/create_bambu_3mf.py) | Python 3MF 自动化生成器 | 自动加载 X1C 官方配置、注入工艺并调用官方 CLI 切片。 |

---

## 3. PETG-HF + 拓竹 X1C + 纹理 PEI 板最佳工艺设置清单

本项目 `.3mf` 文件内部已固化以下经过工程验证的参数，无需在切片软件中反复调整：

| 工艺参数分类 | 设定数值 | 调优原理与实战经验 |
| :--- | :--- | :--- |
| **打印机型号 (Printer)** | `Bambu Lab X1 Carbon 0.4 nozzle` | 官方标准 0.4mm 硬化钢喷嘴配置。 |
| **耗材类型 (Filament)** | `Bambu PETG HF @BBL X1C` | 官方 PETG-HF 专属高流量曲线。 |
| **打印热床 (Plate Type)** | `Textured PEI Plate` (纹理 PEI 弹簧钢板) | 专选金黄/黑金双面纹理板。 |
| **热床温度 (Bed Temp)** | **首层 70℃ / 后续层 70℃** | 70℃ 为 PETG-HF 在纹理板上的最佳玻璃化附着温区，不脱落，冷却后自动弹出。 |
| **喷嘴温度 (Nozzle Temp)** | **首层 245℃ / 后续层 245℃** | 保证高流动性与高熔融结合强度。 |
| **壁层数 (Wall Loops)** | **4 圈 (4 Walls)** | 4 圈壁厚（单边约 1.6mm），螺丝孔周围全为实心致密壁，锁死螺丝不爆裂。 |
| **顶层 / 底层数** | **顶层 5 层 / 底层 5 层** | 0.20mm 层高下，5顶 + 5底刚好为 10 层（$10 \times 0.2 = 2.0\text{ mm}$），零件呈现 **100% 全实心注塑级质感**。 |
| **辅助侧边风扇 (Aux Fan)** | **0% (完全关闭)** | **关键绝招**：X1C 侧风扇单侧吹风会导致大面积平底 PETG 件单边剧烈收缩起翘，必须关闭！ |
| **部件冷却风扇 (Part Fan)** | **前 3 层关闭 (0%)，后续 10%~40%** | 前 3 层无风保温让 PETG 彻底浸润 PEI 纹理，后段微风冷却保持孔洞清晰。 |
| **首层打印速度** | **40 mm/s** (内填充 50 mm/s) | 适度慢速让高温熔融料充分咬进 PEI 纹理孔隙中，首层无拉丝、无孔隙。 |
| **裙边 (Brim)** | **无 (No Brim)** | 54×54mm 底面在 70℃ 纹理板上附着力极佳，无需 Brim，撕下即用，无生硬边缘毛刺。 |
| **排版位置** | **热床绝对中心 (128.0, 128.0)** | 避开热床四周温差边缘，处于腔体均热核心区。 |
| **打印耗时** | **约 22 分钟** | 兼顾极致强度与表面质感。 |
