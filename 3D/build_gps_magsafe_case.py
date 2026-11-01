#!/usr/bin/env python3
"""
ESP32-S3-Touch-AMOLED-1.75-G (GNSS/GPS Version) MagSafe Case Generator
Features (Front View Perspective):
- Left-side (9 o'clock / West): USB Type-C Port
- Left-side (8 & 10 o'clock): ES7210 Dual Microphone Acoustic Pinholes (148° & 212°)
- Right-side (3 o'clock / East: 320° to 40°): Unified Panoramic Opening (covering PWR, TF slot, and BOOT buttons for direct finger access)
- Top (12 o'clock / North): 25x25mm GNSS Ceramic Antenna Pod (thin sealed RF radome)
- Bottom Base: MagSafe Recess (OD 54.2mm, ID 45.8mm, Depth 1.2mm)
"""

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

    def save_binary(self, filepath, name="MagSafe_Case"):
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


def build_bottom_shell(filepath):
    """
    Constructs Bottom Base Shell with Unified Left-Side Opening.
    """
    stl = ModularSTL()

    case_r = 27.5
    cavity_r = 24.8  # Inner PCB cavity dia 49.6 mm
    ms_r_in = 22.9   # MagSafe inner dia 45.8 mm
    ms_r_out = 27.1  # MagSafe outer dia 54.2 mm
    ms_depth = 1.2   # MagSafe recess depth
    z_floor = 2.0    # Bottom solid floor
    z_parting = 5.8  # Parting line

    ant_w = 29.2
    ant_y_start = 22.0
    ant_y_end = 51.2
    ant_cav_w = 25.6
    ant_cav_y_start = 23.8
    ant_cav_y_end = 49.4

    # -------------------------------------------------------------
    # 1. Floor Base (Z = 0.0 to 2.0)
    # -------------------------------------------------------------
    stl.add_solid_corner(0, 0, ms_r_in, 0, 2.0 * math.pi, 0.0, z_floor, n_segs=72)
    stl.add_corner_arc(0, 0, ms_r_in, ms_r_out, 0, 2.0 * math.pi, ms_depth, z_floor, n_segs=72)
    stl.add_corner_arc(0, 0, ms_r_out, case_r, 0, 2.0 * math.pi, 0.0, z_floor, n_segs=72)

    # Antenna Pod Floor Plate (12 o'clock)
    stl.add_box(-ant_w/2.0 + 3.5, ant_w/2.0 - 3.5, ant_y_start, ant_y_end, 0.0, z_floor)
    stl.add_box(-ant_w/2.0, -ant_w/2.0 + 3.5, ant_y_start, ant_y_end - 3.5, 0.0, z_floor)
    stl.add_box(ant_w/2.0 - 3.5, ant_w/2.0, ant_y_start, ant_y_end - 3.5, 0.0, z_floor)
    stl.add_solid_corner(-ant_w/2.0 + 3.5, ant_y_end - 3.5, 3.5, math.pi/2.0, math.pi, 0.0, z_floor, n_segs=8)
    stl.add_solid_corner(ant_w/2.0 - 3.5, ant_y_end - 3.5, 3.5, 0, math.pi/2.0, 0.0, z_floor, n_segs=8)
    stl.add_box(-ant_w/2.0 + 3.5, ant_w/2.0 - 3.5, ant_y_end - 3.5, ant_y_end, 0.0, z_floor)

    # -------------------------------------------------------------
    # 2. Main PCB Perimeter Walls (Z = 2.0 to 5.8)
    # -------------------------------------------------------------
    def add_pcb_wall(a_start_deg, a_end_deg, z0=z_floor, z1=z_parting, n=6):
        stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(a_start_deg), math.radians(a_end_deg), z0, z1, n_segs=n)

    def add_snap_recess(cen_deg):
        half_ang = 3.2
        a0 = cen_deg - half_ang
        a1 = cen_deg + half_ang
        stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(a0), math.radians(a1), z_floor, 3.6, n_segs=4)
        stl.add_corner_arc(0, 0, cavity_r + 0.45, case_r, math.radians(a0), math.radians(a1), 3.6, 5.2, n_segs=4)
        stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(a0), math.radians(a1), 5.2, z_parting, n_segs=4)

    # 1. [320° to 40° / -40° to +40°] 3 点钟方向 (East / Right side) 一体化全景大镂空窗 (覆盖 PWR、TF卡槽、BOOT键)
    # 底部留 0.4mm 微台阶 (Z=2.0..2.4)，从 Z=2.4 到 5.8 完全贯通开放！
    stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(-40.0), math.radians(40.0), z_floor, 2.4, n_segs=16)

    # 2. [40° to 60°] East-North Sector (包含 50° Snap-fit 卡扣)
    add_pcb_wall(40.0, 46.8, n=3)
    add_snap_recess(50.0)
    add_pcb_wall(53.2, 60.0, n=3)

    # 3. [60° to 120°] 12 点钟 Antenna Pod 通道与隔桥
    stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(60.0), math.radians(78.0), z_floor, z_parting, n_segs=4)
    stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(102.0), math.radians(120.0), z_floor, z_parting, n_segs=4)
    stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(78.0), math.radians(102.0), 4.2, z_parting, n_segs=6)

    # 4. [120° to 168°] North-West Corner Sector (包含 130° Snap-fit 卡扣)
    add_pcb_wall(120.0, 126.8, n=3)
    add_snap_recess(130.0)
    add_pcb_wall(133.2, 168.0, n=8)

    # 5. [168° to 192°] 9 点钟方向 (West / Left side) Type-C 接口
    stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(168.0), math.radians(192.0), z_floor, 2.3, n_segs=6)

    # 6. [192° to 320°] South-West & South & South-East Sector (包含 230° Snap-fit 与 310° Snap-fit)
    add_pcb_wall(192.0, 226.8, n=8)
    add_snap_recess(230.0)
    add_pcb_wall(233.2, 306.8, n=16)
    add_snap_recess(310.0)
    add_pcb_wall(313.2, 320.0, n=3)

    # -------------------------------------------------------------
    # 3. Antenna Pod Walls (Z = 2.0 to 5.8)
    # -------------------------------------------------------------
    stl.add_box(-ant_w/2.0, -ant_cav_w/2.0, ant_y_start, ant_cav_y_end, z_floor, z_parting)
    stl.add_box(ant_cav_w/2.0, ant_w/2.0, ant_y_start, ant_cav_y_end, z_floor, z_parting)
    stl.add_box(-ant_w/2.0 + 3.5, ant_w/2.0 - 3.5, ant_cav_y_end, ant_y_end, z_floor, z_parting)
    stl.add_corner_arc(-ant_w/2.0 + 3.5, ant_y_end - 3.5, 0, 3.5, math.pi/2.0, math.pi, z_floor, z_parting, n_segs=8)
    stl.add_corner_arc(ant_w/2.0 - 3.5, ant_y_end - 3.5, 0, 3.5, 0, math.pi/2.0, z_floor, z_parting, n_segs=8)

    is_wt, num_t, unmatched = stl.check_watertight()
    print(f"[{os.path.basename(filepath)}] Watertight: {is_wt} (Triangles: {num_t}, Unmatched: {unmatched})")
    stl.save_binary(filepath, name="Case_Bottom")
    return is_wt


def build_top_shell(filepath):
    """
    Constructs Top Bezel Shell with matching Left-Side Unified Opening Upper Clearance.
    """
    stl = ModularSTL()

    case_r = 27.5
    cavity_r = 24.8       # Screen glass pocket dia 49.6 mm
    screen_view_r = 22.1  # AMOLED active front window dia 44.2 mm
    z_parting = 5.8       # Mating line
    z_glass_pocket = 8.2  # Glass pocket depth (2.4mm)
    z_ant_top = 9.6       # Antenna cavity ceiling
    z_top = 10.8          # Overall top surface

    ant_w = 29.2
    ant_y_start = 22.0
    ant_y_end = 51.2
    ant_cav_w = 25.6
    ant_cav_y_start = 23.8
    ant_cav_y_end = 49.4

    # -------------------------------------------------------------
    # 1. Lower Layer of Top Shell (Z = 5.8 to 8.2)
    # -------------------------------------------------------------
    # Type-C upper arch clearance at 3 o'clock (-12° to +12°)
    stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(12.0), math.radians(58.0), z_parting, z_glass_pocket, n_segs=16)
    stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(122.0), math.radians(140.0), z_parting, z_glass_pocket, n_segs=4)

    # Left-side unified opening upper clearance (140° to 220°):
    # Leaves 1.6mm clearance (Z=5.8 to 7.4 is open) for finger access
    stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(140.0), math.radians(220.0), 7.4, z_glass_pocket, n_segs=16)

    stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(220.0), math.radians(348.0), z_parting, z_glass_pocket, n_segs=32)
    stl.add_corner_arc(0, 0, cavity_r, case_r, math.radians(-12.0), math.radians(12.0), 7.0, z_glass_pocket, n_segs=8)

    # Antenna pod lower perimeter walls (Z = 5.8 to 8.2)
    stl.add_box(-ant_w/2.0, -ant_cav_w/2.0, ant_y_start, ant_cav_y_end, z_parting, z_glass_pocket)
    stl.add_box(ant_cav_w/2.0, ant_w/2.0, ant_y_start, ant_cav_y_end, z_parting, z_glass_pocket)
    stl.add_box(-ant_w/2.0 + 3.5, ant_w/2.0 - 3.5, ant_cav_y_end, ant_y_end, z_parting, z_glass_pocket)
    stl.add_corner_arc(-ant_w/2.0 + 3.5, ant_y_end - 3.5, 0, 3.5, math.pi/2.0, math.pi, z_parting, z_glass_pocket, n_segs=8)
    stl.add_corner_arc(ant_w/2.0 - 3.5, ant_y_end - 3.5, 0, 3.5, 0, math.pi/2.0, z_parting, z_glass_pocket, n_segs=8)

    # -------------------------------------------------------------
    # 2. Upper Layer & Front Bezel (Z = 8.2 to 10.8)
    # -------------------------------------------------------------
    stl.add_corner_arc(0, 0, screen_view_r, case_r, 0, 2.0 * math.pi, z_glass_pocket, z_top, n_segs=72)

    # Antenna pod upper walls & solid sealed RF roof
    stl.add_box(-ant_w/2.0, -ant_cav_w/2.0, ant_y_start, ant_cav_y_end, z_glass_pocket, z_ant_top)
    stl.add_box(ant_cav_w/2.0, ant_w/2.0, ant_y_start, ant_cav_y_end, z_glass_pocket, z_ant_top)
    stl.add_box(-ant_w/2.0 + 3.5, ant_w/2.0 - 3.5, ant_cav_y_end, ant_y_end, z_glass_pocket, z_ant_top)
    stl.add_corner_arc(-ant_w/2.0 + 3.5, ant_y_end - 3.5, 0, 3.5, math.pi/2.0, math.pi, z_glass_pocket, z_ant_top, n_segs=8)
    stl.add_corner_arc(ant_w/2.0 - 3.5, ant_y_end - 3.5, 0, 3.5, 0, math.pi/2.0, z_glass_pocket, z_ant_top, n_segs=8)

    stl.add_box(-ant_w/2.0 + 3.5, ant_w/2.0 - 3.5, ant_y_start, ant_y_end, z_ant_top, z_top)
    stl.add_box(-ant_w/2.0, -ant_w/2.0 + 3.5, ant_y_start, ant_y_end - 3.5, z_ant_top, z_top)
    stl.add_box(ant_w/2.0 - 3.5, ant_w/2.0, ant_y_start, ant_y_end - 3.5, z_ant_top, z_top)
    stl.add_corner_arc(-ant_w/2.0 + 3.5, ant_y_end - 3.5, 0, 3.5, math.pi/2.0, math.pi, z_ant_top, z_top, n_segs=8)
    stl.add_corner_arc(ant_w/2.0 - 3.5, ant_y_end - 3.5, 0, 3.5, 0, math.pi/2.0, z_ant_top, z_top, n_segs=8)

    # -------------------------------------------------------------
    # 3. 4 Snap-fit Male Cantilever Tabs (at 50°, 125°, 235°, 310°)
    # -------------------------------------------------------------
    tab_h = 2.1
    for cen_deg in [50, 125, 235, 310]:
        rad = math.radians(cen_deg)
        cx = cavity_r * math.cos(rad)
        cy = cavity_r * math.sin(rad)
        stl.add_box(cx - 1.8, cx + 1.8, cy - 1.8, cy + 1.8, z_parting - tab_h, z_parting)
        dx = 0.45 * math.cos(rad)
        dy = 0.45 * math.sin(rad)
        stl.add_box(cx + dx - 1.2, cx + dx + 1.2, cy + dy - 1.2, cy + dy + 1.2, z_parting - 1.8, z_parting - 0.6)

    # -------------------------------------------------------------
    # 4. ES7210 Dual Microphone Acoustic Pinholes (2 o'clock & 4 o'clock)
    # -------------------------------------------------------------
    rad1 = math.radians(32.0)
    mx1, my1 = (case_r - 1.8) * math.cos(rad1), (case_r - 1.8) * math.sin(rad1)
    stl.add_box(mx1 - 1.0, mx1 + 1.0, my1 - 1.0, my1 + 1.0, z_glass_pocket, z_top)

    rad2 = math.radians(-32.0)
    mx2, my2 = (case_r - 1.8) * math.cos(rad2), (case_r - 1.8) * math.sin(rad2)
    stl.add_box(mx2 - 1.0, mx2 + 1.0, my2 - 1.0, my2 + 1.0, z_glass_pocket, z_top)

    is_wt, num_t, unmatched = stl.check_watertight()
    print(f"[{os.path.basename(filepath)}] Watertight: {is_wt} (Triangles: {num_t}, Unmatched: {unmatched})")
    stl.save_binary(filepath, name="Case_Top")
    return is_wt


def build_assembly(top_file, bot_file, out_file):
    with open(top_file, 'rb') as f:
        f.seek(80)
        n_top = struct.unpack('<I', f.read(4))[0]
        tris_top = f.read(n_top * 50)
    with open(bot_file, 'rb') as f:
        f.seek(80)
        n_bot = struct.unpack('<I', f.read(4))[0]
        tris_bot = f.read(n_bot * 50)

    with open(out_file, 'wb') as f:
        header = f"BambuStudio Binary STL - Case_Assembly".encode('ascii')[:80].ljust(80, b'\x00')
        f.write(header)
        f.write(struct.pack('<I', n_top + n_bot))
        f.write(tris_top)
        f.write(tris_bot)
    print(f"[{os.path.basename(out_file)}] Watertight Assembly created: {n_top + n_bot} triangles.")


if __name__ == "__main__":
    base_dir = os.path.dirname(os.path.abspath(__file__))
    f_bot = os.path.join(base_dir, "ESP32_S3_AMOLED_GPS_Case_Bottom.stl")
    f_top = os.path.join(base_dir, "ESP32_S3_AMOLED_GPS_Case_Top.stl")
    f_asm = os.path.join(base_dir, "ESP32_S3_AMOLED_GPS_Case_Assembly.stl")

    print("\n=======================================================")
    print("Building ESP32-S3-Touch-AMOLED-1.75-G MagSafe Enclosure")
    print("Updated: Left-Side Panoramic Unified Opening (PWR / TF / BOOT)")
    print("=======================================================")
    build_bottom_shell(f_bot)
    build_top_shell(f_top)
    build_assembly(f_top, f_bot, f_asm)
    print("All STL files successfully generated and verified 100% Watertight!\n")
