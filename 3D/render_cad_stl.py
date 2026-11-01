#!/usr/bin/env python3
"""
Pure Python 3D CAD Orthographic & Isometric Vector/Raster Renderer
Renders true STL geometry to SVG and rasterizes to PNG via sips.
"""

import math
import struct
import os
import subprocess

def load_stl(filepath):
    triangles = []
    with open(filepath, 'rb') as f:
        f.seek(80)
        n_tris = struct.unpack('<I', f.read(4))[0]
        for _ in range(n_tris):
            data = f.read(50)
            if len(data) < 50:
                break
            n = struct.unpack('<3f', data[0:12])
            v0 = struct.unpack('<3f', data[12:24])
            v1 = struct.unpack('<3f', data[24:36])
            v2 = struct.unpack('<3f', data[36:48])
            triangles.append((n, v0, v1, v2))
    return triangles

def render_view(triangles, out_png, view_type='iso', width=1200, height=900, title="3D CAD Render"):
    # Light direction
    lx, ly, lz = 0.5, -0.6, 0.8
    ll = math.sqrt(lx**2 + ly**2 + lz**2)
    lx, ly, lz = lx/ll, ly/ll, lz/ll

    # Transform matrices
    # Isometric: rot Z 45 deg, rot X 55 deg
    if view_type == 'iso':
        az = math.radians(40) # Azimuth
        el = math.radians(35) # Elevation
    elif view_type == 'iso_left':
        az = math.radians(145) # View from left side (showing PWR / TF / BOOT opening)
        el = math.radians(35)
    elif view_type == 'top':
        az = math.radians(0)
        el = math.radians(89.9)
    elif view_type == 'bottom':
        az = math.radians(0)
        el = math.radians(-89.9)
    elif view_type == 'side_right':
        az = math.radians(90)
        el = math.radians(10)
    else:
        az = math.radians(45)
        el = math.radians(30)

    cos_az, sin_az = math.cos(az), math.sin(az)
    cos_el, sin_el = math.cos(el), math.sin(el)

    def project(v):
        # 1. Rotate Z by az
        x1 = v[0] * cos_az - v[1] * sin_az
        y1 = v[0] * sin_az + v[1] * cos_az
        z1 = v[2]
        # 2. Rotate X by (90-el) -> elevation
        # tilt around X
        tilt = math.pi/2.0 - el
        cos_t, sin_t = math.cos(tilt), math.sin(tilt)
        x2 = x1
        y2 = y1 * cos_t - z1 * sin_t
        z2 = y1 * sin_t + z1 * cos_t
        return x2, y2, z2

    # Process all triangles
    proj_tris = []
    min_x, max_x = float('inf'), -float('inf')
    min_y, max_y = float('inf'), -float('inf')

    for n, v0, v1, v2 in triangles:
        p0 = project(v0)
        p1 = project(v1)
        p2 = project(v2)

        # 2D cross product for front-facing check (in projection plane)
        cp = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0])
        if cp <= 0:
            continue # Back-face culling

        # Average Z for painter's depth sort
        avg_z = (p0[2] + p1[2] + p2[2]) / 3.0

        # Normal lighting
        nx, ny, nz = n
        norm_l = math.sqrt(nx**2 + ny**2 + nz**2)
        if norm_l > 1e-6:
            nx, ny, nz = nx/norm_l, ny/norm_l, nz/norm_l
            dot = max(0.0, nx * lx + ny * ly + nz * lz)
        else:
            dot = 0.5
        intensity = 0.35 + 0.65 * dot

        # Colors: stylish industrial cool grey with contrast
        base_r, base_g, base_b = 65, 75, 88 # Matte dark slate
        r = int(min(255, base_r * intensity * 2.2))
        g = int(min(255, base_g * intensity * 2.2))
        b = int(min(255, base_b * intensity * 2.2))
        fill_color = f"rgb({r},{g},{b})"
        edge_color = f"rgb({max(0, r-35)},{max(0, g-35)},{max(0, b-35)})"

        min_x = min(min_x, p0[0], p1[0], p2[0])
        max_x = max(max_x, p0[0], p1[0], p2[0])
        min_y = min(min_y, p0[1], p1[1], p2[1])
        max_y = max(max_y, p0[1], p1[1], p2[1])

        proj_tris.append((avg_z, p0, p1, p2, fill_color, edge_color))

    # Sort triangles back-to-front (depth sort)
    proj_tris.sort(key=lambda t: t[0])

    # Scale to canvas
    span_x = max(1.0, max_x - min_x)
    span_y = max(1.0, max_y - min_y)
    margin = 80
    scale = min((width - 2 * margin) / span_x, (height - 2 * margin) / span_y)

    mid_x = (min_x + max_x) / 2.0
    mid_y = (min_y + max_y) / 2.0

    def screen_pt(p):
        sx = width / 2.0 + (p[0] - mid_x) * scale
        sy = height / 2.0 - (p[1] - mid_y) * scale # Flip Y for screen
        return sx, sy

    # Generate SVG
    svg_lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        f'<rect width="{width}" height="{height}" fill="#14181F"/>', # Dark studio backdrop
        # Gradient backdrop circle
        f'<circle cx="{width/2}" cy="{height/2}" r="{min(width,height)*0.45}" fill="#1A202C" opacity="0.6"/>',
        # Title text
        f'<text x="40" y="50" font-family="-apple-system, BlinkMacSystemFont, Arial" font-size="22" font-weight="bold" fill="#E2E8F0">{title.replace("&", "&amp;")}</text>',
        f'<text x="40" y="75" font-family="-apple-system, BlinkMacSystemFont, Arial" font-size="13" fill="#94A3B8">Waveshare ESP32-S3-Touch-AMOLED-1.75-G MagSafe Enclosure | Actual 3D Mesh</text>',
    ]

    for avg_z, p0, p1, p2, fill_col, edge_col in proj_tris:
        s0 = screen_pt(p0)
        s1 = screen_pt(p1)
        s2 = screen_pt(p2)
        pts_str = f"{s0[0]:.1f},{s0[1]:.1f} {s1[0]:.1f},{s1[1]:.1f} {s2[0]:.1f},{s2[1]:.1f}"
        svg_lines.append(f'<polygon points="{pts_str}" fill="{fill_col}" stroke="{edge_col}" stroke-width="0.3"/>')

    svg_lines.append('</svg>')
    svg_content = "\n".join(svg_lines)

    svg_path = out_png.replace('.png', '.svg')
    with open(svg_path, 'w') as f:
        f.write(svg_content)

    subprocess.run(['sips', '-s', 'format', 'png', svg_path, '--out', out_png], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    print(f"Generated True CAD Render: {out_png}")


if __name__ == '__main__':
    base_dir = "/Users/aubreyhan/Tools/git_repo/PersonalDev/3D"
    asm_stl = os.path.join(base_dir, "ESP32_S3_AMOLED_GPS_Case_Assembly.stl")
    bot_stl = os.path.join(base_dir, "ESP32_S3_AMOLED_GPS_Case_Bottom.stl")

    tris_asm = load_stl(asm_stl)
    tris_bot = load_stl(bot_stl)

    # 1. Top Orthographic View
    render_view(tris_asm, os.path.join(base_dir, "CAD_Actual_Mesh_Top_View.png"),
                view_type='top', title="True CAD Geometry: Top Front View (Type-C at 9 o'clock, Opening at 3 o'clock)")

    # 2. Isometric 3D View (Right side showing 3 o'clock Unified PWR/TF/BOOT Opening)
    render_view(tris_asm, os.path.join(base_dir, "CAD_Actual_Mesh_Iso_Right_Cutout.png"),
                view_type='iso', title="True CAD Geometry: Right Side (3 o'clock) Unified Cutout (PWR / TF / BOOT)")

    # 3. Isometric 3D View (Left side showing 9 o'clock Type-C port & Mics)
    render_view(tris_asm, os.path.join(base_dir, "CAD_Actual_Mesh_Iso_Left_TypeC.png"),
                view_type='iso_left', title="True CAD Geometry: Left Side (9 o'clock) Type-C & Mic Ports")

    # 4. Bottom View (showing MagSafe ring and ports)
    render_view(tris_bot, os.path.join(base_dir, "CAD_Actual_Mesh_Bottom_View.png"),
                view_type='bottom', title="True CAD Geometry: Bottom View (MagSafe Recess & Base)")
