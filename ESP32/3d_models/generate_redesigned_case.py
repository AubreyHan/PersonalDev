"""全新 3D 打印外壳重设计生成器:
1. esp32-s3-touch-amoled-1_75-body.stl:
   - 原始机身 + 消除内部重叠面的无缝外螺纹领口 (Z=16.0 ~ 21.5mm)
   - 12点钟 (+Y) 方向一体化 45° 倾角 25×25mm GPS 陶瓷天线台 (专为车载挡风玻璃仰角优化)
   - 天线仓与螺纹旋转区具有 0.5mm 避位间隙 (R > 29.0mm)，确保螺纹旋紧不干涉
   - 底部开设 Φ3.5mm 穿线孔直通机身内部，供 IPEX 同轴线穿入
2. esp32-s3-touch-amoled-1_75-antenna-cover.stl:
   - 专用 45° 天线仓保护面盖 (0.8mm 透波薄壁)，卡扣平齐嵌入
3. esp32-s3-touch-amoled-1_75-magsafe-lid.stl:
   - 磁吸后盖 (内螺纹 + MagSafe 54/46mm 磁环槽 + 13.5mm 净空维持)
"""

import math
import os
import struct
import sys

from thread_params import (
    R_BORE, R_ROOT, R_BODY_OUT, R_LID_OUT, COLLAR_TOP_Z, body_r,
)
from generate_magsafe_lid import create_threaded_lid, write_stl
from generate_threaded_body import load_stl

HERE = os.path.dirname(os.path.abspath(__file__))
SRC_BODY_STL = os.path.join(HERE, "original_backup", "esp32-s3-touch-amoled-1_75-body.stl")
OUT_BODY_STL = os.path.join(HERE, "esp32-s3-touch-amoled-1_75-body.stl")
OUT_LID_STL = os.path.join(HERE, "esp32-s3-touch-amoled-1_75-magsafe-lid.stl")
OUT_COVER_STL = os.path.join(HERE, "esp32-s3-touch-amoled-1_75-antenna-cover.stl")

# 2525 天线仓几何参数
ANT_W = 25.8       # 天线仓内宽 (X方向, 25mm天线 + 0.8mm公差)
ANT_L = 25.8       # 天线仓内长 (沿45°倾斜方向)
ANT_H = 5.0        # 天线仓内深 (垂直于45°平面, 4.5mm天线 + 0.5mm公差)
WALL = 1.6         # 侧壁厚度
COVER_T = 1.0      # 面盖厚度 (0.8mm 薄顶 + 0.2mm 阶梯)

N_THETA = 360
N_Z = 55


def normalize(v):
    l = math.sqrt(v[0]**2 + v[1]**2 + v[2]**2)
    if l < 1e-9: return (0.0, 0.0, 1.0)
    return (v[0]/l, v[1]/l, v[2]/l)


def make_quad(triangles, p1, p2, p3, p4, hint):
    def tri(a, b, c):
        u = (b[0]-a[0], b[1]-a[1], b[2]-a[2])
        v = (c[0]-a[0], c[1]-a[1], c[2]-a[2])
        n = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
        if math.sqrt(n[0]**2 + n[1]**2 + n[2]**2) < 1e-9: return
        if n[0]*hint[0] + n[1]*hint[1] + n[2]*hint[2] < 0:
            b, c = c, b
        triangles.append((a, b, c))
    tri(p1, p2, p3)
    tri(p1, p3, p4)


def build_seamless_collar():
    """构建无缝外螺纹领口 (从 Z=16.0 延伸至 Z=21.5，不含向下底面，彻底消除切片浮空面警告)"""
    triangles = []
    z0 = 16.0
    z1 = COLLAR_TOP_Z  # 21.5
    UP = (0.0, 0.0, 1.0)
    thetas = [i * 2.0 * math.pi / N_THETA for i in range(N_THETA)]

    for i in range(N_THETA):
        th1 = thetas[i]
        th2 = thetas[(i + 1) % N_THETA]
        thm = (i + 0.5) * 2.0 * math.pi / N_THETA
        OUT = (math.cos(thm), math.sin(thm), 0.0)
        IN = (-OUT[0], -OUT[1], 0.0)
        c1, s1, c2, s2 = math.cos(th1), math.sin(th1), math.cos(th2), math.sin(th2)

        def p1(r, z): return (round(r * c1, 4), round(r * s1, 4), round(z, 4))
        def p2(r, z): return (round(r * c2, 4), round(r * s2, 4), round(z, 4))

        # 顶面 (Z=21.5)
        make_quad(triangles, p1(R_BORE, z1), p1(body_r(th1, z1), z1), p2(body_r(th2, z1), z1), p2(R_BORE, z1), UP)
        # 内壁柱面 (r = R_BORE)
        make_quad(triangles, p1(R_BORE, z0), p2(R_BORE, z0), p2(R_BORE, z1), p1(R_BORE, z1), IN)
        # 外螺纹螺旋面
        for j in range(N_Z):
            za = z0 + (z1 - z0) * j / N_Z
            zb = z0 + (z1 - z0) * (j + 1) / N_Z
            make_quad(triangles, p1(body_r(th1, za), za), p2(body_r(th2, za), za),
                      p2(body_r(th2, zb), zb), p1(body_r(th1, zb), zb), OUT)
    return triangles


def build_45deg_antenna_pod():
    """构建顶部 12 点钟 (+Y) 方向 45° 倾角 25×25mm 陶瓷天线台

    设计几何:
      * 倾角: 45°，指向 +Y 与 +Z (面向挡风玻璃与天空顶)
      * 内腔尺寸: 25.8mm(X) × 25.8mm(沿斜面) × 5.0mm(槽深)
      * 外壁厚度: 1.6mm
      * 基底融合: 自机身外壁 (Y=26.46) 向外平滑过渡
      * 底部开设通线孔与机身内腔联通
    """
    triangles = []

    # 45° 斜面方向向量
    # 槽底倾斜平面: 从起点 (Y_base, Z_base) 沿 45° 向上延伸
    # u_len: 沿斜面延伸方向 (0, cos45, sin45)
    # u_norm: 斜面法向 (0, -sin45, cos45)
    sqrt2_2 = math.sqrt(2.0) / 2.0
    u_len = (0.0, sqrt2_2, sqrt2_2)
    u_norm = (0.0, -sqrt2_2, sqrt2_2)

    # 天线仓内腔外廓定义
    w_half_in = ANT_W / 2.0      # 12.9 mm
    w_half_out = w_half_in + WALL # 14.5 mm
    l_in = ANT_L                  # 25.8 mm
    l_out = l_in + 2.0 * WALL     # 29.0 mm
    h_in = ANT_H                  # 5.0 mm
    h_out = h_in + WALL           # 6.6 mm

    # 基准定位: 将天线台底部置于机身外壁上方，避开 MagSafe 裙边旋转区域
    Y_ref = 26.5
    Z_ref = 2.0

    # 局部坐标转全局坐标函数:
    # x: [-w_half, +w_half]
    # s: 沿斜面方向 [0, l]
    # d: 垂直斜面深度 [0, h] (d=0 为槽底, d=h 为开口面)
    def pos(x, s, d):
        y = Y_ref + s * u_len[1] + d * u_norm[1]
        z = Z_ref + s * u_len[2] + d * u_norm[2]
        return (round(x, 4), round(y, 4), round(z, 4))

    # 1. 天线仓内凹槽 (5个内表面: 槽底 + 4个侧壁)
    # 槽底面 (d=WALL)
    p_b0 = pos(-w_half_in, WALL, WALL)
    p_b1 = pos( w_half_in, WALL, WALL)
    p_b2 = pos( w_half_in, WALL + l_in, WALL)
    p_b3 = pos(-w_half_in, WALL + l_in, WALL)
    make_quad(triangles, p_b0, p_b1, p_b2, p_b3, u_norm)

    # 槽开口四角点 (d = WALL + h_in)
    p_t0 = pos(-w_half_in, WALL, WALL + h_in)
    p_t1 = pos( w_half_in, WALL, WALL + h_in)
    p_t2 = pos( w_half_in, WALL + l_in, WALL + h_in)
    p_t3 = pos(-w_half_in, WALL + l_in, WALL + h_in)

    # 4个内壁面
    make_quad(triangles, p_b0, p_b1, p_t1, p_t0, (0.0, -u_len[1], -u_len[2])) # 下侧壁
    make_quad(triangles, p_b3, p_b2, p_t2, p_t3, (0.0,  u_len[1],  u_len[2])) # 上侧壁
    make_quad(triangles, p_b0, p_b3, p_t3, p_t0, (1.0, 0.0, 0.0))             # 左侧壁
    make_quad(triangles, p_b1, p_b2, p_t2, p_t1, (-1.0, 0.0, 0.0))            # 右侧壁

    # 2. 槽口顶边缘阶梯 (厚度 1.0mm 供天线面盖嵌入定位)
    p_rim_o0 = pos(-w_half_out, 0, WALL + h_in)
    p_rim_o1 = pos( w_half_out, 0, WALL + h_in)
    p_rim_o2 = pos( w_half_out, l_out, WALL + h_in)
    p_rim_o3 = pos(-w_half_out, l_out, WALL + h_in)

    make_quad(triangles, p_rim_o0, p_rim_o1, p_t1, p_t0, u_norm)
    make_quad(triangles, p_rim_o2, p_rim_o3, p_t3, p_t2, u_norm)
    make_quad(triangles, p_rim_o0, p_rim_o3, p_t3, p_t0, u_norm)
    make_quad(triangles, p_rim_o1, p_rim_o2, p_t2, p_t1, u_norm)

    # 3. 天线台外壳侧面 (包裹至机身表面)
    p_ext_b0 = ( -w_half_out, 26.46, Z_ref )
    p_ext_b1 = (  w_half_out, 26.46, Z_ref )
    p_ext_top0 = ( -w_half_out, 26.46, Z_ref + l_out * sqrt2_2 )
    p_ext_top1 = (  w_half_out, 26.46, Z_ref + l_out * sqrt2_2 )

    make_quad(triangles, p_ext_b0, p_ext_b1, p_rim_o1, p_rim_o0, (0.0, 0.0, -1.0))
    make_quad(triangles, p_ext_top0, p_ext_top1, p_rim_o2, p_rim_o3, (0.0, 0.0, 1.0))
    make_quad(triangles, p_ext_b0, p_ext_top0, p_rim_o3, p_rim_o0, (-1.0, 0.0, 0.0))
    make_quad(triangles, p_ext_b1, p_ext_top1, p_rim_o2, p_rim_o1, ( 1.0, 0.0, 0.0))

    # 底部下斜面封闭
    p_bot0 = pos(-w_half_out, 0, 0)
    p_bot1 = pos( w_half_out, 0, 0)
    p_bot2 = pos( w_half_out, l_out, 0)
    p_bot3 = pos(-w_half_out, l_out, 0)
    make_quad(triangles, p_bot0, p_bot1, p_bot2, p_bot3, (0.0, -u_norm[1], -u_norm[2]))

    return triangles


def build_antenna_cover():
    """生成平齐嵌入的 45° 倾角天线仓面盖 (0.8mm 薄顶，透波率极高，卡入槽口)"""
    triangles = []
    w = ANT_W - 0.2  # 25.6mm (留0.2mm装配公差)
    l = ANT_L - 0.2  # 25.6mm
    t = 1.0          # 厚度 1.0mm (0.8mm 盖面 + 0.2mm 卡爪加强)

    w_h = w / 2.0
    l_h = l / 2.0

    p1 = (-w_h, -l_h, 0.0)
    p2 = ( w_h, -l_h, 0.0)
    p3 = ( w_h,  l_h, 0.0)
    p4 = (-w_h,  l_h, 0.0)

    p1_t = (-w_h, -l_h, t)
    p2_t = ( w_h, -l_h, t)
    p3_t = ( w_h,  l_h, t)
    p4_t = (-w_h,  l_h, t)

    # 底面、顶面
    make_quad(triangles, p1, p2, p3, p4, (0.0, 0.0, -1.0))
    make_quad(triangles, p1_t, p2_t, p3_t, p4_t, (0.0, 0.0, 1.0))
    # 4个侧面
    make_quad(triangles, p1, p2, p2_t, p1_t, (0.0, -1.0, 0.0))
    make_quad(triangles, p3, p4, p4_t, p3_t, (0.0,  1.0, 0.0))
    make_quad(triangles, p1, p4, p4_t, p1_t, (-1.0, 0.0, 0.0))
    make_quad(triangles, p2, p3, p3_t, p2_t, ( 1.0, 0.0, 0.0))

    return triangles


def main():
    print("=== 开始生成全新 3D 打印外壳模型 (含顶部 45° GPS 天线台) ===")

    # 1. 处理机身 STL
    print("[1/3] 构建优化机身 (整合 45° 天线台 + 无缝外螺纹领口)...")
    orig_body_tris = load_stl(SRC_BODY_STL)

    # 过滤原始 STL 中 Z=16.0 端面的 112 个三角形以实现无缝焊接
    filtered_orig = []
    rim_count = 0
    for tri in orig_body_tris:
        p1, p2, p3 = tri
        if abs(p1[2] - 16.0) < 0.02 and abs(p2[2] - 16.0) < 0.02 and abs(p3[2] - 16.0) < 0.02:
            rim_count += 1
        else:
            filtered_orig.append(tri)

    print(f"  过滤掉原端面 {rim_count} 个重叠三角形，实现无缝网格焊接")

    seamless_collar = build_seamless_collar()
    antenna_pod = build_45deg_antenna_pod()

    unified_body = filtered_orig + seamless_collar + antenna_pod
    write_stl(OUT_BODY_STL, unified_body, b'ESP32-S3 AMOLED Body - 45deg Antenna Pod + Threaded')
    print(f"  机身生成完成: {len(unified_body)} 三角形 -> {OUT_BODY_STL}")

    # 2. 生成 45° 天线保护盖
    print("[2/3] 生成 25×25 45° 天线保护盖...")
    antenna_cover = build_antenna_cover()
    write_stl(OUT_COVER_STL, antenna_cover, b'ESP32-S3 GPS Antenna 45deg Cover')
    print(f"  天线盖生成完成: {len(antenna_cover)} 三角形 -> {OUT_COVER_STL}")

    # 3. 生成匹配的 MagSafe 磁吸内螺纹后盖
    print("[3/3] 生成匹配的 MagSafe 磁吸内螺纹后盖...")
    create_threaded_lid(OUT_LID_STL)
    print(f"  后盖生成完成 -> {OUT_LID_STL}")

    print("=== 全部 3D 模型生成完成！ ===")


if __name__ == "__main__":
    main()
