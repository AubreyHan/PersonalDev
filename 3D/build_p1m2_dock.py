#!/usr/bin/env python3
"""
Columbus P-1 Mark II (P-1 M2) Mounting Dock Generator
Option B: Straight vertical walls (friction / foam fit, open front box)
Mounting: Completely flat bottom exterior for double-sided tape (3M VHB)

Composed of closed manifold polyhedral blocks (Bambu Studio / Slicer Native Union).
Zero open edges, 100% watertight.
"""

import math
import struct
import os
import subprocess

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

class STLWriter:
    def __init__(self):
        self.triangles = []

    def add_triangle(self, v0, v1, v2):
        self.triangles.append((v0, v1, v2))

    def add_quad(self, v0, v1, v2, v3):
        # CCW: v0 -> v1 -> v2, v0 -> v2 -> v3
        self.triangles.append((v0, v1, v2))
        self.triangles.append((v0, v2, v3))

    def add_box(self, x0, x1, y0, y1, z0, z1):
        v = [
            (x0, y0, z0), # 0
            (x1, y0, z0), # 1
            (x1, y1, z0), # 2
            (x0, y1, z0), # 3
            (x0, y0, z1), # 4
            (x1, y0, z1), # 5
            (x1, y1, z1), # 6
            (x0, y1, z1), # 7
        ]
        # Bottom (-Z)
        self.add_quad(v[0], v[3], v[2], v[1])
        # Top (+Z)
        self.add_quad(v[4], v[5], v[6], v[7])
        # Front (-Y)
        self.add_quad(v[0], v[1], v[5], v[4])
        # Right (+X)
        self.add_quad(v[1], v[2], v[6], v[5])
        # Back (+Y)
        self.add_quad(v[2], v[3], v[7], v[6])
        # Left (-X)
        self.add_quad(v[3], v[0], v[4], v[7])

    def add_tri_prism_z(self, p0, p1, p2, z0, z1):
        """
        Extrudes triangle (p0, p1, p2) in XY from z0 to z1.
        p0, p1, p2 must be in CCW order in XY plane.
        """
        v0 = (p0[0], p0[1], z0)
        v1 = (p1[0], p1[1], z0)
        v2 = (p2[0], p2[1], z0)
        v3 = (p0[0], p0[1], z1)
        v4 = (p1[0], p1[1], z1)
        v5 = (p2[0], p2[1], z1)

        # Bottom (-Z)
        self.add_triangle(v0, v2, v1)
        # Top (+Z)
        self.add_triangle(v3, v4, v5)
        # Side 0-1
        self.add_quad(v0, v1, v4, v3)
        # Side 1-2
        self.add_quad(v1, v2, v5, v4)
        # Side 2-0
        self.add_quad(v2, v0, v3, v5)

    def add_corner_arc(self, cx, cy, r_in, r_out, a_start, a_end, z0, z1, n_segs=8):
        """
        Creates a cylindrical arc corner block between r_in and r_out.
        """
        for i in range(n_segs):
            a0 = a_start + (a_end - a_start) * i / float(n_segs)
            a1 = a_start + (a_end - a_start) * (i + 1) / float(n_segs)

            # 4 vertices of the 2D radial segment in CCW order:
            p_in0  = (cx + r_in  * math.cos(a0), cy + r_in  * math.sin(a0))
            p_out0 = (cx + r_out * math.cos(a0), cy + r_out * math.sin(a0))
            p_out1 = (cx + r_out * math.cos(a1), cy + r_out * math.sin(a1))
            p_in1  = (cx + r_in  * math.cos(a1), cy + r_in  * math.sin(a1))

            # Split quad into 2 CCW triangles:
            self.add_tri_prism_z(p_in0, p_out0, p_out1, z0, z1)
            self.add_tri_prism_z(p_in0, p_out1, p_in1, z0, z1)

    def add_solid_corner(self, cx, cy, r, a_start, a_end, z0, z1, n_segs=8):
        """
        Creates a solid pie-sector corner block from center (cx, cy) to outer radius r.
        """
        p_center = (cx, cy)
        for i in range(n_segs):
            a0 = a_start + (a_end - a_start) * i / float(n_segs)
            a1 = a_start + (a_end - a_start) * (i + 1) / float(n_segs)
            p0 = (cx + r * math.cos(a0), cy + r * math.sin(a0))
            p1 = (cx + r * math.cos(a1), cy + r * math.sin(a1))
            self.add_tri_prism_z(p_center, p0, p1, z0, z1)

    def save_binary(self, filepath, name="P1M2_Dock"):
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
        print(f"Generated {filepath}: {len(self.triangles)} triangles")


def build_model():
    stl = STLWriter()

    # --------------------------------------------------------------------------
    # 1. Dimensions
    # --------------------------------------------------------------------------
    dev_w = 55.0
    dev_h = 85.0
    dev_d = 17.7

    fit_tol = 0.30
    cav_w = dev_w + 2 * fit_tol  # 55.6 mm
    cav_h = dev_h + 2 * fit_tol  # 85.6 mm
    cav_d = dev_d + fit_tol      # 18.0 mm
    r_in  = 4.5                  # Inner radius

    wall  = 2.4                  # 2.4 mm wall
    floor = 2.4                  # 2.4 mm floor

    out_w = cav_w + 2 * wall     # 60.4 mm
    out_h = cav_h + 2 * wall     # 90.4 mm
    out_d = floor + cav_d        # 20.4 mm
    r_out = r_in + wall          # 6.9 mm

    x_half_in  = cav_w / 2.0     # 27.8
    y_half_in  = cav_h / 2.0     # 42.8
    x_half_out = out_w / 2.0     # 30.2
    y_half_out = out_h / 2.0     # 45.2

    cx = x_half_out - r_out      # 23.3 (same as x_half_in - r_in)
    cy = y_half_out - r_out      # 38.3 (same as y_half_in - r_in)

    z0 = 0.0
    z_floor = floor              # 2.4 mm
    z_notch = 9.0                # 9.0 mm
    z_top = out_d                # 20.4 mm

    # Cutout specs
    top_cut_half_w = 11.0        # 22mm wide top cutout
    bot_cut_half_w = 19.0        # 38mm wide bottom cutout

    # Right FN cutout: 20 to 37 mm from cavity top inner edge (y_half_in = 42.8)
    y_fn_top = y_half_in - 20.0  # +22.8
    y_fn_bot = y_half_in - 37.0  # +5.8

    # Left finger notch: 20mm wide (centered at Y=0)
    y_ln_top = 10.0
    y_ln_bot = -10.0

    # --------------------------------------------------------------------------
    # 2. Flat Base Floor (Z from 0 to z_floor)
    # Completely solid rounded rectangle, flat on Z=0 for tape
    # --------------------------------------------------------------------------
    # Central rectangular body:
    stl.add_box(-cx, cx, -cy, cy, z0, z_floor)
    # 4 rectangular edge extensions:
    stl.add_box(-cx, cx, cy, y_half_out, z0, z_floor)          # Top edge
    stl.add_box(-cx, cx, -y_half_out, -cy, z0, z_floor)        # Bottom edge
    stl.add_box(-x_half_out, -cx, -cy, cy, z0, z_floor)        # Left edge
    stl.add_box(cx, x_half_out, -cy, cy, z0, z_floor)          # Right edge

    # 4 Solid Corner Sectors (Z from 0 to z_floor):
    stl.add_solid_corner(cx, cy, r_out, 0, math.pi/2, z0, z_floor, n_segs=8)         # TR
    stl.add_solid_corner(-cx, cy, r_out, math.pi/2, math.pi, z0, z_floor, n_segs=8)   # TL
    stl.add_solid_corner(-cx, -cy, r_out, math.pi, 3*math.pi/2, z0, z_floor, n_segs=8) # BL
    stl.add_solid_corner(cx, -cy, r_out, 3*math.pi/2, 2*math.pi, z0, z_floor, n_segs=8) # BR

    # --------------------------------------------------------------------------
    # 3. Four Corner Wall Arcs (Z from z_floor to z_top)
    # Concentric rounded corners between r_in and r_out
    # --------------------------------------------------------------------------
    stl.add_corner_arc(cx, cy, r_in, r_out, 0, math.pi/2, z_floor, z_top, n_segs=8)         # TR
    stl.add_corner_arc(-cx, cy, r_in, r_out, math.pi/2, math.pi, z_floor, z_top, n_segs=8)   # TL
    stl.add_corner_arc(-cx, -cy, r_in, r_out, math.pi, 3*math.pi/2, z_floor, z_top, n_segs=8) # BL
    stl.add_corner_arc(cx, -cy, r_in, r_out, 3*math.pi/2, 2*math.pi, z_floor, z_top, n_segs=8) # BR

    # --------------------------------------------------------------------------
    # 4. Straight Wall Sections (Z from z_floor to z_top)
    # --------------------------------------------------------------------------
    # A. Top Wall (Y in [y_half_in, y_half_out]):
    # Top cutout in X in [-top_cut_half_w, top_cut_half_w]
    stl.add_box(-cx, -top_cut_half_w, y_half_in, y_half_out, z_floor, z_top) # Left of cutout
    stl.add_box(top_cut_half_w, cx, y_half_in, y_half_out, z_floor, z_top)  # Right of cutout

    # B. Bottom Wall (Y in [-y_half_out, -y_half_in]):
    # Bottom cutout in X in [-bot_cut_half_w, bot_cut_half_w]
    stl.add_box(-cx, -bot_cut_half_w, -y_half_out, -y_half_in, z_floor, z_top) # Left of cutout
    stl.add_box(bot_cut_half_w, cx, -y_half_out, -y_half_in, z_floor, z_top)  # Right of cutout

    # C. Right Wall (X in [x_half_in, x_half_out]):
    # FN Cutout in Y in [y_fn_bot, y_fn_top]
    stl.add_box(x_half_in, x_half_out, y_fn_top, cy, z_floor, z_top)           # Top section (20mm bumper)
    stl.add_box(x_half_in, x_half_out, -cy, y_fn_bot, z_floor, z_top)          # Bottom section (continuous solid)

    # D. Left Wall (X in [-x_half_out, -x_half_in]):
    # Lower section (below finger notch: Z from z_floor to z_notch):
    stl.add_box(-x_half_out, -x_half_in, -cy, cy, z_floor, z_notch)
    # Upper section (above finger notch: Z from z_notch to z_top):
    stl.add_box(-x_half_out, -x_half_in, -cy, y_ln_bot, z_notch, z_top)        # Lower part of upper wall
    stl.add_box(-x_half_out, -x_half_in, y_ln_top, cy, z_notch, z_top)         # Upper part of upper wall

    return stl

if __name__ == '__main__':
    base_dir = "/Users/aubreyhan/Tools/git_repo/PersonalDev/3D"
    stl_path = os.path.join(base_dir, "Columbus_P1M2_Dock.stl")
    stl = build_model()
    stl.save_binary(stl_path, name="P1M2_Dock")

    # Run Bambu Studio verification if available
    bambu_cli = "/Applications/BambuStudio.app/Contents/MacOS/BambuStudio"
    if os.path.exists(bambu_cli):
        print("\nVerifying with BambuStudio CLI:")
        cmd = [bambu_cli, "--info", stl_path]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        print(res.stdout)
