"""机身与 MagSafe 后盖的螺纹共用参数与几何函数。

坐标系约定
  * 机身 (body)  : 屏幕面 Z=0，开口端朝 +Z，原始高度 16.0mm，内腔半径 24.46mm。
  * 后盖 (lid)   : 外表面(贴手机侧) Z=0，朝机身的一面朝 +Z。
  * 装配关系     : 后盖绕水平轴翻转 180° 后扣到机身上，
                   body(x, y, z) = lid(x, -y, ZC - z)。

螺纹为右旋、梯形(45° 牙侧，FDM 打印无需支撑)，
机身为外螺纹(加高一段 collar)，后盖为内螺纹(裙边)。
后盖插塞(plug)尖端在机身坐标系中保持在 Z=13.5mm，
因此机身内部可用空间与原设计完全一致。
"""
import math

# ---- 螺纹几何 ---------------------------------------------------------
PITCH = 2.0            # 螺距 (mm)
DEPTH = 0.6            # 牙深 (mm)
CREST_FLAT = 0.4       # 牙顶平台宽度 (mm)，牙底平台宽度 = 2.0-0.4-2*0.6 = 0.4
CLEARANCE = 0.25       # 内外螺纹径向配合间隙 (mm)，FDM 经验值

# ---- 机身 -------------------------------------------------------------
R_BORE = 24.46         # 机身内腔半径 (原始值，保持不变)
R_BODY_OUT = 26.46     # 机身原始外半径 = 螺纹牙顶半径
R_ROOT = R_BODY_OUT - DEPTH    # 螺纹牙底半径 25.86 (壁厚仍有 1.4mm)
BODY_RIM_Z = 16.0      # 机身原始开口端面高度
COLLAR_EMBED_Z = 15.8  # collar 下端嵌入机身壁内 0.2mm，保证切片时与机身融合
COLLAR_TOP_Z = 21.5    # 加高后的机身顶面高度 (collar 高 5.5mm)
PLUG_TIP_BODY_Z = 13.5 # 插塞尖端在机身坐标系中的高度 (= 原设计 16.0-2.5)

# ---- 后盖 -------------------------------------------------------------
Z_SHOULDER = 2.57                          # 后盖座面(贴合 collar 顶面)
ZC = COLLAR_TOP_Z + Z_SHOULDER             # lid_z = ZC - body_z
SKIRT_END_BODY_Z = BODY_RIM_Z + 0.3        # 裙边端面距机身原端面保留 0.3mm
Z_SKIRT_TOP = ZC - SKIRT_END_BODY_Z        # 裙边端面 (lid 坐标)
Z_PLUG_TOP = ZC - PLUG_TIP_BODY_Z          # 插塞尖端 (lid 坐标)
R_LID_OUT = 28.5
R_LID_MAJOR = R_BODY_OUT + CLEARANCE       # 内螺纹牙底(大径)半径
R_LID_MINOR = R_ROOT + CLEARANCE           # 内螺纹牙顶(小径)半径


def _profile(s):
    """螺纹截面 0..1 (1=牙顶, 0=牙底)，s 为沿轴向相位 (mm)，周期 PITCH。"""
    t = s % PITCH
    d = abs(t - PITCH / 2.0)            # 距牙顶中心的距离
    half = CREST_FLAT / 2.0
    if d <= half:
        return 1.0
    if d >= half + DEPTH:               # 45° 牙侧 => 水平投影 = 牙深
        return 0.0
    return 1.0 - (d - half) / DEPTH


def body_r(theta, z):
    """机身 collar 外表面半径 (右旋: z 随 theta 增大而升高)。"""
    s = z - PITCH * theta / (2.0 * math.pi)
    r = R_ROOT + DEPTH * _profile(s)
    # 顶端 45° 导入倒角
    r = min(r, R_ROOT + (COLLAR_TOP_Z - z))
    return r


def lid_r(theta, z):
    """后盖裙边内螺纹半径 (lid 坐标)。"""
    zb = ZC - z                          # 对应机身坐标
    s = zb + PITCH * theta / (2.0 * math.pi)   # theta_body = -theta_lid
    r = R_ROOT + DEPTH * _profile(s) + CLEARANCE
    # 裙边开口端 45° 导入倒角
    r = max(r, R_LID_MAJOR - (Z_SKIRT_TOP - z))
    return r
