#!/usr/bin/env python3
"""
High-Precision Parametric 3D Mesh, CAD Render & Peak-Optimized Bambu Lab X1C 3MF Generator
for 16-Sector Circle Model (Ø120mm × 6mm, 16 Equal Detachable Sectors).

Target Hardware & Material Configuration:
- Printer:  Bambu Lab X1 Carbon (0.4 mm Hardened Steel Nozzle)
- Filament: Bambu PETG-HF (@BBL X1C)
- Plate:    Textured PEI Plate (70°C Bed / 245°C Nozzle / Aux Fan 0%)
"""

import os
import sys
import math
import struct
import json
import zipfile
import shutil
import subprocess
import re

BASE_DIR = "/Users/aubreyhan/Tools/git_repo/PersonalDev/3D"
OUT_DIR = os.path.join(BASE_DIR, "output")
os.makedirs(OUT_DIR, exist_ok=True)
BAMBU_CLI = "/Applications/BambuStudio.app/Contents/MacOS/BambuStudio"
BBL_PROFILE_DIR = "/Applications/BambuStudio.app/Contents/Resources/profiles/BBL"

# ============================================================================
# 1. Exact Parametric Geometry for a Single 1/16 Sector (22.5°)
# ============================================================================
DIAMETER = 120.0
RADIUS = DIAMETER / 2.0                 # 60.0 mm
THICKNESS = 6.0                         # 6.0 mm (Exact 30 layers @ 0.20mm)
NUM_SECTORS = 16
SECTOR_ANGLE_DEG = 360.0 / NUM_SECTORS  # 22.5°
SECTOR_ANGLE_RAD = math.radians(SECTOR_ANGLE_DEG)
HALF_ANGLE = SECTOR_ANGLE_RAD / 2.0     # 11.25°

SIDE_CLEARANCE = 0.10                   # 0.10 mm per side (0.20 mm total gap between adjacent sectors)
TIP_FILLET_R = 0.80                     # R0.80 mm tangent rounded tip to eliminate 22.5° acute tip over-extrusion
EDGE_CHAMFER = 0.40                     # 0.40 mm (exact 2 layers @ 0.20mm) 45° chamfer on top & bottom edges


def load_flattened_bambu_profile(subdir, name_or_filename):
    """
    Recursively resolves the `inherits` chain in Bambu Studio's official BBL profiles
    so that CLI `--load-settings` and `--load-filaments` receive 100% complete parameters
    (X1C 10,000 mm/s² acceleration, PETG-HF 1.27 g/cm³ density, 21 mm³/s max volumetric speed, etc.).
    """
    folder = os.path.join(BBL_PROFILE_DIR, subdir)
    fname = name_or_filename if name_or_filename.endswith(".json") else f"{name_or_filename}.json"
    path = os.path.join(folder, fname)
    if not os.path.exists(path):
        # Search recursively inside BBL_PROFILE_DIR
        for root, _, files in os.walk(BBL_PROFILE_DIR):
            if fname in files:
                path = os.path.join(root, fname)
                break
    if not os.path.exists(path):
        return {}

    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    parent_name = data.get("inherits")
    if parent_name:
        parent_data = load_flattened_bambu_profile(subdir, parent_name)
        merged = dict(parent_data)
        merged.update(data)
        return merged
    return data


def generate_sector_polygon_2d(inset=0.0, arc_steps=36, tip_steps=14):
    """
    Computes a counter-clockwise 2D polygon for one 22.5° sector centered along +X axis
    (spanning [-HALF_ANGLE, +HALF_ANGLE]), inset inward by (SIDE_CLEARANCE + inset).
    Includes a smooth tangent rounded tip of radius TIP_FILLET_R.
    """
    d = SIDE_CLEARANCE + inset
    r_out = RADIUS - inset
    r_tip = max(0.3, TIP_FILLET_R - inset * 0.5)

    sin_h = math.sin(HALF_ANGLE)
    cx_tip = (d + r_tip) / sin_h

    pts = []

    # 1. Smooth tangent tip arc from top ray tangent (+pi/2 + HALF_ANGLE) to bottom ray tangent (3*pi/2 - HALF_ANGLE)
    ang_start = math.pi / 2.0 + HALF_ANGLE
    ang_end = 1.5 * math.pi - HALF_ANGLE
    for i in range(tip_steps + 1):
        t = i / tip_steps
        ang = ang_start + t * (ang_end - ang_start)
        px = cx_tip + r_tip * math.cos(ang)
        py = r_tip * math.sin(ang)
        pts.append((px, py))

    # 2. Outer circular arc from bottom corner to top corner
    ang_margin = math.asin(d / r_out)
    arc_ang_start = -HALF_ANGLE + ang_margin
    arc_ang_end = +HALF_ANGLE - ang_margin

    for i in range(arc_steps + 1):
        t = i / arc_steps
        ang = arc_ang_start + t * (arc_ang_end - arc_ang_start)
        px = r_out * math.cos(ang)
        py = r_out * math.sin(ang)
        pts.append((px, py))

    return pts


def compute_normal(v0, v1, v2):
    ux, uy, uz = v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]
    vx, vy, vz = v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]
    nx = uy * vz - uz * vy
    ny = uz * vx - ux * vz
    nz = ux * vy - uy * vx
    nl = math.hypot(nx, ny, nz)
    if nl > 1e-12:
        return (nx / nl, ny / nl, nz / nl)
    return (0.0, 0.0, 1.0)


def build_single_sector_triangles():
    """
    Constructs a watertight manifold triangle mesh for a single 3D sector
    spanning Z in [0, THICKNESS] with 45° top & bottom chamfers.
    """
    z_levels = [
        (0.0, EDGE_CHAMFER),
        (EDGE_CHAMFER, 0.0),
        (THICKNESS - EDGE_CHAMFER, 0.0),
        (THICKNESS, EDGE_CHAMFER)
    ]

    rings = []
    for z, inset in z_levels:
        pts_2d = generate_sector_polygon_2d(inset=inset, arc_steps=36, tip_steps=14)
        rings.append([(x, y, z) for (x, y) in pts_2d])

    n_pts = len(rings[0])
    triangles = []

    # 1. Side walls between consecutive Z rings
    for r_idx in range(len(rings) - 1):
        ring_a = rings[r_idx]
        ring_b = rings[r_idx + 1]
        for i in range(n_pts):
            j = (i + 1) % n_pts
            p00 = ring_a[i]
            p10 = ring_a[j]
            p11 = ring_b[j]
            p01 = ring_b[i]
            triangles.append((compute_normal(p00, p10, p11), p00, p10, p11))
            triangles.append((compute_normal(p00, p11, p01), p00, p11, p01))

    # 2. Bottom cap (z = 0, normal = (0, 0, -1))
    bot_ring = rings[0]
    cx_bot = sum(p[0] for p in bot_ring) / n_pts
    cy_bot = sum(p[1] for p in bot_ring) / n_pts
    c_bot = (cx_bot, cy_bot, 0.0)
    for i in range(n_pts):
        j = (i + 1) % n_pts
        p0 = bot_ring[j]
        p1 = bot_ring[i]
        triangles.append(((0.0, 0.0, -1.0), c_bot, p0, p1))

    # 3. Top cap (z = THICKNESS, normal = (0, 0, +1))
    top_ring = rings[-1]
    cx_top = sum(p[0] for p in top_ring) / n_pts
    cy_top = sum(p[1] for p in top_ring) / n_pts
    c_top = (cx_top, cy_top, THICKNESS)
    for i in range(n_pts):
        j = (i + 1) % n_pts
        p0 = top_ring[i]
        p1 = top_ring[j]
        triangles.append(((0.0, 0.0, 1.0), c_top, p0, p1))

    return triangles


def transform_triangles(tris, rot_deg=0.0, tx=0.0, ty=0.0, tz=0.0):
    rad = math.radians(rot_deg)
    ca, sa = math.cos(rad), math.sin(rad)

    def tf_pt(v):
        x = v[0] * ca - v[1] * sa + tx
        y = v[0] * sa + v[1] * ca + ty
        z = v[2] + tz
        return (x, y, z)

    def tf_norm(n):
        nx = n[0] * ca - n[1] * sa
        ny = n[0] * sa + n[1] * ca
        nz = n[2]
        return (nx, ny, nz)

    out = []
    for n, v0, v1, v2 in tris:
        out.append((tf_norm(n), tf_pt(v0), tf_pt(v1), tf_pt(v2)))
    return out


def write_binary_stl(path, triangles, header_text="16-Sector Circle CAD Model"):
    hdr = header_text.encode('ascii', errors='ignore')[:80].ljust(80, b'\x00')
    with open(path, 'wb') as f:
        f.write(hdr)
        f.write(struct.pack('<I', len(triangles)))
        for n, v0, v1, v2 in triangles:
            f.write(struct.pack(
                '<12fH',
                n[0], n[1], n[2],
                v0[0], v0[1], v0[2],
                v1[0], v1[1], v1[2],
                v2[0], v2[1], v2[2],
                0
            ))
    print(f"Saved binary STL: {path} ({len(triangles):,} triangles)")


# ============================================================================
# 2. CAD Multi-View Renderer (1:1 True Proportions)
# ============================================================================
def render_cad_preview(single_tris, out_png):
    width, height = 1800, 1100

    def project(v, az_deg, el_deg):
        az = math.radians(az_deg)
        el = math.radians(el_deg)
        cos_az, sin_az = math.cos(az), math.sin(az)
        x1 = v[0] * cos_az - v[1] * sin_az
        y1 = v[0] * sin_az + v[1] * cos_az
        z1 = v[2]
        tilt = math.pi / 2.0 - el
        cos_t, sin_t = math.cos(tilt), math.sin(tilt)
        x2 = x1
        y2 = y1 * cos_t - z1 * sin_t
        z2 = y1 * sin_t + z1 * cos_t
        return x2, y2, z2

    l1 = [0.45, 0.35, 0.82]
    l1_len = math.hypot(*l1)
    l1 = [x / l1_len for x in l1]
    l2 = [-0.55, -0.35, 0.45]
    l2_len = math.hypot(*l2)
    l2 = [x / l2_len for x in l2]

    def build_viewport_polygons(objects, az_deg, el_deg, vp_cx, vp_cy, scale):
        proj = []
        for tris, color_hex in objects:
            cr = int(color_hex[1:3], 16)
            cg = int(color_hex[3:5], 16)
            cb = int(color_hex[5:7], 16)
            for n, v0, v1, v2 in tris:
                p0 = project(v0, az_deg, el_deg)
                p1 = project(v1, az_deg, el_deg)
                p2 = project(v2, az_deg, el_deg)
                cp = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0])
                if cp <= 0:
                    continue
                avg_z = (p0[2] + p1[2] + p2[2]) / 3.0
                nx, ny, nz = n
                dot1 = max(0.0, nx * l1[0] + ny * l1[1] + nz * l1[2])
                dot2 = max(0.0, nx * l2[0] + ny * l2[1] + nz * l2[2])
                intensity = 0.32 + 0.54 * dot1 + 0.14 * dot2
                proj.append((avg_z, p0, p1, p2, intensity, (cr, cg, cb)))

        proj.sort(key=lambda x: x[0])
        polys_svg = []
        for avg_z, p0, p1, p2, it, (cr, cg, cb) in proj:
            sx0 = vp_cx + p0[0] * scale
            sy0 = vp_cy - p0[1] * scale
            sx1 = vp_cx + p1[0] * scale
            sy1 = vp_cy - p1[1] * scale
            sx2 = vp_cx + p2[0] * scale
            sy2 = vp_cy - p2[1] * scale
            r_c = int(min(255, cr * it))
            g_c = int(min(255, cg * it))
            b_c = int(min(255, cb * it))
            er = int(r_c * 0.65)
            eg = int(g_c * 0.65)
            eb = int(b_c * 0.65)
            polys_svg.append(
                f'<polygon points="{sx0:.1f},{sy0:.1f} {sx1:.1f},{sy1:.1f} {sx2:.1f},{sy2:.1f}" '
                f'fill="#{r_c:02x}{g_c:02x}{b_c:02x}" stroke="#{er:02x}{eg:02x}{eb:02x}" stroke-width="0.35"/>'
            )
        return polys_svg

    # Viewport 1 (Left): Assembled 16-Sector Circle Isometric View
    vp1_objs = []
    for i in range(NUM_SECTORS):
        ang = i * SECTOR_ANGLE_DEG
        rad = math.radians(ang)
        r_off = 0.6
        tris_i = transform_triangles(single_tris, rot_deg=ang, tx=r_off * math.cos(rad), ty=r_off * math.sin(rad))
        color = "#38bdf8" if (i % 2 == 0) else "#f59e0b"
        vp1_objs.append((tris_i, color))

    # Viewport 2 (Top Right): Top-Down Orthographic Print Plate Layout (3mm radial separation)
    vp2_objs = []
    for i in range(NUM_SECTORS):
        ang = i * SECTOR_ANGLE_DEG
        rad = math.radians(ang)
        r_off = 3.0
        tris_i = transform_triangles(single_tris, rot_deg=ang, tx=r_off * math.cos(rad), ty=r_off * math.sin(rad))
        color = "#38bdf8" if (i % 2 == 0) else "#f59e0b"
        vp2_objs.append((tris_i, color))

    # Viewport 3 (Bottom Right): Parallelogram Area Derivation (8 bottom + 8 top interleaved)
    vp3_objs = []
    arc_step = 2.0 * RADIUS * math.sin(HALF_ANGLE) + 0.6
    y_height = RADIUS * math.cos(HALF_ANGLE)
    for i in range(8):
        bx = (i - 3.75) * arc_step
        by = -y_height / 2.0
        tris_b = transform_triangles(single_tris, rot_deg=90.0, tx=bx, ty=by)
        vp3_objs.append((tris_b, "#38bdf8"))
        tx = (i - 3.25) * arc_step
        ty = +y_height / 2.0
        tris_t = transform_triangles(single_tris, rot_deg=-90.0, tx=tx, ty=ty)
        vp3_objs.append((tris_t, "#f59e0b"))

    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" height="{height}">']
    svg.append('<defs>')
    svg.append('  <radialGradient id="bg" cx="50%" cy="50%" r="75%">')
    svg.append('    <stop offset="0%" stop-color="#1e293b"/>')
    svg.append('    <stop offset="100%" stop-color="#090d16"/>')
    svg.append('  </radialGradient>')
    svg.append('</defs>')
    svg.append('<rect width="100%" height="100%" fill="url(#bg)"/>')

    # Grid lines
    for gx in range(0, width, 80):
        svg.append(f'<line x1="{gx}" y1="0" x2="{gx}" y2="{height}" stroke="#334155" stroke-width="0.5" stroke-dasharray="3,6" opacity="0.3"/>')
    for gy in range(0, height, 80):
        svg.append(f'<line x1="0" y1="{gy}" x2="{width}" y2="{gy}" stroke="#334155" stroke-width="0.5" stroke-dasharray="3,6" opacity="0.3"/>')

    # Header
    svg.append('<text x="60" y="65" fill="#38bdf8" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="28" font-weight="700">16等分扇形圆模型 — 拓竹 X1C + PETG-HF 纹理板工程视图 (Ø120mm × 6mm)</text>')
    svg.append('<text x="60" y="98" fill="#94a3b8" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="15" font-weight="500">16 × 22.5° Equal Detachable Sectors • 0.10mm Side Clearance • R0.8mm Tip Fillet • 0.4mm Top/Bottom Chamfer</text>')

    # Cards for viewports
    svg.append('<rect x="50" y="125" width="820" height="920" rx="14" fill="#0f172a" stroke="#334155" stroke-width="1.5" opacity="0.65"/>')
    svg.append('<text x="80" y="168" fill="#f8fafc" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="20" font-weight="700">1. 16等分圆盘拼合轴测图 (Assembled Ø120mm × 6mm)</text>')

    svg.append('<rect x="900" y="125" width="850" height="445" rx="14" fill="#0f172a" stroke="#334155" stroke-width="1.5" opacity="0.65"/>')
    svg.append('<text x="930" y="168" fill="#f8fafc" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="20" font-weight="700">2. 拓竹 X1C 纹理 PEI 板一盘直打排布 (Top Orthographic View)</text>')

    svg.append('<rect x="900" y="600" width="850" height="445" rx="14" fill="#0f172a" stroke="#334155" stroke-width="1.5" opacity="0.65"/>')
    svg.append('<text x="930" y="643" fill="#f8fafc" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="20" font-weight="700">3. 扇形交错拼接近似平行四边形 (Area Derivation: πr × r)</text>')

    # Render Viewport 1
    svg.extend(build_viewport_polygons(vp1_objs, az_deg=25, el_deg=38, vp_cx=460, vp_cy=580, scale=5.2))
    svg.append('<rect x="85" y="965" width="750" height="52" rx="8" fill="#1e293b" stroke="#38bdf8" stroke-width="1"/>')
    svg.append('<text x="110" y="997" fill="#e2e8f0" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="15" font-weight="600">外径 Ø120.0 mm (半径 R=60.0 mm)  |  单块圆心角 22.5° (360°÷16)  |  厚度 H=6.0 mm</text>')

    # Render Viewport 2
    svg.extend(build_viewport_polygons(vp2_objs, az_deg=0, el_deg=90, vp_cx=1325, vp_cy=365, scale=2.55))
    svg.append('<text x="930" y="545" fill="#94a3b8" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="14">居中排布于 256×256mm 热床 (128,128)，径向展开 3.0mm 防粘连，16个独立对象支持分件染色</text>')

    # Render Viewport 3
    svg.extend(build_viewport_polygons(vp3_objs, az_deg=15, el_deg=42, vp_cx=1325, vp_cy=830, scale=3.1))
    svg.append('<text x="930" y="1020" fill="#94a3b8" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="14">上8块 + 下8块交错咬合 → 近似平行四边形（底 ≈ πR = 188.5 mm，高 ≈ R = 60.0 mm）</text>')

    svg.append('</svg>')

    svg_path = out_png.replace('.png', '.svg')
    with open(svg_path, 'w') as f:
        f.write('\n'.join(svg))
    subprocess.run(['sips', '-s', 'format', 'png', svg_path, '--out', out_png], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Rendered CAD preview: {out_png}")


# ============================================================================
# 3. Peak-Optimized Bambu Lab X1C + PETG-HF + 0.4mm + Textured PEI 3MF Generator
# ============================================================================
def generate_bambu_3mf(sector_stl_files, out_3mf_name="Circle_16_Sectors_X1C_PETG-HF.3mf"):
    print(f"\n=======================================================")
    print(f"Generating Peak-Optimized Bambu X1C 3MF: {out_3mf_name}")
    print(f"=======================================================")

    raw_3mf_path = os.path.join("/tmp", "raw_" + out_3mf_name)
    final_3mf_path = os.path.join(OUT_DIR, out_3mf_name)

    # 1. Load & Flatten Official Bambu Lab X1C Machine, Filament, and Process Profiles
    flat_machine = load_flattened_bambu_profile("machine", "Bambu Lab X1 Carbon 0.4 nozzle.json")
    flat_machine_bed = {
        "curr_bed_type": "Textured PEI Plate"
    }
    flat_machine.update(flat_machine_bed)

    flat_filament = load_flattened_bambu_profile("filament", "Bambu PETG HF @BBL X1C.json")
    # Optimize Bambu PETG-HF specifically for Textured PEI Plate & 16-island anti-stringing
    petg_hf_overrides = {
        "name": "Bambu PETG HF @BBL X1C",
        "filament_settings_id": ["Bambu PETG HF @BBL X1C"],
        "filament_type": ["PETG"],
        "filament_vendor": ["Bambu Lab"],
        "filament_density": ["1.27"],
        "filament_cost": ["24.99"],
        "filament_max_volumetric_speed": ["21"],
        "nozzle_temperature": ["245"],
        "nozzle_temperature_initial_layer": ["245"],
        "textured_plate_temp": ["70"],
        "textured_plate_temp_initial_layer": ["70"],
        "hot_plate_temp": ["70"],
        "hot_plate_temp_initial_layer": ["70"],
        "eng_plate_temp": ["70"],
        "eng_plate_temp_initial_layer": ["70"],
        "cool_plate_temp": ["70"],
        "cool_plate_temp_initial_layer": ["70"],
        "auxiliary_fan": ["0"],
        "additional_cooling_fan_speed": ["0"],
        "close_fan_the_first_x_layers": ["3"],
        "fan_min_speed": ["10"],
        "fan_max_speed": ["40"],
        "overhang_fan_speed": ["90"],
        "slow_down_for_layer_cooling": ["1"],
        "filament_retraction_length": ["0.8"],
        "filament_retraction_speed": ["30"],
        "filament_deretraction_speed": ["30"],
        "filament_z_hop": ["0.4"],
        "filament_z_hop_types": ["Spiral Lift"]
    }
    flat_filament.update(petg_hf_overrides)

    flat_process = load_flattened_bambu_profile("process", "0.20mm Standard @BBL X1C.json")
    # Peak process settings for 16-sector precision geometric puzzle blocks (Ø120×6mm)
    process_overrides = {
        "type": "process",
        "name": "0.20mm Optimal PETG-HF 16-Sectors @X1C",
        "inherits": "0.20mm Standard @BBL X1C",
        "from": "user",
        "setting_id": "UP_CIRCLE16_X1C_PETGHF",
        "instantiation": "true",
        "description": "Peak-tuned X1C 0.4mm profile for 16-sector circle with Bambu PETG-HF on Textured PEI Plate.",
        # Layer & Line Widths (6.0mm height = exact 30 layers @ 0.20mm; 0.4mm chamfer = exact 2 layers)
        "layer_height": "0.20",
        "initial_layer_print_height": "0.20",
        "line_width": "0.42",
        "initial_layer_line_width": "0.45",
        "outer_wall_line_width": "0.42",
        "inner_wall_line_width": "0.45",
        "top_surface_line_width": "0.42",
        # Arachne & Dimensional Accuracy for 22.5° Acute Tips
        "wall_generator": "arachne",
        "wall_transition_angle": "10",
        "min_feature_size": "25%",
        "min_wall_width": "85%",
        "precise_outer_wall": "1",
        "elefant_foot_compensation": "0.15",
        # Shells & Infill
        "wall_loops": "3",
        "top_shell_layers": "5",
        "top_shell_thickness": "1.0",
        "bottom_shell_layers": "4",
        "bottom_shell_thickness": "0.8",
        "top_surface_pattern": "monotonicline",
        "bottom_surface_pattern": "monotonic",
        "sparse_infill_pattern": "gyroid",
        "sparse_infill_density": "20%",
        # Anti-Stringing & Seam Control across 16 Separate Islands
        "seam_position": "back",
        "seam_slope_type": "all",
        "scarf_angle_threshold": "155",
        "wipe_on_loops": "1",
        "reduce_infill_retraction": "0",
        "reduce_crossing_wall": "1",
        "max_travel_detour_distance": "0",
        # Adhesion (No Brim needed so sector edges stay clean and dimensionally exact)
        "brim_type": "no_brim",
        "brim_width": "0",
        "enable_support": "0",
        "curr_bed_type": "Textured PEI Plate",
        # Speeds & Accelerations tuned for PETG-HF gloss & sharp corners
        "initial_layer_speed": "35",
        "initial_layer_infill_speed": "45",
        "outer_wall_speed": "65",
        "inner_wall_speed": "140",
        "top_surface_speed": "60",
        "sparse_infill_speed": "180",
        "internal_solid_infill_speed": "180",
        "travel_speed": "500",
        "default_acceleration": "8000",
        "outer_wall_acceleration": "4000",
        "inner_wall_acceleration": "8000",
        "top_surface_acceleration": "2000",
        "initial_layer_acceleration": "500"
    }
    flat_process.update(process_overrides)

    temp_machine_path = "/tmp/flat_x1c_machine.json"
    temp_filament_path = "/tmp/flat_petg_hf_filament.json"
    temp_process_path = "/tmp/flat_circle16_process.json"

    with open(temp_machine_path, "w") as f:
        json.dump(flat_machine, f, indent=4)
    with open(temp_filament_path, "w") as f:
        json.dump(flat_filament, f, indent=4)
    with open(temp_process_path, "w") as f:
        json.dump(flat_process, f, indent=4)

    # 2. Export base 3MF with all 16 sector STL files
    cmd_base = [
        BAMBU_CLI,
        "--load-settings", f"{temp_machine_path};{temp_process_path}",
        "--load-filaments", temp_filament_path,
        "--export-3mf", raw_3mf_path,
    ] + sector_stl_files

    res = subprocess.run(cmd_base, capture_output=True, text=True)
    if res.returncode != 0:
        print("Error exporting base 3MF:", res.stderr)
        sys.exit(1)

    unpack_dir = os.path.join("/tmp", "unpack_" + out_3mf_name)
    if os.path.exists(unpack_dir):
        shutil.rmtree(unpack_dir)
    os.makedirs(unpack_dir, exist_ok=True)

    with zipfile.ZipFile(raw_3mf_path, 'r') as z:
        z.extractall(unpack_dir)

    # 3. Synchronize Metadata/project_settings.config
    proj_cfg_path = os.path.join(unpack_dir, "Metadata", "project_settings.config")
    if os.path.exists(proj_cfg_path):
        with open(proj_cfg_path, 'r') as f:
            proj_cfg = json.load(f)

        for k, v in petg_hf_overrides.items():
            proj_cfg[k] = v
        for k, v in process_overrides.items():
            if k not in ("type", "name", "inherits", "from", "setting_id", "instantiation", "description"):
                proj_cfg[k] = v
        proj_cfg["curr_bed_type"] = "Textured PEI Plate"
        proj_cfg["printable_area"] = ["0x0", "256x0", "256x256", "0x256"]
        proj_cfg["printable_height"] = "256"
        proj_cfg["bed_exclude_area"] = ["0x0", "18x0", "18x28", "0x28"]
        proj_cfg["process_settings_id"] = "0.20mm Optimal PETG-HF 16-Sectors @X1C"
        proj_cfg["filament_settings_id"] = ["Bambu PETG HF @BBL X1C"]
        proj_cfg["printer_settings_id"] = "Bambu Lab X1 Carbon 0.4 nozzle"

        with open(proj_cfg_path, 'w') as f:
            json.dump(proj_cfg, f, indent=4)

    # 4. Place all 16 sector objects centered on the 256x256 mm build plate at (128.0, 128.0, 0.0)
    # Since each STL in `sector_stl_files` is already positioned around (128.0, 128.0, 0.0) in bed coordinates,
    # its <component> transform inside 3dmodel.model already stores its exact (128+dx, 128+dy, 3.0) position,
    # so setting <item transform="1 0 0 0 1 0 0 0 1 0 0 0"> keeps every single sector at its exact bed coordinate!
    model_xml_path = os.path.join(unpack_dir, "3D", "3dmodel.model")
    with open(model_xml_path, 'r') as f:
        content = f.read()

    items = re.findall(r'<item\s+objectid="(\d+)"\s+p:UUID="([^"]+)"\s+transform="[^"]+"\s+printable="1"/>', content)
    for obj_id, obj_uuid in items:
        new_item = f'<item objectid="{obj_id}" p:UUID="{obj_uuid}" transform="1 0 0 0 1 0 0 0 1 0.0 0.0 0.0" printable="1"/>'
        content = re.sub(
            rf'<item\s+objectid="{obj_id}"\s+p:UUID="{obj_uuid}"\s+transform="[^"]+"\s+printable="1"/>',
            new_item,
            content
        )

    with open(model_xml_path, 'w') as f:
        f.write(content)

    # 5. Pack intermediate 3MF
    inter_3mf_path = os.path.join("/tmp", "inter_" + out_3mf_name)
    with zipfile.ZipFile(inter_3mf_path, 'w', zipfile.ZIP_DEFLATED) as zout:
        for root, dirs, files in os.walk(unpack_dir):
            for file in files:
                full = os.path.join(root, file)
                rel = os.path.relpath(full, unpack_dir)
                zout.write(full, rel)

    # 6. Slice with BambuStudio CLI using flattened settings
    slice_out_dir = os.path.join("/tmp", "slice_circle16_x1c")
    if os.path.exists(slice_out_dir):
        shutil.rmtree(slice_out_dir)
    os.makedirs(slice_out_dir, exist_ok=True)

    cmd_slice = [
        BAMBU_CLI,
        "--load-settings", f"{temp_machine_path};{temp_process_path}",
        "--load-filaments", temp_filament_path,
        "--slice", "0",
        "--outputdir", slice_out_dir,
        inter_3mf_path
    ]
    print("Running BambuStudio CLI slice engine with flattened X1C + PETG-HF profiles...")
    res_slice = subprocess.run(cmd_slice, capture_output=True, text=True)
    if res_slice.returncode != 0:
        print("Slice stderr:", res_slice.stderr)

    gcode_path = os.path.join(slice_out_dir, "plate_1.gcode")
    result_json_path = os.path.join(slice_out_dir, "result.json")

    if os.path.exists(gcode_path):
        print(f"Generated G-code size: {os.path.getsize(gcode_path):,} bytes")
        shutil.copyfile(gcode_path, os.path.join(unpack_dir, "Metadata", "plate_1.gcode"))
        for thumb in ["plate_1.png", "plate_1_small.png", "plate_no_light_1.png", "pick_1.png", "top_1.png"]:
            thumb_p = os.path.join(slice_out_dir, thumb)
            if os.path.exists(thumb_p):
                shutil.copyfile(thumb_p, os.path.join(unpack_dir, "Metadata", thumb))

        with zipfile.ZipFile(final_3mf_path, 'w', zipfile.ZIP_DEFLATED) as zfinal:
            for root, dirs, files in os.walk(unpack_dir):
                for file in files:
                    full = os.path.join(root, file)
                    rel = os.path.relpath(full, unpack_dir)
                    zfinal.write(full, rel)

        if os.path.exists(result_json_path):
            with open(result_json_path) as f:
                slice_res = json.load(f)
            plate_info = slice_res['sliced_plates'][0]
            est_sec = float(plate_info.get('total_predication', 0))
            weight_g = float(plate_info['filaments'][0].get('total_used_g', 0)) if plate_info.get('filaments') else 0.0
            print(f"SUCCESS! Embedded G-code into {out_3mf_name}")
            print(f"  Estimated Print Time: {int(est_sec // 3600)}h {int((est_sec % 3600) // 60)}m {int(est_sec % 60)}s ({est_sec:.1f}s)")
            print(f"  Filament Used:        {weight_g:.2f} g")
    else:
        shutil.copyfile(inter_3mf_path, final_3mf_path)
        print(f"Saved 3MF project to {final_3mf_path}")


def main():
    single_tris = build_single_sector_triangles()

    # 1. Single sector STL
    single_stl_path = os.path.join(OUT_DIR, "Circle_16_Sector_Single.stl")
    write_binary_stl(single_stl_path, single_tris, "16-Sector Circle - Single 22.5 deg Sector")

    # 2. Assembled 16-sector circle STL (0.2mm radial offset so all 16 shells remain separate)
    assembled_tris = []
    for i in range(NUM_SECTORS):
        ang = i * SECTOR_ANGLE_DEG
        rad = math.radians(ang)
        r_off = 0.20
        assembled_tris.extend(
            transform_triangles(single_tris, rot_deg=ang, tx=r_off * math.cos(rad), ty=r_off * math.sin(rad))
        )
    assembled_stl_path = os.path.join(OUT_DIR, "Circle_16_Sectors_Assembled.stl")
    write_binary_stl(assembled_stl_path, assembled_tris, "16-Sector Circle - Assembled O120x6mm")

    # 3. Print Plate 16-sector circle STL (3.0mm radial separation for one-shot printing)
    #    and 16 individual STL files in X1C bed coordinates centered at (128.0, 128.0, 0.0)
    print_plate_tris = []
    temp_sector_stls = []
    os.makedirs("/tmp/circle16_parts", exist_ok=True)
    for i in range(NUM_SECTORS):
        ang = i * SECTOR_ANGLE_DEG
        rad = math.radians(ang)
        r_off = 3.0
        # For standalone Print_Plate.stl (centered at 0,0)
        sec_local = transform_triangles(single_tris, rot_deg=ang, tx=r_off * math.cos(rad), ty=r_off * math.sin(rad))
        print_plate_tris.extend(sec_local)

        # For Bambu X1C 3MF (centered at bed 128.0, 128.0, 0.0)
        sec_bed = transform_triangles(
            single_tris,
            rot_deg=ang,
            tx=128.0 + r_off * math.cos(rad),
            ty=128.0 + r_off * math.sin(rad),
            tz=0.0
        )
        part_stl = f"/tmp/circle16_parts/Sector_{i+1:02d}.stl"
        write_binary_stl(part_stl, sec_bed, f"Sector {i+1:02d} of 16")
        temp_sector_stls.append(part_stl)

    print_plate_stl_path = os.path.join(OUT_DIR, "Circle_16_Sectors_Print_Plate.stl")
    write_binary_stl(print_plate_stl_path, print_plate_tris, "16-Sector Circle - Print Plate Layout")

    # 4. Render CAD Preview
    render_png_path = os.path.join(OUT_DIR, "Circle_16_Sectors_CAD_Render.png")
    render_cad_preview(single_tris, render_png_path)

    # 5. Generate Bambu Lab X1C 3MF Project
    if os.path.exists(BAMBU_CLI):
        generate_bambu_3mf(temp_sector_stls, "Circle_16_Sectors_X1C_PETG-HF.3mf")


if __name__ == "__main__":
    main()
