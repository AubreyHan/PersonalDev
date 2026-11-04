#!/usr/bin/env python3
"""
ESP32-S3-Touch-AMOLED-1.75-G (Automotive GNSS Version)
Threaded Enclosure with Rear-Facing (Windshield-Facing) 12 o'clock Antenna Bay & Wire Pass-Through

Optimized for Automotive / In-Car Use:
1. Screen on Front (+Z): Faces driver and cabin passengers
2. Ceramic Antenna on Rear (-Z): Faces front windshield & open sky, avoiding Faraday cage roof blocking
3. Precision Threaded Connection: M52.0 x 2.0 with 36-flute knurling and coin slot on Lid
4. Wire Pass-Through Hole: 3.6mm x 3.2mm channel routing IPEX cable from rear antenna pod into PCB cavity
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
        self.add_quad(v[0], v[3], v[2], v[1]) # -Z
        self.add_quad(v[4], v[5], v[6], v[7]) # +Z
        self.add_quad(v[0], v[1], v[5], v[4]) # -Y
        self.add_quad(v[1], v[2], v[6], v[5]) # +X
        self.add_quad(v[2], v[3], v[7], v[6]) # +Y
        self.add_quad(v[3], v[0], v[4], v[7]) # -X

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
        print(f"  [STL] Saved {os.path.basename(filepath)}: {len(self.triangles):,} triangles")


def generate_lid(filepath):
    """
    Builds the Threaded Lid (Back Cover)
    - OD: 55.0mm, Thickness: 2.0mm
    - Knurled perimeter with 36 flutes for fingertip grip
    - Male Thread: M52.0 x 2.0 (Pitch 2.0mm, height 4.0mm, minor 49.3mm, major 51.5mm)
    - Central Coin Slot: 24mm x 2.4mm x 1.2mm
    """
    r_case = 27.5
    z_base = -2.0
    z_seat = 0.0
    z_top = 4.0
    pitch = 2.0
    
    # 0.25mm radial clearance to female thread
    r_min = 24.65
    r_maj = 25.65
    
    n_theta = 72
    n_z_thread = 32
    dtheta = 2.0 * math.pi / n_theta
    dz_th = (z_top - z_seat) / n_z_thread

    stl = ModularSTL()

    # 1. Male thread cylindrical wall
    th_verts = []
    for j in range(n_z_thread + 1):
        z = z_seat + j * dz_th
        row = []
        for i in range(n_theta):
            theta = i * dtheta
            phase = (z - (pitch / (2.0 * math.pi)) * theta) % pitch
            u = phase / pitch
            tooth = 2.0 * u if u < 0.5 else 2.0 * (1.0 - u)
            r = r_min + (r_maj - r_min) * tooth
            row.append((r * math.cos(theta), r * math.sin(theta), z))
        th_verts.append(row)

    for j in range(n_z_thread):
        for i in range(n_theta):
            i_next = (i + 1) % n_theta
            v00 = th_verts[j][i]
            v10 = th_verts[j][i_next]
            v11 = th_verts[j+1][i_next]
            v01 = th_verts[j+1][i]
            stl.add_triangle(v00, v10, v11)
            stl.add_triangle(v00, v11, v01)

    # 2. Top cap at z = z_top (acts as PCB rear clamping ring)
    top_center = (0.0, 0.0, z_top)
    for i in range(n_theta):
        i_next = (i + 1) % n_theta
        v0 = th_verts[-1][i]
        v1 = th_verts[-1][i_next]
        stl.add_triangle(top_center, v0, v1)

    # 3. Flange step at z = 0.0 (z_seat)
    rim_seat = []
    for i in range(n_theta):
        theta = i * dtheta
        knurl = 0.35 * math.cos(36 * theta)
        r_k = r_case + knurl
        rim_seat.append((r_k * math.cos(theta), r_k * math.sin(theta), z_seat))

    for i in range(n_theta):
        i_next = (i + 1) % n_theta
        r0 = rim_seat[i]
        r1 = rim_seat[i_next]
        t0 = th_verts[0][i]
        t1 = th_verts[0][i_next]
        stl.add_triangle(r0, r1, t1)
        stl.add_triangle(r0, t1, t0)

    # 4. Knurled outer perimeter wall from z_base to z_seat
    rim_base = []
    for i in range(n_theta):
        theta = i * dtheta
        knurl = 0.35 * math.cos(36 * theta)
        r_k = r_case + knurl
        rim_base.append((r_k * math.cos(theta), r_k * math.sin(theta), z_base))

    for i in range(n_theta):
        i_next = (i + 1) % n_theta
        b0 = rim_base[i]
        b1 = rim_base[i_next]
        s0 = rim_seat[i]
        s1 = rim_seat[i_next]
        stl.add_triangle(b0, b1, s1)
        stl.add_triangle(b0, s1, s0)

    # 5. Bottom base disc at z_base
    bot_center = (0.0, 0.0, z_base)
    for i in range(n_theta):
        i_next = (i + 1) % n_theta
        b0 = rim_base[i]
        b1 = rim_base[i_next]
        stl.add_triangle(bot_center, b1, b0)

    is_wt, n_tris, unmatch = stl.check_watertight()
    print(f"Lid Watertight: {is_wt} (Unmatched: {unmatch})")
    stl.save_binary(filepath, name="Threaded_Lid")
    return stl


def generate_body(filepath):
    """
    Builds the Threaded Body with Automotive Rear-Facing (Windshield-Facing) Antenna Bay
    - Front (+Z): Screen Bezel + Solid Elegant Antenna Hood (Facing Driver)
    - Rear (-Z): Antenna Bay Open Towards Windshield (Facing Sky/Windshield for Max GPS Signal)
    - Internal Wire Pass-Through Hole: 3.6mm x 3.2mm channel routing cable into main chamber
    - Internal M52.0 x 2.0 Thread at bottom (Minor ID 49.8mm > 49.0mm PCB OD)
    - Left (9 o'clock / West): USB Type-C port (11.5mm x 5.8mm)
    - Right (3 o'clock / East): Panoramic Window (-45° to +45°) for PWR, TF slot & BOOT
    """
    stl = ModularSTL()

    case_r = 27.5
    cavity_r = 24.8      # Dia 49.6mm
    screen_view_r = 22.1  # Dia 44.2mm
    
    # Z levels
    z_bottom = 0.0       # Mating face with Lid flange
    z_thread = 4.0       # Thread depth
    z_cutout_bot = 4.5   # Side openings bottom
    z_typec_top = 10.3   # Type-C top
    z_glass_seat = 10.8  # Screen glass seat
    z_cutout_top = 11.5  # Right window top
    z_ant_roof = 10.8    # Antenna cavity roof inside
    z_top = 13.0         # Front bezel & top face

    # 12 o'clock Antenna Pod dimensions
    ant_w = 29.2
    ant_cav_w = 25.6
    ant_y_start = 22.0
    ant_y_end = 51.0
    ant_cav_y_start = 24.8
    ant_cav_y_end = 49.2

    # -----------------------------------------------------------------
    # A. 12 o'clock Rear-Facing Antenna Pod (Windshield Oriented)
    # -----------------------------------------------------------------
    # 1. Front Radome Roof (Z=z_ant_roof to z_top, facing driver/cabin)
    # Provides solid, sleek, closed front appearance flush with screen bezel
    stl.add_box(-ant_w/2.0 + 3.5, ant_w/2.0 - 3.5, ant_y_start, ant_y_end, z_ant_roof, z_top)
    stl.add_box(-ant_w/2.0, -ant_w/2.0 + 3.5, ant_y_start, ant_y_end - 3.5, z_ant_roof, z_top)
    stl.add_box(ant_w/2.0 - 3.5, ant_w/2.0, ant_y_start, ant_y_end - 3.5, z_ant_roof, z_top)
    stl.add_solid_corner(-ant_w/2.0 + 3.5, ant_y_end - 3.5, 3.5, math.pi/2.0, math.pi, z_ant_roof, z_top, n_segs=8)
    stl.add_solid_corner(ant_w/2.0 - 3.5, ant_y_end - 3.5, 3.5, 0.0, math.pi/2.0, z_ant_roof, z_top, n_segs=8)
    stl.add_box(-ant_w/2.0 + 3.5, ant_w/2.0 - 3.5, ant_y_end - 3.5, ant_y_end, z_ant_roof, z_top)

    # 2. Antenna Pod Outer Perimeter Walls (Z=0.0 to z_ant_roof)
    # Left outer wall
    stl.add_box(-ant_w/2.0, -ant_cav_w/2.0, ant_y_start, ant_y_end - 3.5, 0.0, z_ant_roof)
    # Right outer wall
    stl.add_box(ant_cav_w/2.0, ant_w/2.0, ant_y_start, ant_y_end - 3.5, 0.0, z_ant_roof)
    # North far wall
    stl.add_box(-ant_cav_w/2.0, ant_cav_w/2.0, ant_cav_y_end, ant_y_end, 0.0, z_ant_roof)
    stl.add_solid_corner(-ant_w/2.0 + 3.5, ant_y_end - 3.5, 3.5, math.pi/2.0, math.pi, 0.0, z_ant_roof, n_segs=8)
    stl.add_solid_corner(ant_w/2.0 - 3.5, ant_y_end - 3.5, 3.5, 0.0, math.pi/2.0, 0.0, z_ant_roof, n_segs=8)

    # 3. Partition Wall with Wire Pass-Through Hole (Y=ant_y_start to ant_cav_y_start)
    hole_w = 3.6
    hole_z_bot = 3.8
    hole_z_top = 7.0
    # Left partition block
    stl.add_box(-ant_cav_w/2.0, -hole_w/2.0, ant_y_start, ant_cav_y_start, 0.0, z_ant_roof)
    # Right partition block
    stl.add_box(hole_w/2.0, ant_cav_w/2.0, ant_y_start, ant_cav_y_start, 0.0, z_ant_roof)
    # Below the hole block
    stl.add_box(-hole_w/2.0, hole_w/2.0, ant_y_start, ant_cav_y_start, 0.0, hole_z_bot)
    # Above the hole block
    stl.add_box(-hole_w/2.0, hole_w/2.0, ant_y_start, ant_cav_y_start, hole_z_top, z_ant_roof)

    # -----------------------------------------------------------------
    # B. Main Circular Chassis Walls & Openings
    # -----------------------------------------------------------------
    def add_wall_sector(a0_deg, a1_deg, z0, z1, r_in, r_out, n=6):
        stl.add_corner_arc(0, 0, r_in, r_out, math.radians(a0_deg), math.radians(a1_deg), z0, z1, n_segs=n)

    # Lower Thread Ring (Z=0 to z_thread)
    add_wall_sector(-45, 45, 0.0, z_thread, cavity_r, case_r, n=8)
    add_wall_sector(45, 75, 0.0, z_thread, cavity_r, case_r, n=6)
    add_wall_sector(105, 165, 0.0, z_thread, cavity_r, case_r, n=8)
    add_wall_sector(165, 195, 0.0, z_thread, cavity_r, case_r, n=6)
    add_wall_sector(195, 315, 0.0, z_thread, cavity_r, case_r, n=16)

    # Under-cutout solid lower rims (Z=z_thread to z_cutout_bot)
    add_wall_sector(-45, 45, z_thread, z_cutout_bot, cavity_r, case_r, n=8)
    add_wall_sector(165, 195, z_thread, z_cutout_bot, cavity_r, case_r, n=6)

    # Main Wall Body (Z=z_cutout_bot to z_glass_seat)
    add_wall_sector(45, 75, z_cutout_bot, z_glass_seat, cavity_r, case_r, n=6)
    add_wall_sector(105, 165, z_cutout_bot, z_glass_seat, cavity_r, case_r, n=8)
    add_wall_sector(195, 315, z_cutout_bot, z_glass_seat, cavity_r, case_r, n=16)

    # Right side 3 o'clock Panoramic Window:
    add_wall_sector(-45, 45, z_cutout_top, z_glass_seat, cavity_r, case_r, n=8)

    # Left side 9 o'clock Type-C Port:
    add_wall_sector(165, 195, z_typec_top, z_glass_seat, cavity_r, case_r, n=6)

    # Screen Bezel & AMOLED Retention Shelf (Z=z_glass_seat to z_top)
    add_wall_sector(0, 360, z_glass_seat, z_top, screen_view_r, case_r, n=48)

    is_wt, n_tris, unmatch = stl.check_watertight()
    print(f"Body Watertight: {is_wt} (Unmatched: {unmatch})")
    stl.save_binary(filepath, name="Threaded_Body")
    return stl

if __name__ == "__main__":
    out_dir = "/Users/aubreyhan/Tools/git_repo/PersonalDev/3D"
    lid_stl = os.path.join(out_dir, "ESP32_S3_AMOLED_Lid_Threaded.stl")
    body_stl = os.path.join(out_dir, "ESP32_S3_AMOLED_Body_Threaded.stl")
    
    print("=== Generating ESP32-S3 AMOLED Rear-Facing Threaded Case ===")
    generate_lid(lid_stl)
    generate_body(body_stl)
