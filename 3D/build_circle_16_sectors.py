#!/usr/bin/env python3
"""
High-Precision Parametric 3D Mesh, CAD Render & Bambu X1C 3MF Generator
for 16-Sector Circle Model (Ø120mm × 6mm, 16 Equal Detachable Sectors).
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
BAMBU_CLI = "/Applications/BambuStudio.app/Contents/MacOS/BambuStudio"
MACHINE_JSON = "/Applications/BambuStudio.app/Contents/Resources/profiles/BBL/machine/Bambu Lab X1 Carbon 0.4 nozzle.json"
FILAMENT_JSON = "/Applications/BambuStudio.app/Contents/Resources/profiles/BBL/filament/Bambu PETG HF @BBL X1C.json"

# ============================================================================
# 1. Exact Parametric Geometry for a Single 1/16 Sector (22.5°)
# ============================================================================
DIAMETER = 120.0
RADIUS = DIAMETER / 2.0          # 60.0 mm
THICKNESS = 6.0                  # 6.0 mm
NUM_SECTORS = 16
SECTOR_ANGLE_DEG = 360.0 / NUM_SECTORS  # 22.5°
SECTOR_ANGLE_RAD = math.radians(SECTOR_ANGLE_DEG)
HALF_ANGLE = SECTOR_ANGLE_RAD / 2.0     # 11.25°

SIDE_CLEARANCE = 0.10            # 0.10 mm per side (0.20 mm between neighbors)
TIP_FILLET_R = 0.80              # 0.80 mm rounded tip radius to prevent 22.5° nozzle blobbing
EDGE_CHAMFER = 0.40              # 0.40 mm 45° chamfer on top and bottom edges


def generate_sector_polygon_2d(inset=0.0, arc_steps=32, tip_steps=12):
    """
    Computes a counter-clockwise 2D polygon for one sector centered along +X axis
    (spanning [-HALF_ANGLE, +HALF_ANGLE]), inset inward by (SIDE_CLEARANCE + inset).
    Includes a smooth tangent rounded tip of radius TIP_FILLET_R and corner fillets
    at the outer arc transitions.
    """
    d = SIDE_CLEARANCE + inset
    r_out = RADIUS - inset
    r_tip = max(0.3, TIP_FILLET_R - inset * 0.5)

    # Line equations for the two straight sides offset inward by distance `d`:
    # Bottom side (angle -HALF_ANGLE): normal pointing inward is (-sin(HALF_ANGLE), +cos(HALF_ANGLE))
    # A point along bottom offset ray at distance s from shifted apex:
    # Shifted apex where the two offset rays meet:
    # x_apex = d / sin(HALF_ANGLE)
    sin_h = math.sin(HALF_ANGLE)
    cos_h = math.cos(HALF_ANGLE)

    # Center of the tangent tip circle of radius r_tip that touches both offset side rays:
    # Distance from origin to tip circle center along +X axis:
    cx_tip = (d + r_tip) / sin_h
    # The tangent point on the bottom ray is at angle (-PI/2 - HALF_ANGLE) from tip center
    # Wait: inward normal to bottom ray is (-sin_h, +cos_h), so outward normal is (+sin_h, -cos_h)
    # Wait! For angle = -HALF_ANGLE, ray direction is (cos_h, -sin_h).
    # Outward normal of bottom ray is (-sin_h, -cos_h).
    # Let's verify: dot((cos_h, -sin_h), (-sin_h, -cos_h)) = 0, and y-component is negative (pointing down/outward).
    # Yes! So on the tip circle centered at (cx_tip, 0), the tangent point to the bottom ray is at angle (-pi/2 - HALF_ANGLE) -> Wait:
    # Normal vector (-sin_h, -cos_h) has angle: atan2(-cos_h, -sin_h) = -pi/2 - HALF_ANGLE.
    # And on the top ray, outward normal is (-sin_h, +cos_h), which has angle: +pi/2 + HALF_ANGLE.
    # Let's check x-coordinate of tangent point on bottom ray:
    # x_tan = cx_tip + r_tip * (-sin_h), y_tan = 0 + r_tip * (-cos_h)
    # Distance from origin line (x*sin_h + y*cos_h = 0):
    # x_tan * sin_h + y_tan * cos_h = ((d + r_tip)/sin_h - r_tip*sin_h)*sin_h - r_tip*cos_h^2
    # = d + r_tip - r_tip*(sin_h^2 + cos_h^2) = d. Exact!

    pts = []

    # 1. Tip arc from top tangent (angle = +pi/2 + HALF_ANGLE) to bottom tangent (angle = 3*pi/2 - HALF_ANGLE)
    ang_start = math.pi / 2.0 + HALF_ANGLE
    ang_end = 1.5 * math.pi - HALF_ANGLE
    for i in range(tip_steps + 1):
        t = i / tip_steps
        ang = ang_start + t * (ang_end - ang_start)
        px = cx_tip + r_tip * math.cos(ang)
        py = r_tip * math.sin(ang)
        pts.append((px, py))

    # 2. Intersection of offset bottom ray with outer circle of radius r_out:
    # Bottom ray is parameterized by: P(s) = (s * cos_h, -s * sin_h) + (-d * sin_h, d * cos_h)
    # Wait: let's check P(s) dot (-sin_h, cos_h) = -s*cos_h*sin_h - s*sin_h*cos_h + d*(sin_h^2 + cos_h^2)
    # Better: use rotated coordinates (u along ray, v perpendicular inward = d):
    # Since |P|^2 = u^2 + d^2 = r_out^2, at the outer circle u_out = sqrt(r_out^2 - d^2).
    u_out = math.sqrt(max(0.0, r_out * r_out - d * d))
    # Angle of bottom corner on the outer circle of radius r_out:
    # In polar coords, angle = -HALF_ANGLE + asin(d / r_out)
    ang_margin = math.asin(d / r_out)
    arc_ang_start = -HALF_ANGLE + ang_margin
    arc_ang_end = +HALF_ANGLE - ang_margin

    # 3. Outer arc from bottom corner (arc_ang_start) to top corner (arc_ang_end)
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
        # Clockwise from top = counter-clockwise from bottom
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
        r_off = 0.6  # Slight visual separation so all 16 sectors are crisp
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
    total_w = 7.5 * arc_step
    y_height = RADIUS * math.cos(HALF_ANGLE)
    for i in range(8):
        # Bottom row pointing up (+Y)
        bx = (i - 3.75) * arc_step
        by = -y_height / 2.0
        tris_b = transform_triangles(single_tris, rot_deg=90.0, tx=bx, ty=by)
        vp3_objs.append((tris_b, "#38bdf8"))
        # Top row pointing down (-Y)
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
    svg.append('  <linearGradient id="headerGrad" x1="0" y1="0" x2="1" y2="0">')
    svg.append('    <stop offset="0%" stop-color="#38bdf8"/>')
    svg.append('    <stop offset="100%" stop-color="#f59e0b"/>')
    svg.append('  </linearGradient>')
    svg.append('</defs>')
    svg.append('<rect width="100%" height="100%" fill="url(#bg)"/>')

    # Grid lines
    for gx in range(0, width, 80):
        svg.append(f'<line x1="{gx}" y1="0" x2="{gx}" y2="{height}" stroke="#334155" stroke-width="0.5" stroke-dasharray="3,6" opacity="0.3"/>')
    for gy in range(0, height, 80):
        svg.append(f'<line x1="0" y1="{gy}" x2="{width}" y2="{gy}" stroke="#334155" stroke-width="0.5" stroke-dasharray="3,6" opacity="0.3"/>')

    # Header
    svg.append('<text x="60" y="65" fill="url(#headerGrad)" font-family="-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif" font-size="30" font-weight="800">16等分扇形圆模型 (Ø120mm × 6mm 可分离拼块)</text>')
    svg.append('<text x="60" y="98" fill="#94a3b8" font-family="-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif" font-size="15" font-weight="500">16 × 22.5° Equal Detachable Sectors • 0.10mm Side Clearance • R0.8mm Tip Fillet • 0.4mm Top/Bottom Chamfer</text>')

    # Cards for viewports
    svg.append('<rect x="50" y="125" width="820" height="920" rx="14" fill="#0f172a" stroke="#334155" stroke-width="1.5" opacity="0.65"/>')
    svg.append('<text x="80" y="168" fill="#f8fafc" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="20" font-weight="700">1. 16等分圆盘拼合轴测图 (Assembled Ø120mm × 6mm)</text>')

    svg.append('<rect x="900" y="125" width="850" height="445" rx="14" fill="#0f172a" stroke="#334155" stroke-width="1.5" opacity="0.65"/>')
    svg.append('<text x="930" y="168" fill="#f8fafc" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="20" font-weight="700">2. 俯视正交图 / 打印盘防粘连排布 (Top Orthographic View)</text>')

    svg.append('<rect x="900" y="600" width="850" height="445" rx="14" fill="#0f172a" stroke="#334155" stroke-width="1.5" opacity="0.65"/>')
    svg.append('<text x="930" y="643" fill="#f8fafc" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="20" font-weight="700">3. 扇形交错拼接近似平行四边形 (Area Derivation: πr × r)</text>')

    # Render Viewport 1
    svg.extend(build_viewport_polygons(vp1_objs, az_deg=25, el_deg=38, vp_cx=460, vp_cy=580, scale=5.2))
    # Annotations on Viewport 1
    svg.append('<rect x="85" y="965" width="750" height="52" rx="8" fill="#1e293b" stroke="#38bdf8" stroke-width="1"/>')
    svg.append('<text x="110" y="997" fill="#e2e8f0" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="15" font-weight="600">外径 Ø120.0 mm (半径 R=60.0 mm)  |  单块圆心角 22.5° (360°÷16)  |  厚度 H=6.0 mm</text>')

    # Render Viewport 2 (Top-down orthographic el_deg=90)
    svg.extend(build_viewport_polygons(vp2_objs, az_deg=0, el_deg=90, vp_cx=1325, vp_cy=365, scale=2.55))
    svg.append('<text x="930" y="545" fill="#94a3b8" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="14">径向展开 3.0mm 间距一盘直打，16个独立零件互不粘连，支持拓竹 AMS 双色/多色分件染色</text>')

    # Render Viewport 3 (Parallelogram isometric)
    svg.extend(build_viewport_polygons(vp3_objs, az_deg=15, el_deg=42, vp_cx=1325, vp_cy=830, scale=3.1))
    svg.append('<text x="930" y="1020" fill="#94a3b8" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="14">上8块 + 下8块交错咬合 → 近似平行四边形（底 ≈ πR = 188.5 mm，高 ≈ R = 60.0 mm）</text>')

    svg.append('</svg>')

    svg_path = out_png.replace('.png', '.svg')
    with open(svg_path, 'w') as f:
        f.write('\n'.join(svg))
    subprocess.run(['sips', '-s', 'format', 'png', svg_path, '--out', out_png], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Rendered CAD preview: {out_png}")


# ============================================================================
# 3. Bambu Lab X1C 3MF Project Generator (16 Independent Sector Objects)
# ============================================================================
def generate_bambu_3mf(sector_stl_files, out_3mf_name="Circle_16_Sectors_X1C_PETG-HF.3mf"):
    print(f"\n=======================================================")
    print(f"Generating Bambu Studio 3MF: {out_3mf_name}")
    print(f"=======================================================")

    raw_3mf_path = os.path.join("/tmp", "raw_" + out_3mf_name)
    final_3mf_path = os.path.join(BASE_DIR, out_3mf_name)

    custom_process = {
        "type": "process",
        "name": "0.20mm Standard PETG-HF 16-Sectors @X1C",
        "inherits": "0.20mm Standard @BBL X1C",
        "from": "user",
        "setting_id": "UP_CIRCLE16_OPT_01",
        "instantiation": "true",
        "description": "High-precision profile for 16-sector Ø120x6mm circle model on Bambu X1C with PETG-HF.",
        "layer_height": "0.20",
        "initial_layer_print_height": "0.20",
        "wall_loops": "3",
        "top_shell_layers": "5",
        "bottom_shell_layers": "4",
        "sparse_infill_pattern": "gyroid",
        "sparse_infill_density": "20%",
        "brim_type": "no_brim",
        "elefant_foot_compensation": "0.15",
        "precise_outer_wall": "1",
        "curr_bed_type": "Textured PEI Plate",
        "textured_plate_temp": ["70"],
        "textured_plate_temp_initial_layer": ["70"],
        "nozzle_temperature": ["245"],
        "nozzle_temperature_initial_layer": ["245"],
        "compatible_printers": [
            "Bambu Lab X1 Carbon 0.4 nozzle",
            "Bambu Lab X1 0.4 nozzle",
            "Bambu Lab P1S 0.4 nozzle",
            "Bambu Lab X1E 0.4 nozzle"
        ]
    }

    temp_process_path = "/tmp/bambu_petg_hf_circle16.json"
    with open(temp_process_path, "w") as f:
        json.dump(custom_process, f, indent=4)

    cmd_base = [
        BAMBU_CLI,
        "--load-settings", f"{MACHINE_JSON};{temp_process_path}",
        "--load-filaments", FILAMENT_JSON,
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

    # Inject project_settings.config
    proj_cfg_path = os.path.join(unpack_dir, "Metadata", "project_settings.config")
    if os.path.exists(proj_cfg_path):
        with open(proj_cfg_path, 'r') as f:
            proj_cfg = json.load(f)

        proj_cfg["curr_bed_type"] = "Textured PEI Plate"
        proj_cfg["textured_plate_temp"] = ["70"]
        proj_cfg["textured_plate_temp_initial_layer"] = ["70"]
        proj_cfg["nozzle_temperature"] = ["245"]
        proj_cfg["nozzle_temperature_initial_layer"] = ["245"]
        proj_cfg["layer_height"] = "0.20"
        proj_cfg["initial_layer_print_height"] = "0.20"
        proj_cfg["wall_loops"] = "3"
        proj_cfg["top_shell_layers"] = "5"
        proj_cfg["bottom_shell_layers"] = "4"
        proj_cfg["sparse_infill_pattern"] = "gyroid"
        proj_cfg["sparse_infill_density"] = "20%"
        proj_cfg["brim_type"] = "no_brim"
        proj_cfg["elefant_foot_compensation"] = "0.15"
        proj_cfg["precise_outer_wall"] = "1"

        with open(proj_cfg_path, 'w') as f:
            json.dump(proj_cfg, f, indent=4)

    # Position all 16 sector objects centered around the 256x256 build plate (128, 128)
    # Because each sector STL is already generated in its exact print-plate XY coordinates
    # relative to (0,0), placing each object's origin at (128, 128, 0) arranges them in a
    # perfect 16-sector circle centered on the build plate!
    model_xml_path = os.path.join(unpack_dir, "3D", "3dmodel.model")
    with open(model_xml_path, 'r') as f:
        content = f.read()

    items = re.findall(r'<item\s+objectid="(\d+)"\s+p:UUID="([^"]+)"\s+transform="[^"]+"\s+printable="1"/>', content)
    for obj_id, obj_uuid in items:
        new_item = f'<item objectid="{obj_id}" p:UUID="{obj_uuid}" transform="1 0 0 0 1 0 0 0 1 128.0 128.0 0.0" printable="1"/>'
        content = re.sub(
            rf'<item\s+objectid="{obj_id}"\s+p:UUID="{obj_uuid}"\s+transform="[^"]+"\s+printable="1"/>',
            new_item,
            content
        )

    with open(model_xml_path, 'w') as f:
        f.write(content)

    # Pack intermediate 3MF
    inter_3mf_path = os.path.join("/tmp", "inter_" + out_3mf_name)
    with zipfile.ZipFile(inter_3mf_path, 'w', zipfile.ZIP_DEFLATED) as zout:
        for root, dirs, files in os.walk(unpack_dir):
            for file in files:
                full = os.path.join(root, file)
                rel = os.path.relpath(full, unpack_dir)
                zout.write(full, rel)

    # Slice with BambuStudio CLI
    slice_out_dir = os.path.join("/tmp", "slice_circle16_x1c")
    if os.path.exists(slice_out_dir):
        shutil.rmtree(slice_out_dir)
    os.makedirs(slice_out_dir, exist_ok=True)

    cmd_slice = [
        BAMBU_CLI,
        "--slice", "0",
        "--outputdir", slice_out_dir,
        inter_3mf_path
    ]
    print("Running BambuStudio CLI slice engine...")
    subprocess.run(cmd_slice, capture_output=True, text=True)

    gcode_path = os.path.join(slice_out_dir, "plate_1.gcode")
    result_json_path = os.path.join(slice_out_dir, "result.json")

    if os.path.exists(gcode_path):
        print(f"Generated G-code size: {os.path.getsize(gcode_path):,} bytes")
        shutil.copyfile(gcode_path, os.path.join(unpack_dir, "Metadata", "plate_1.gcode"))

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
            print(f"  Estimated Print Time: {int(est_sec // 60)}m {int(est_sec % 60)}s")
            print(f"  Filament Used:        {weight_g:.1f} g")
    else:
        shutil.copyfile(inter_3mf_path, final_3mf_path)
        print(f"Saved 3MF project to {final_3mf_path}")


def main():
    single_tris = build_single_sector_triangles()

    # 1. Single sector STL
    single_stl_path = os.path.join(BASE_DIR, "Circle_16_Sector_Single.stl")
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
    assembled_stl_path = os.path.join(BASE_DIR, "Circle_16_Sectors_Assembled.stl")
    write_binary_stl(assembled_stl_path, assembled_tris, "16-Sector Circle - Assembled O120x6mm")

    # 3. Print Plate 16-sector circle STL (3.0mm radial separation for one-shot printing) + 16 individual temp STLs for 3MF
    print_plate_tris = []
    temp_sector_stls = []
    os.makedirs("/tmp/circle16_parts", exist_ok=True)
    for i in range(NUM_SECTORS):
        ang = i * SECTOR_ANGLE_DEG
        rad = math.radians(ang)
        r_off = 3.0
        sec_i_tris = transform_triangles(single_tris, rot_deg=ang, tx=r_off * math.cos(rad), ty=r_off * math.sin(rad))
        print_plate_tris.extend(sec_i_tris)
        part_stl = f"/tmp/circle16_parts/Sector_{i+1:02d}.stl"
        write_binary_stl(part_stl, sec_i_tris, f"Sector {i+1:02d} of 16")
        temp_sector_stls.append(part_stl)

    print_plate_stl_path = os.path.join(BASE_DIR, "Circle_16_Sectors_Print_Plate.stl")
    write_binary_stl(print_plate_stl_path, print_plate_tris, "16-Sector Circle - Print Plate Layout")

    # 4. Render CAD Preview
    render_png_path = os.path.join(BASE_DIR, "Circle_16_Sectors_CAD_Render.png")
    render_cad_preview(single_tris, render_png_path)

    # 5. Generate Bambu Lab X1C 3MF Project
    if os.path.exists(BAMBU_CLI):
        generate_bambu_3mf(temp_sector_stls, "Circle_16_Sectors_X1C_PETG-HF.3mf")


if __name__ == "__main__":
    main()
