"""生成带螺纹的 MagSafe 磁吸后盖 (与 generate_threaded_body.py 生成的机身配套)。

相对原设计的改动
  1. 后盖增加内螺纹裙边 (外径仍为 57.0mm)，与机身加高的外螺纹 collar 旋合，避免脱落。
  2. 插塞(plug)加长，使其尖端在机身坐标系中仍停在 Z=13.5mm，
     机身内部可用空间与原设计完全一致。
  3. 所有面片按外法线方向统一缠绕 (原模型缠绕混乱，有符号体积异常)。
参数见 thread_params.py。
"""
import math
import os
import struct
import sys

from thread_params import (
    R_LID_OUT, Z_SHOULDER, Z_SKIRT_TOP, Z_PLUG_TOP, lid_r,
)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_STL = os.path.join(HERE, "esp32-s3-touch-amoled-1_75-magsafe-lid.stl")

N_THETA = 360          # 1° 分辨率，保证螺旋面平滑
N_Z_THREAD = 52        # 裙边内螺纹沿 Z 的分段 (约 0.1mm/段)


def create_threaded_lid(out_stl_path):
    thetas = [i * 2.0 * math.pi / N_THETA for i in range(N_THETA)]

    # Radii definitions (与原设计一致)
    R_HUB = 5.0
    R_WIN_OUT = 20.5
    R_STEP_IN = 21.6
    R_MAG_IN = 22.4
    R_MAG_OUT = 27.6
    R_PLUG_IN = 22.0
    R_PLUG = 24.35
    R_PLUG_TIP = 23.85

    # Z levels
    Z_BED = 0.00
    Z_GROOVE = 0.80
    Z_SHLD = Z_SHOULDER
    Z_PLUG_CHAMF = Z_PLUG_TOP - 0.5
    Z_TOP = Z_PLUG_TOP

    def is_center_window(deg):
        deg = deg % 360.0
        quads = [(65, 115), (155, 205), (245, 295), (335, 360), (0, 25)]
        for q_min, q_max in quads:
            if q_min <= deg <= q_max:
                return True
        return False

    triangles = []

    def tri(a, b, c, hint):
        """按期望外法线 hint 自动校正缠绕方向；退化三角形丢弃。"""
        u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        if math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) < 1e-9:
            return
        if n[0] * hint[0] + n[1] * hint[1] + n[2] * hint[2] < 0:
            b, c = c, b
        triangles.append((a, b, c))

    def quad(p1, p2, p3, p4, hint):
        tri(p1, p2, p3, hint)
        tri(p1, p3, p4, hint)

    UP = (0.0, 0.0, 1.0)
    DOWN = (0.0, 0.0, -1.0)

    for i in range(N_THETA):
        th1 = thetas[i]
        th2 = thetas[(i + 1) % N_THETA]
        thm = (i + 0.5) * 2.0 * math.pi / N_THETA
        deg_mid = math.degrees(thm) % 360.0
        c1, s1 = math.cos(th1), math.sin(th1)
        c2, s2 = math.cos(th2), math.sin(th2)
        OUT = (math.cos(thm), math.sin(thm), 0.0)
        IN = (-OUT[0], -OUT[1], 0.0)

        has_win = is_center_window(deg_mid)

        def pt1(r, z): return (round(r * c1, 4), round(r * s1, 4), round(z, 4))
        def pt2(r, z): return (round(r * c2, 4), round(r * s2, 4), round(z, 4))

        # --- 1. 中心 hub ---
        tri((0.0, 0.0, Z_BED), pt1(R_HUB, Z_BED), pt2(R_HUB, Z_BED), DOWN)
        tri((0.0, 0.0, Z_SHLD), pt1(R_HUB, Z_SHLD), pt2(R_HUB, Z_SHLD), UP)

        # --- 2. 辐条 / 窗口区 (R_HUB .. R_WIN_OUT) ---
        if not has_win:
            quad(pt1(R_HUB, Z_BED), pt1(R_WIN_OUT, Z_BED), pt2(R_WIN_OUT, Z_BED), pt2(R_HUB, Z_BED), DOWN)
            quad(pt1(R_HUB, Z_SHLD), pt1(R_WIN_OUT, Z_SHLD), pt2(R_WIN_OUT, Z_SHLD), pt2(R_HUB, Z_SHLD), UP)
        else:
            # hub 外壁 (材料在内侧 -> 法线朝外)
            quad(pt1(R_HUB, Z_BED), pt2(R_HUB, Z_BED), pt2(R_HUB, Z_SHLD), pt1(R_HUB, Z_SHLD), OUT)
            # 窗口外缘壁 (材料在外侧 -> 法线朝轴心)
            quad(pt1(R_WIN_OUT, Z_SHLD), pt2(R_WIN_OUT, Z_SHLD), pt2(R_WIN_OUT, Z_BED), pt1(R_WIN_OUT, Z_BED), IN)

        # 窗口/辐条交界处的径向侧壁 (法线指向空侧)
        has_win_prev = is_center_window((thm - 2.0 * math.pi / N_THETA) * 180.0 / math.pi)
        if has_win_prev != has_win:
            tang = (-math.sin(th1), math.cos(th1), 0.0)
            hint = tang if has_win else (-tang[0], -tang[1], 0.0)
            quad(pt1(R_HUB, Z_BED), pt1(R_WIN_OUT, Z_BED), pt1(R_WIN_OUT, Z_SHLD), pt1(R_HUB, Z_SHLD), hint)

        # --- 3. R_WIN_OUT .. R_STEP_IN ---
        quad(pt1(R_WIN_OUT, Z_BED), pt1(R_STEP_IN, Z_BED), pt2(R_STEP_IN, Z_BED), pt2(R_WIN_OUT, Z_BED), DOWN)
        quad(pt1(R_WIN_OUT, Z_SHLD), pt1(R_STEP_IN, Z_SHLD), pt2(R_STEP_IN, Z_SHLD), pt2(R_WIN_OUT, Z_SHLD), UP)

        # --- 4. 内侧挡边 R_STEP_IN .. R_MAG_IN ---
        quad(pt1(R_STEP_IN, Z_BED), pt1(R_MAG_IN, Z_BED), pt2(R_MAG_IN, Z_BED), pt2(R_STEP_IN, Z_BED), DOWN)
        quad(pt1(R_STEP_IN, Z_SHLD), pt1(R_PLUG_IN, Z_SHLD), pt2(R_PLUG_IN, Z_SHLD), pt2(R_STEP_IN, Z_SHLD), UP)
        # MagSafe 凹槽内壁 (材料在内侧)
        quad(pt1(R_MAG_IN, Z_BED), pt2(R_MAG_IN, Z_BED), pt2(R_MAG_IN, Z_GROOVE), pt1(R_MAG_IN, Z_GROOVE), OUT)

        # --- 5. MagSafe 凹槽 ---
        quad(pt1(R_MAG_IN, Z_GROOVE), pt1(R_MAG_OUT, Z_GROOVE), pt2(R_MAG_OUT, Z_GROOVE), pt2(R_MAG_IN, Z_GROOVE), DOWN)
        # 凹槽外壁 (材料在外侧)
        quad(pt1(R_MAG_OUT, Z_GROOVE), pt2(R_MAG_OUT, Z_GROOVE), pt2(R_MAG_OUT, Z_BED), pt1(R_MAG_OUT, Z_BED), IN)

        # --- 6. 外圈 + 内螺纹裙边 ---
        quad(pt1(R_MAG_OUT, Z_BED), pt1(R_LID_OUT, Z_BED), pt2(R_LID_OUT, Z_BED), pt2(R_MAG_OUT, Z_BED), DOWN)
        # 外圆柱面: 从 Z_BED 一直到裙边端面
        quad(pt1(R_LID_OUT, Z_BED), pt2(R_LID_OUT, Z_BED), pt2(R_LID_OUT, Z_SKIRT_TOP), pt1(R_LID_OUT, Z_SKIRT_TOP), OUT)
        # 裙边端面
        quad(pt1(lid_r(th1, Z_SKIRT_TOP), Z_SKIRT_TOP), pt1(R_LID_OUT, Z_SKIRT_TOP),
             pt2(R_LID_OUT, Z_SKIRT_TOP), pt2(lid_r(th2, Z_SKIRT_TOP), Z_SKIRT_TOP), UP)
        # 座面 (贴合机身 collar 顶面): R_PLUG .. 内螺纹
        quad(pt1(R_PLUG, Z_SHLD), pt1(lid_r(th1, Z_SHLD), Z_SHLD),
             pt2(lid_r(th2, Z_SHLD), Z_SHLD), pt2(R_PLUG, Z_SHLD), UP)
        # 内螺纹螺旋面 (材料在外侧 -> 法线朝轴心)
        for j in range(N_Z_THREAD):
            za = Z_SHLD + (Z_SKIRT_TOP - Z_SHLD) * j / N_Z_THREAD
            zb = Z_SHLD + (Z_SKIRT_TOP - Z_SHLD) * (j + 1) / N_Z_THREAD
            quad(pt1(lid_r(th1, za), za), pt2(lid_r(th2, za), za),
                 pt2(lid_r(th2, zb), zb), pt1(lid_r(th1, zb), zb), IN)

        # --- 7. 插塞 (已加长，尖端在机身坐标 Z=13.5mm) ---
        quad(pt1(R_PLUG, Z_SHLD), pt1(R_PLUG, Z_PLUG_CHAMF), pt2(R_PLUG, Z_PLUG_CHAMF), pt2(R_PLUG, Z_SHLD), OUT)
        quad(pt1(R_PLUG, Z_PLUG_CHAMF), pt1(R_PLUG_TIP, Z_TOP), pt2(R_PLUG_TIP, Z_TOP), pt2(R_PLUG, Z_PLUG_CHAMF),
             (OUT[0], OUT[1], 1.0))
        quad(pt1(R_PLUG_IN, Z_TOP), pt2(R_PLUG_IN, Z_TOP), pt2(R_PLUG_TIP, Z_TOP), pt1(R_PLUG_TIP, Z_TOP), UP)
        quad(pt1(R_PLUG_IN, Z_TOP), pt1(R_PLUG_IN, Z_SHLD), pt2(R_PLUG_IN, Z_SHLD), pt2(R_PLUG_IN, Z_TOP), IN)

    write_stl(out_stl_path, triangles,
              b'ESP32-S3-AMOLED MagSafe Lid - Threaded (Flat Bed Z=0)')
    return triangles


def write_stl(path, triangles, header):
    with open(path, 'wb') as f:
        f.write(header.ljust(80, b'\x00')[:80])
        f.write(struct.pack('<I', len(triangles)))
        for p1, p2, p3 in triangles:
            u = (p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2])
            v = (p3[0] - p1[0], p3[1] - p1[1], p3[2] - p1[2])
            nx = u[1] * v[2] - u[2] * v[1]
            ny = u[2] * v[0] - u[0] * v[2]
            nz = u[0] * v[1] - u[1] * v[0]
            l = math.sqrt(nx * nx + ny * ny + nz * nz)
            if l > 1e-12:
                nx, ny, nz = nx / l, ny / l, nz / l
            f.write(struct.pack('<12fH', nx, ny, nz, *p1, *p2, *p3, 0))
    print(f"Generated {len(triangles)} triangles to {path}")


if __name__ == "__main__":
    create_threaded_lid(sys.argv[1] if len(sys.argv) > 1 else OUT_STL)
