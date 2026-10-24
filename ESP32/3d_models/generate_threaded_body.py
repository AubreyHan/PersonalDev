"""在原始机身 STL 上加高并加工外螺纹 collar (与 generate_magsafe_lid.py 配套)。

  * 输入 : original_backup/esp32-s3-touch-amoled-1_75-body.stl (原始机身，不修改)
  * 输出 : esp32-s3-touch-amoled-1_75-body.stl (原机身 + 外螺纹 collar)

collar 是内径 24.46mm (与原内腔相同) 的薄壁螺纹管，下端嵌入原机身壁内 0.2mm，
作为独立壳体与机身在切片时自动融合。原机身网格 (含侧面开孔) 一字节不改，
内腔 Z=0~16 的空间完全不变。
"""
import math
import os
import re
import struct
import sys

from thread_params import (
    R_BORE, R_ROOT, COLLAR_EMBED_Z, COLLAR_TOP_Z, body_r,
)
from generate_magsafe_lid import write_stl

HERE = os.path.dirname(os.path.abspath(__file__))
SRC_STL = os.path.join(HERE, "original_backup", "esp32-s3-touch-amoled-1_75-body.stl")
OUT_STL = os.path.join(HERE, "esp32-s3-touch-amoled-1_75-body.stl")

N_THETA = 360
N_Z = 55               # (21.5-15.8)/55 ≈ 0.104mm


def load_stl(path):
    d = open(path, 'rb').read()
    if d[:5] == b'solid' and b'facet' in d[:300]:
        v = [tuple(map(float, m)) for m in re.findall(rb'vertex\s+(\S+)\s+(\S+)\s+(\S+)', d)]
        return [tuple(v[i:i + 3]) for i in range(0, len(v), 3)]
    n = struct.unpack('<I', d[80:84])[0]
    tris = []
    for i in range(n):
        f = struct.unpack('<12f', d[84 + i * 50:84 + i * 50 + 48])
        tris.append((tuple(f[3:6]), tuple(f[6:9]), tuple(f[9:12])))
    return tris


def build_collar():
    triangles = []

    def tri(a, b, c, hint):
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

    z0, z1 = COLLAR_EMBED_Z, COLLAR_TOP_Z
    UP, DOWN = (0.0, 0.0, 1.0), (0.0, 0.0, -1.0)
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

        # 底面 (嵌入机身壁内)、顶面
        quad(p1(R_BORE, z0), p1(body_r(th1, z0), z0), p2(body_r(th2, z0), z0), p2(R_BORE, z0), DOWN)
        quad(p1(R_BORE, z1), p1(body_r(th1, z1), z1), p2(body_r(th2, z1), z1), p2(R_BORE, z1), UP)
        # 内壁 (与原内腔同径)
        quad(p1(R_BORE, z0), p2(R_BORE, z0), p2(R_BORE, z1), p1(R_BORE, z1), IN)
        # 外螺纹螺旋面
        for j in range(N_Z):
            za = z0 + (z1 - z0) * j / N_Z
            zb = z0 + (z1 - z0) * (j + 1) / N_Z
            quad(p1(body_r(th1, za), za), p2(body_r(th2, za), za),
                 p2(body_r(th2, zb), zb), p1(body_r(th1, zb), zb), OUT)
    return triangles


def create_threaded_body(src, out):
    original = load_stl(src)
    collar = build_collar()
    write_stl(out, original + collar,
              b'ESP32-S3-AMOLED Body - Threaded collar (Front Face Z=0)')
    print(f"  original {len(original)} + collar {len(collar)} triangles")


if __name__ == "__main__":
    create_threaded_body(SRC_STL, sys.argv[1] if len(sys.argv) > 1 else OUT_STL)
