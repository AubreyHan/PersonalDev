#!/usr/bin/env python3
import math
import struct
import os

def norm(v):
    l = math.sqrt(v[0]**2 + v[1]**2 + v[2]**2)
    return (v[0]/l, v[1]/l, v[2]/l) if l > 0 else (0.0, 0.0, 0.0)

def cross(a, b):
    return (
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0]
    )

def sub(a, b):
    return (a[0]-b[0], a[1]-b[1], a[2]-b[2])

class ModularSTL:
    def __init__(self):
        self.triangles = []

    def add_triangle(self, v0, v1, v2):
        self.triangles.append((v0, v1, v2))

    def add_quad(self, v0, v1, v2, v3):
        self.triangles.append((v0, v1, v2))
        self.triangles.append((v0, v2, v3))

    def add_box(self, x0, x1, y0, y1, z0, z1):
        v = [
            (x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
            (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)
        ]
        self.add_quad(v[0], v[3], v[2], v[1]) # Bottom (-Z)
        self.add_quad(v[4], v[5], v[6], v[7]) # Top (+Z)
        self.add_quad(v[0], v[1], v[5], v[4]) # Front (-Y)
        self.add_quad(v[1], v[2], v[6], v[5]) # Right (+X)
        self.add_quad(v[2], v[3], v[7], v[6]) # Back (+Y)
        self.add_quad(v[3], v[0], v[4], v[7]) # Left (-X)

    def add_tri_prism_z(self, p0, p1, p2, z0, z1):
        v0 = (p0[0], p0[1], z0)
        v1 = (p1[0], p1[1], z0)
        v2 = (p2[0], p2[1], z0)
        v3 = (p0[0], p0[1], z1)
        v4 = (p1[0], p1[1], z1)
        v5 = (p2[0], p2[1], z1)
        self.add_triangle(v0, v2, v1)
        self.add_triangle(v3, v4, v5)
        self.add_quad(v0, v1, v4, v3)
        self.add_quad(v1, v2, v5, v4)
        self.add_quad(v2, v0, v3, v5)

    def add_corner_arc(self, cx, cy, r_in, r_out, a_start, a_end, z0, z1, n_segs=8):
        for i in range(n_segs):
            a0 = a_start + (a_end - a_start) * i / float(n_segs)
            a1 = a_start + (a_end - a_start) * (i + 1) / float(n_segs)
            p_in0  = (cx + r_in  * math.cos(a0), cy + r_in  * math.sin(a0))
            p_out0 = (cx + r_out * math.cos(a0), cy + r_out * math.sin(a0))
            p_out1 = (cx + r_out * math.cos(a1), cy + r_out * math.sin(a1))
            p_in1  = (cx + r_in  * math.cos(a1), cy + r_in  * math.sin(a1))
            self.add_tri_prism_z(p_in0, p_out0, p_out1, z0, z1)
            self.add_tri_prism_z(p_in0, p_out1, p_in1, z0, z1)

    def add_solid_corner(self, cx, cy, r, a_start, a_end, z0, z1, n_segs=8):
        p_center = (cx, cy)
        for i in range(n_segs):
            a0 = a_start + (a_end - a_start) * i / float(n_segs)
            a1 = a_start + (a_end - a_start) * (i + 1) / float(n_segs)
            p0 = (cx + r * math.cos(a0), cy + r * math.sin(a0))
            p1 = (cx + r * math.cos(a1), cy + r * math.sin(a1))
            self.add_tri_prism_z(p_center, p0, p1, z0, z1)

    def check_watertight(self):
        edge_count = {}
        for tri in self.triangles:
            for k in range(3):
                v_start = tri[k]
                v_end = tri[(k + 1) % 3]
                e = (
                    (round(v_start[0], 4), round(v_start[1], 4), round(v_start[2], 4)),
                    (round(v_end[0], 4), round(v_end[1], 4), round(v_end[2], 4))
                )
                re = (e[1], e[0])
                if re in edge_count:
                    edge_count[re] -= 1
                    if edge_count[re] == 0:
                        del edge_count[re]
                else:
                    edge_count[e] = edge_count.get(e, 0) + 1
        unmatched = sum(abs(c) for c in edge_count.values())
        return unmatched == 0, len(self.triangles), unmatched

    def save_binary(self, filepath, name="Model"):
        header = f"BambuStudio Binary STL - {name}".encode('ascii')[:80].ljust(80, b'\x00')
        with open(filepath, 'wb') as f:
            f.write(header)
            f.write(struct.pack('<I', len(self.triangles)))
            for tri in self.triangles:
                n = norm(cross(sub(tri[1], tri[0]), sub(tri[2], tri[0])))
                f.write(struct.pack('<3f', *n))
                for v in tri:
                    f.write(struct.pack('<3f', float(v[0]), float(v[1]), float(v[2])))
                f.write(struct.pack('<H', 0))

m = ModularSTL()
m.add_box(-10, 10, -10, 10, 0, 5)
m.add_corner_arc(0, 0, 10, 15, 0, math.pi*2, 0, 5, n_segs=36)
is_wt, n_t, un = m.check_watertight()
print(f"Test watertight: {is_wt}, triangles: {n_t}, unmatched: {un}")
