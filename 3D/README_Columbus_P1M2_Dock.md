# Columbus P-1 Mark II (P-1 M2) 固定底座模型说明

专门针对 **Columbus 探险家 P-1 Mark II (P-1 M2)** 专业型 GNSS 轨迹记录仪设计的**正面敞开式固定底座（Cradle / Dock）**。

---

## 一、 模型主要特点与设计考量

1. **绝对平整底面（粘贴安装面）**：
   - 底座背面为完整的平整矩形平面（约 `60.4 × 90.4 mm`），接触面积高达 `~54 cm²`。
   - 完美适配 3M VHB（如 5952 / 4910）强力泡棉双面胶，可极其稳固地贴在汽车中控台、垂直面板或设备箱表面。
2. **纯平直壁阻尼配合（选项 B）**：
   - 内壁直上直下（无凸出倒扣卡唇），放入手感平顺丝滑。
   - 预留 `0.3 mm` 单边配合公差，内部可根据需要加贴 `0.5 mm` 薄防滑绒布或泡棉实现阻尼紧固。
3. **精准避让缺口与开孔**：
   - **顶面正中缺口（宽 22 mm）**：直达顶部电源键，拇指/食指轻松长按开关机。
   - **右侧上部独立窗口（长 17 mm，距顶 20 ~ 37 mm）**：精准露出功能键 (FN)，侧边手指按压无干涉，同时保留右上角（20mm）和右下段（48mm）的完整实心侧壁支撑。
   - **底面正中缺口（宽 38 mm）**：完整覆盖 MicroUSB 充电口、TF 卡槽与橡胶防水盖。支持车载常插 MicroUSB 线供电记录。
   - **左侧取机指扣槽（宽 20 mm）**：左壁中部下凹至 9mm 高度，与右侧按键缺口呼应，双手捏住两侧即可轻松将设备拔出。
4. **GNSS 天线 100% 无遮挡**：
   - 正面完全敞开，内置陶瓷天线全视野朝天，4 颗 LED 指示灯及正面锚点键完全外露，视认与操作毫无阻碍。

---

## 二、 关键尺寸数据对照

| 维度 | 设备标称尺寸 | 底座内腔尺寸 (配合位) | 底座外形总尺寸 |
| :--- | :--- | :--- | :--- |
| **宽度 (X)** | 55.0 mm | **55.6 mm** (预留 +0.6mm) | **60.4 mm** (壁厚 2.4mm) |
| **高度 (Y)** | 85.0 mm | **85.6 mm** (预留 +0.6mm) | **90.4 mm** (壁厚 2.4mm) |
| **厚度/深度 (Z)** | 17.7 mm | **18.0 mm** (深度 +0.3mm) | **20.4 mm** (底厚 2.4mm) |
| **四角圆角** | 约 R4.0 ~ R4.5 mm | **R4.5 mm** | **R6.9 mm** (同心圆角) |

---

## 三、 文件列表

* [Columbus_P1M2_Dock.stl](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Columbus_P1M2_Dock.stl)：经 BambuStudio 严格校验的二进制 STL 模型（Watertight 2-Manifold，0 开边）。
* [Columbus_P1M2_Dock_X1C_PETG-HF.3mf](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Columbus_P1M2_Dock_X1C_PETG-HF.3mf)：已切片的拓竹 Bambu Studio 工程文件（配置针对 X1C + Bambu PETG-HF + 纹理 PEI 板优化）。
* [Columbus_P1M2_Dock.scad](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Columbus_P1M2_Dock.scad)：完全参数化的 OpenSCAD 源代码，可自由调节间隙与壁厚。
* [build_p1m2_dock.py](file:///Users/aubreyhan/Tools/git_repo/PersonalDev/3D/build_p1m2_dock.py)：独立 Python 几何生成脚本。

---

## 四、 3D 打印建议参数

* **推荐材料**：**PETG** 或 **ABS / ASA**（车载中控台夏季日晒温度极高，PLA 容易发生热软化变形，推荐使用耐温材料）。
* **喷嘴孔径**：0.4 mm
* **层高**：0.20 mm Standard
* **外墙圈数 (Wall Loops)**：4 ~ 6 圈（2.4mm 壁厚可实现近乎全实心侧壁）
* **顶/底层层数**：5 层
* **填充率与花纹**：20% ~ 25% 陀螺线 (Gyroid)
* **支撑**：**无需任何支撑**（所有壁面垂直直上，底面绝对平整，正面敞开，一次成型极度干净）。
