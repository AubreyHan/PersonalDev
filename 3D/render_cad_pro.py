#!/usr/bin/env python3
"""
High-Precision CAD 3D Multi-View Renderer with Strict 1:1 Proportions
Renders:
1. Isometric View (Showing Antenna Bay, Bezel, Type-C, Panoramic Window)
2. Exploded / Separation View (Showing Body and Threaded Lid)
3. Top View (Orthographic with dimension annotations)
4. Bottom / Rear View (Showing Threaded Lid knurling & coin slot)
"""

import math
import struct
import os
import subprocess

def load_stl(path):
    triangles = []
    with open(path, 'rb') as f:
        f.seek(80)
        n = struct.unpack('<I', f.read(4))[0]
        for _ in range(n):
            data = f.read(50)
            v = struct.unpack('<12fH', data)
            triangles.append((
                v[0:3],
                v[3:6],
                v[6:9],
                v[9:12]
            ))
    return triangles

out_dir = '/Users/aubreyhan/Tools/git_repo/PersonalDev/3D'
body_path = os.path.join(out_dir, 'ESP32_S3_AMOLED_Body_Threaded.stl')
lid_path = os.path.join(out_dir, 'ESP32_S3_AMOLED_Lid_Threaded.stl')

body_tris = load_stl(body_path)
lid_tris = load_stl(lid_path)

def render_scene(objects, out_png, az_deg=35, el_deg=32, width=1600, height=1200, title="CAD Render", annotations=[]):
    az = math.radians(az_deg)
    el = math.radians(el_deg)
    cos_az, sin_az = math.cos(az), math.sin(az)
    cos_el, sin_el = math.cos(el), math.sin(el)
    
    # Dual-light setup for realistic CAD shading
    l1 = (0.5, 0.4, 0.8)
    l1_len = math.sqrt(sum(x**2 for x in l1))
    l1 = [x/l1_len for x in l1]
    
    l2 = (-0.6, -0.3, 0.4)
    l2_len = math.sqrt(sum(x**2 for x in l2))
    l2 = [x/l2_len for x in l2]

    def project(v):
        x1 = v[0] * cos_az - v[1] * sin_az
        y1 = v[0] * sin_az + v[1] * cos_az
        z1 = v[2]
        tilt = math.pi/2.0 - el
        cos_t, sin_t = math.cos(tilt), math.sin(tilt)
        x2 = x1
        y2 = y1 * cos_t - z1 * sin_t
        z2 = y1 * sin_t + z1 * cos_t
        return x2, y2, z2

    proj = []
    min_px, max_px = float('inf'), float('-inf')
    min_py, max_py = float('inf'), float('-inf')

    for triangles, color_hex, offset in objects:
        cr = int(color_hex[1:3], 16)
        cg = int(color_hex[3:5], 16)
        cb = int(color_hex[5:7], 16)
        for n, v0, v1, v2 in triangles:
            # apply offset
            p0 = project((v0[0]+offset[0], v0[1]+offset[1], v0[2]+offset[2]))
            p1 = project((v1[0]+offset[0], v1[1]+offset[1], v1[2]+offset[2]))
            p2 = project((v2[0]+offset[0], v2[1]+offset[1], v2[2]+offset[2]))
            
            # Backface culling
            cp = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0])
            if cp <= 0:
                continue
            
            avg_z = (p0[2] + p1[2] + p2[2]) / 3.0
            nx, ny, nz = n
            nl = math.hypot(nx, ny, nz)
            if nl > 1e-6:
                nx, ny, nz = nx/nl, ny/nl, nz/nl
                dot1 = max(0.0, nx*l1[0] + ny*l1[1] + nz*l1[2])
                dot2 = max(0.0, nx*l2[0] + ny*l2[1] + nz*l2[2])
                intensity = 0.28 + 0.58 * dot1 + 0.14 * dot2
            else:
                intensity = 0.5
            
            min_px = min(min_px, p0[0], p1[0], p2[0]); max_px = max(max_px, p0[0], p1[0], p2[0])
            min_py = min(min_py, p0[1], p1[1], p2[1]); max_py = max(max_py, p0[1], p1[1], p2[1])
            proj.append((avg_z, p0, p1, p2, intensity, (cr, cg, cb)))

    proj.sort(key=lambda x: x[0])
    
    # Strict 1:1 Aspect Ratio Isotropic Scaling
    margin = 120
    span_x = max_px - min_px
    span_y = max_py - min_py
    scale = min((width - 2*margin) / span_x, (height - 2*margin) / span_y)
    cx, cy = (min_px + max_px) / 2.0, (min_py + max_py) / 2.0

    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" height="{height}">']
    # Background gradient
    svg.append('<defs>')
    svg.append('  <radialGradient id="bg" cx="50%" cy="50%" r="75%">')
    svg.append('    <stop offset="0%" stop-color="#1e293b"/>')
    svg.append('    <stop offset="100%" stop-color="#090d16"/>')
    svg.append('  </radialGradient>')
    svg.append('  <linearGradient id="headerGrad" x1="0" y1="0" x2="1" y2="0">')
    svg.append('    <stop offset="0%" stop-color="#38bdf8"/>')
    svg.append('    <stop offset="100%" stop-color="#818cf8"/>')
    svg.append('  </linearGradient>')
    svg.append('</defs>')
    svg.append('<rect width="100%" height="100%" fill="url(#bg)"/>')
    
    # Grid lines for engineering aesthetics
    for gx in range(0, width, 80):
        svg.append(f'<line x1="{gx}" y1="0" x2="{gx}" y2="{height}" stroke="#334155" stroke-width="0.5" stroke-dasharray="3,6" opacity="0.3"/>')
    for gy in range(0, height, 80):
        svg.append(f'<line x1="0" y1="{gy}" x2="{width}" y2="{gy}" stroke="#334155" stroke-width="0.5" stroke-dasharray="3,6" opacity="0.3"/>')

    # Title header
    svg.append(f'<text x="60" y="70" fill="url(#headerGrad)" font-family="-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif" font-size="28" font-weight="800" letter-spacing="1">{title}</text>')
    svg.append(f'<text x="60" y="100" fill="#94a3b8" font-family="-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif" font-size="14" font-weight="500">True 1:1 Scale CAD Geometry • M52×2.0 Precision Thread • 12h GNSS Pod with Wire Channel</text>')

    # Polygons
    for avg_z, p0, p1, p2, it, (cr, cg, cb) in proj:
        sx0 = (p0[0] - cx) * scale + width/2
        sy0 = height/2 - (p0[1] - cy) * scale
        sx1 = (p1[0] - cx) * scale + width/2
        sy1 = height/2 - (p1[1] - cy) * scale
        sx2 = (p2[0] - cx) * scale + width/2
        sy2 = height/2 - (p2[1] - cy) * scale

        r_c = int(min(255, cr * it))
        g_c = int(min(255, cg * it))
        b_c = int(min(255, cb * it))
        edge_r = int(r_c * 0.6)
        edge_g = int(g_c * 0.6)
        edge_b = int(b_c * 0.6)

        svg.append(f'<polygon points="{sx0:.1f},{sy0:.1f} {sx1:.1f},{sy1:.1f} {sx2:.1f},{sy2:.1f}" fill="#{r_c:02x}{g_c:02x}{b_c:02x}" stroke="#{edge_r:02x}{edge_g:02x}{edge_b:02x}" stroke-width="0.4"/>')

    # Annotations
    for ann in annotations:
        target = project(ann['pos'])
        tx = (target[0] - cx) * scale + width/2
        ty = height/2 - (target[1] - cy) * scale
        lx = tx + ann.get('dx', 60)
        ly = ty + ann.get('dy', -40)
        
        # Leader line
        svg.append(f'<circle cx="{tx:.1f}" cy="{ty:.1f}" r="4" fill="#38bdf8" stroke="#ffffff" stroke-width="1.5"/>')
        svg.append(f'<polyline points="{tx:.1f},{ty:.1f} {lx:.1f},{ly:.1f} {lx+120:.1f},{ly:.1f}" fill="none" stroke="#38bdf8" stroke-width="1.5"/>')
        svg.append(f'<rect x="{lx:.1f}" y="{ly-24:.1f}" width="150" height="22" rx="4" fill="#0f172a" stroke="#38bdf8" stroke-width="1" opacity="0.9"/>')
        svg.append(f'<text x="{lx+8:.1f}" y="{ly-8:.1f}" fill="#f8fafc" font-family="-apple-system,BlinkMacSystemFont,sans-serif" font-size="12" font-weight="700">{ann["text"]}</text>')

    svg.append('</svg>')
    
    svg_path = out_png.replace('.png', '.svg')
    with open(svg_path, 'w') as f:
        f.write('\n'.join(svg))
    subprocess.run(['sips', '-s', 'format', 'png', svg_path, '--out', out_png], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Generated {out_png}")

# 1. Hero Isometric View (Assembly)
render_scene(
    [(body_tris, '#38bdf8', (0, 0, 0)), (lid_tris, '#818cf8', (0, 0, 0))],
    os.path.join(out_dir, 'Threaded_Case_Hero_Iso_1to1.png'),
    az_deg=40, el_deg=32,
    title="ESP32-S3-AMOLED-1.75-G 螺纹旋合外壳 (1:1 真实比例三维效果图)",
    annotations=[
        {"pos": (0, 36, 6), "text": "12点钟 25mm 天线仓", "dx": -180, "dy": -70},
        {"pos": (0, 24, 5), "text": "内部 3.6×3mm 穿线孔", "dx": 70, "dy": -60},
        {"pos": (-25, 0, 7), "text": "9点钟 Type-C 接口", "dx": -180, "dy": 30},
        {"pos": (25, 0, 8), "text": "3点钟 90°按键大开窗", "dx": 70, "dy": 30},
        {"pos": (0, -26, -1), "text": "M52 螺纹后盖/滚花外圈", "dx": -180, "dy": 70}
    ]
)

# 2. Exploded View (Body lifted, Threaded Lid separated below)
render_scene(
    [(body_tris, '#38bdf8', (0, 0, 15)), (lid_tris, '#a855f7', (0, 0, -10))],
    os.path.join(out_dir, 'Threaded_Case_Exploded_View_1to1.png'),
    az_deg=35, el_deg=28,
    title="螺纹旋合结构爆炸示意图 (Exploded View)",
    annotations=[
        {"pos": (0, 0, 15), "text": "主体舱 (内螺纹 M52)", "dx": 100, "dy": -30},
        {"pos": (0, 0, -6), "text": "后盖 (M52 外螺纹公头)", "dx": 100, "dy": 40},
        {"pos": (0, -25, -12), "text": "36齿防滑滚花外圈", "dx": -180, "dy": 60}
    ]
)

# 3. Top-Down Orthographic View (1:1 True Front Face)
render_scene(
    [(body_tris, '#38bdf8', (0, 0, 0))],
    os.path.join(out_dir, 'Threaded_Case_Top_View_1to1.png'),
    az_deg=0, el_deg=89.9,
    title="正面顶视精准比例图 (Top Orthographic View)",
    annotations=[
        {"pos": (0, 38, 10), "text": "天线仓 25.6×24.4mm", "dx": -190, "dy": -40},
        {"pos": (0, 0, 13), "text": "AMOLED 视窗 Ø44.2mm", "dx": 90, "dy": 40},
        {"pos": (-26, 0, 7), "text": "Type-C 避空口", "dx": -170, "dy": 20},
        {"pos": (26, 0, 7), "text": "侧边 90°全景开窗", "dx": 80, "dy": -20}
    ]
)

# 4. Bottom View (Rear Lid & Coin Slot)
render_scene(
    [(lid_tris, '#a855f7', (0, 0, 0)), (body_tris, '#38bdf8', (0, 0, 0))],
    os.path.join(out_dir, 'Threaded_Case_Bottom_View_1to1.png'),
    az_deg=0, el_deg=-89.9,
    title="底部背视精准比例图 (Bottom Orthographic View)",
    annotations=[
        {"pos": (0, 0, -2), "text": "一字硬币旋合槽 24×2.4mm", "dx": 80, "dy": 40},
        {"pos": (0, 26, -2), "text": "36齿手拧防滑滚花外缘", "dx": -190, "dy": -40}
    ]
)
