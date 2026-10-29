# 外壳底盖 (Enclosure Bottom Cover) 3D 打印工程模型

本底盖模型专为所给工程图纸设计，严格按照机械公差与 3D 打印收缩补偿进行建模。

---

## 1. 图纸尺寸解析与几何推导

根据您提供的工程图纸与要求：
1. **螺丝孔中心距**：
   - 顶部与左侧标注尺寸为 **44 mm × 44 mm**（正方形矩阵分布）。
   - 孔位中心坐标为 `(±22.0 mm, ±22.0 mm)`。
2. **四角圆角 R5 与外边长关系**：
   - 四个角圆角半径为 **R5**（$R = 5.0\text{ mm}$）。
   - 螺丝孔中心与四角 R5 倒角圆心为**同心设计**（孔心到外边缘的距离在 X 和 Y 方向均为 $5.0\text{ mm}$）。
   - 外壳总宽度与总高度：
     $$\text{外形宽度} = 44.0\text{ mm (孔距)} + 2 \times 5.0\text{ mm (R5外边距)} = \mathbf{54.0\text{ mm}}$$
     $$\text{外形高度} = 44.0\text{ mm (孔距)} + 2 \times 5.0\text{ mm (R5外边距)} = \mathbf{54.0\text{ mm}}$$
3. **底盖厚度**：
   - 底盖总厚度为标准的 **2.0 mm**。
4. **M3 螺丝孔位规格**：
   - **通孔直径**：$\varnothing 3.4\text{ mm}$（ISO 273 螺纹紧固件中等装配间隙，确保 3D 打印孔径收缩后 M3 螺丝顺畅旋入不卡滞）。
   - **平头沉头孔 (Countersunk)**：针对标准 **DIN 7991 / ISO 10642 M3 平头沉头螺钉**，大口直径 $\varnothing 6.4\text{ mm}$，90° 沉头锥角，下沉深度 $1.5\text{ mm}$。螺丝锁紧后螺钉头部与底面完全平齐，设备平放桌面上不会刮花桌面或发生摇晃。
5. **图纸中 36mm 标注说明**：
   - 图纸顶部标注的 36mm 为内部 PCB 板固定螺柱横向孔距（在 OpenSCAD 源码中已提供 `has_pcb_holes` 参数化开关，默认底盖密封保护内部主板）。

---

## 2. 交付文件清单

| 文件名 | 类型 | 说明 |
| :--- | :--- | :--- |
| [`Enclosure_Bottom_Cover_Countersunk.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Enclosure_Bottom_Cover_Countersunk.stl) | 二进制 3D 打印模型 | **推荐主力款**：54×54×2mm，四角 R5，4 个 M3 90° 沉头孔（锁紧后螺丝头部不凸出底面，外观极佳）。 |
| [`Enclosure_Bottom_Cover_Flat.stl`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Enclosure_Bottom_Cover_Flat.stl) | 二进制 3D 打印模型 | **直通孔款**：54×54×2mm，四角 R5，4 个 $\varnothing 3.4\text{ mm}$ 普通圆柱直通孔（适合外六角、盘头或圆头螺钉加垫片）。 |
| [`Enclosure_Bottom_Cover.scad`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Enclosure_Bottom_Cover.scad) | OpenSCAD 源代码 | 完全参数化源码，可自由修改外径、壁厚、圆角、沉头形式、内嵌防滑移定位台阶等。 |
| [`build_bottom_cover.py`](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/build_bottom_cover.py) | Python 建模与流形检查脚本 | 独立 STL 算法生成器，内置严格的拓扑流形检查器（Manifold Validator）。 |

> [!NOTE]
> 导出的两个 STL 模型均已通过拓扑流形闭合检查（`unmatched directed edges = 0`，100% 封闭无缝水密流形体），可直接无缝导入 **Bambu Studio / 拓竹切片软件**、PrusaSlicer 或 Cura 进行切片。

---

## 3. 3D 打印与切片参数建议 (Bambu Studio / 拓竹)

* **摆放方向**：
  * **沉头款 (Countersunk)**：沉头大圆开口的一面**朝下贴热床**，或者沉头大圆开口朝上均可。推荐**平整底面贴床**打印，若沉头口朝下可获得如镜面般光滑的平头配合效果。
* **耗材选择**：
  * **日常外壳**：PLA Basic / PLA Matte（平整度好、收缩极低、尺寸精度最高）。
  * **车载/高温/户外环境**：PETG 或 ABS/ASA（耐温性更好，防止暴晒软化）。
* **层高 (Layer Height)**：`0.20mm Standard`（若追求沉头锥角极度顺滑，可开启自适应层高或设为 `0.16mm High Quality`）。
* **墙层数 (Wall Loops)**：`4` 圈（确保螺丝孔周围全为实心注塑壁，锁紧螺丝时不发生受压凹陷变形）。
* **顶/底层数**：顶部 `5` 层，底部 `5` 层。由于总厚度仅 2.0mm，此设置下零件基本为 **100% 实心**，结构极其刚劲耐压。
* **支撑**：**关闭支撑 (No Support Required)**，所有特征均为平整面或标准角度沉孔，完全不需要任何支撑材料。
