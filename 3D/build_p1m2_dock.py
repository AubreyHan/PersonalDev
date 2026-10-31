#!/usr/bin/env python3
"""
Columbus P-1 Mark II (P-1 M2) Mounting Dock Generator
Option B: Straight vertical walls (friction / foam fit, open front box)
Mounting: Completely flat bottom exterior for double-sided tape (3M VHB)
"""

import math
import struct
import os

# ==============================================================================
# Pure Python Mapbox Earcut Triangulation
# ==============================================================================

class Node:
    def __init__(self, i, x, y):
        self.i = i
        self.x = x
        self.y = y
        self.prev = None
        self.next = None
        self.z = 0
        self.prev_z = None
        self.next_z = None
        self.steiner = False

def earcut(data, hole_indices=None, dim=2):
    has_holes = hole_indices and len(hole_indices) > 0
    outer_len = hole_indices[0] * dim if has_holes else len(data)
    outer_node = linked_list(data, 0, outer_len, dim, True)
    triangles = []

    if not outer_node or outer_node.next == outer_node.prev:
        return triangles

    if has_holes:
        outer_node = eliminate_holes(data, hole_indices, outer_node, dim)

    earcut_linked(outer_node, triangles, dim)
    return triangles

def linked_list(data, start, end, dim, clockwise):
    sum_val = 0
    i = start
    j = end - dim
    while i < end:
        sum_val += (data[j] - data[i]) * (data[i + 1] + data[j + 1])
        j = i
        i += dim

    last = None
    if (sum_val > 0) == clockwise:
        for i in range(start, end, dim):
            last = insert_node(i // dim, data[i], data[i + 1], last)
    else:
        for i in range(end - dim, start - dim, -dim):
            last = insert_node(i // dim, data[i], data[i + 1], last)

    if last and equals(last, last.next):
        remove_node(last)
        last = last.next

    return last

def insert_node(i, x, y, last):
    p = Node(i, x, y)
    if not last:
        p.prev = p
        p.next = p
    else:
        p.next = last.next
        p.prev = last
        last.next.prev = p
        last.next = p
    return p

def remove_node(p):
    p.next.prev = p.prev
    p.prev.next = p.next
    if p.prev_z:
        p.prev_z.next_z = p.next_z
    if p.next_z:
        p.next_z.prev_z = p.prev_z

def eliminate_holes(data, hole_indices, outer_node, dim):
    queue = []
    len_holes = len(hole_indices)
    for i in range(len_holes):
        start = hole_indices[i] * dim
        end = hole_indices[i + 1] * dim if i < len_holes - 1 else len(data)
        list_node = linked_list(data, start, end, dim, False)
        if list_node == list_node.next:
            list_node.steiner = True
        queue.append(get_leftmost(list_node))

    queue.sort(key=lambda node: node.x)

    for hole in queue:
        outer_node = eliminate_hole(hole, outer_node)

    return outer_node

def eliminate_hole(hole, outer_node):
    bridge = find_hole_bridge(hole, outer_node)
    if not bridge:
        return outer_node

    bridge_reverse = split_polygon(bridge, hole)
    filter_points(bridge_reverse, bridge_reverse.next)
    return filter_points(bridge, bridge.next)

def find_hole_bridge(hole, outer_node):
    p = outer_node
    hx = hole.x
    hy = hole.y
    qx = -float('inf')
    m = None

    while True:
        if hy <= p.y and hy >= p.next.y and p.next.y != p.y:
            x = p.x + (hy - p.y) * (p.next.x - p.x) / (p.next.y - p.y)
            if x <= hx and x > qx:
                qx = x
                m = p if p.x > p.next.x else p.next
        p = p.next
        if p == outer_node:
            break

    if not m:
        return None

    stop = m
    mx = m.x
    my = m.y
    tan_min = float('inf')

    p = m
    while True:
        if hx >= p.x and p.x >= mx and hx != p.x and \
           point_in_triangle(hx if hy < my else qx, hy, mx, my, qx if hy < my else hx, hy, p.x, p.y):
            tan_val = abs(hy - p.y) / (hx - p.x)
            if locally_inside(p, hole) and \
               (tan_val < tan_min or (tan_val == tan_min and (p.x > m.x or (p.x == m.x and sector_contains_sector(m, p))))):
                m = p
                tan_min = tan_val
        p = p.next
        if p == stop:
            break

    return m

def sector_contains_sector(m, p):
    return area(m.prev, m, p.prev) < 0 and area(p.next, m, m.next) < 0

def get_leftmost(start):
    p = start
    leftmost = start
    while True:
        if p.x < leftmost.x or (p.x == leftmost.x and p.y < leftmost.y):
            leftmost = p
        p = p.next
        if p == start:
            break
    return leftmost

def point_in_triangle(ax, ay, bx, by, cx, cy, px, py):
    return (cx - px) * (ay - py) >= (ax - px) * (cy - py) and \
           (ax - px) * (by - py) >= (bx - px) * (ay - py) and \
           (bx - px) * (cy - py) >= (cx - px) * (by - py)

def locally_inside(a, b):
    return area(a, b, a.next) >= 0 and area(a, a.prev, b) >= 0 if area(a.prev, a, a.next) < 0 \
        else area(a, b, a.prev) < 0 or area(a, a.next, b) < 0

def split_polygon(a, b):
    a2 = Node(a.i, a.x, a.y)
    b2 = Node(b.i, b.x, b.y)
    an = a.next
    bp = b.prev

    a.next = b
    b.prev = a

    a2.next = an
    an.prev = a2

    b2.next = a2
    a2.prev = b2

    bp.next = b2
    b2.prev = bp

    return b2

def filter_points(start, end=None):
    if not start:
        return start
    if not end:
        end = start

    p = start
    while True:
        again = False
        if not p.steiner and (equals(p, p.next) or area(p.prev, p, p.next) == 0):
            remove_node(p)
            p = end = p.prev
            if p == p.next:
                break
            again = True
        else:
            p = p.next
        if not (again or p != end):
            break

    return end

def earcut_linked(ear, triangles, dim, pass_num=0):
    if not ear:
        return

    stop = ear
    while ear.prev != ear.next:
        prev = ear.prev
        next_node = ear.next

        if is_ear(ear):
            triangles.extend([prev.i, ear.i, next_node.i])
            remove_node(ear)
            ear = next_node.next
            stop = next_node.next
            continue

        ear = next_node
        if ear == stop:
            if pass_num == 0:
                earcut_linked(filter_points(ear), triangles, dim, pass_num=1)
            elif pass_num == 1:
                earcut_linked(filter_points(ear), triangles, dim, pass_num=2)
            break

def is_ear(ear):
    a = ear.prev
    b = ear
    c = ear.next

    if area(a, b, c) >= 0:
        return False

    p = ear.next.next
    while p != ear.prev:
        if point_in_triangle(a.x, a.y, b.x, b.y, c.x, c.y, p.x, p.y) and area(p.prev, p, p.next) >= 0:
            return False
        p = p.next

    return True

def area(p, q, r):
    return (q.y - p.y) * (r.x - q.x) - (q.x - p.x) * (r.y - q.y)

def equals(p1, p2):
    return p1.x == p2.x and p1.y == p2.y


# ==============================================================================
# STL Mesh Writer & Manifold Checker
# ==============================================================================

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

    def check_manifold(self):
        edges = {}
        for tri in self.triangles:
            for i in range(3):
                p0 = (round(tri[i][0], 4), round(tri[i][1], 4), round(tri[i][2], 4))
                p1 = (round(tri[(i+1)%3][0], 4), round(tri[(i+1)%3][1], 4), round(tri[(i+1)%3][2], 4))
                edges[(p0, p1)] = edges.get((p0, p1), 0) + 1

        unmatched = 0
        for (p0, p1), count in edges.items():
            rev = edges.get((p1, p0), 0)
            if count != 1 or rev != 1:
                unmatched += 1
        is_ok = (unmatched == 0)
        return is_ok, len(self.triangles), unmatched

    def save_binary(self, filepath, name="dock"):
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


# ==============================================================================
# Model Geometry Generation
# ==============================================================================

def make_rounded_rect_pts(w, h, r, n_arc=8):
    """Generates 2D points for a rounded rectangle in CCW order."""
    x_half = w / 2.0 - r
    y_half = h / 2.0 - r
    pts = []
    # 4 corners: bottom-right, top-right, top-left, bottom-left
    corners = [
        ( x_half, -y_half, -math.pi/2, 0),
        ( x_half,  y_half, 0, math.pi/2),
        (-x_half,  y_half, math.pi/2, math.pi),
        (-x_half, -y_half, math.pi, 3*math.pi/2)
    ]
    for cx, cy, a_start, a_end in corners:
        for i in range(n_arc):
            ang = a_start + (a_end - a_start) * i / float(n_arc)
            pts.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
    return pts

def triangulate_and_add(stl, polygon_pts, z, normal_up=True):
    flat_data = []
    for p in polygon_pts:
        flat_data.extend([p[0], p[1]])
    indices = earcut(flat_data)
    for i in range(0, len(indices), 3):
        i0, i1, i2 = indices[i], indices[i+1], indices[i+2]
        p0 = (polygon_pts[i0][0], polygon_pts[i0][1], z)
        p1 = (polygon_pts[i1][0], polygon_pts[i1][1], z)
        p2 = (polygon_pts[i2][0], polygon_pts[i2][1], z)
        if normal_up:
            stl.add_triangle(p0, p1, p2)
        else:
            stl.add_triangle(p0, p2, p1)

def add_wall_quads(stl, p_start, p_end, z_bot, z_top):
    """Wall quad facing right of vector p_start -> p_end"""
    v0 = (p_start[0], p_start[1], z_bot)
    v1 = (p_end[0],   p_end[1],   z_bot)
    v2 = (p_end[0],   p_end[1],   z_top)
    v3 = (p_start[0], p_start[1], z_top)
    stl.add_quad(v0, v1, v2, v3)

def build_model():
    stl = STLWriter()

    # Dimensions
    dev_w = 55.0
    dev_h = 85.0
    dev_d = 17.7

    fit_tol = 0.30
    cav_w = dev_w + 2 * fit_tol  # 55.6
    cav_h = dev_h + 2 * fit_tol  # 85.6
    cav_d = dev_d + fit_tol      # 18.0
    r_in  = 4.5

    wall = 2.4
    floor = 2.4

    out_w = cav_w + 2 * wall     # 60.4
    out_h = cav_h + 2 * wall     # 90.4
    out_d = floor + cav_d        # 20.4
    r_out = r_in + wall          # 6.9

    z0 = 0.0
    z_floor = floor              # 2.4
    z_notch = 9.0                # floor of left finger notch
    z_top = out_d                # 20.4

    # Cutouts
    # Top cutout (Power button): X in [-11.0, +11.0]
    top_cut_w = 22.0
    x_tc_l = -top_cut_w / 2.0
    x_tc_r =  top_cut_w / 2.0

    # Right cutout (Function button FN): from 20.0 to 37.0 from cavity top
    # Cavity top is Y = cav_h / 2 = 42.8
    y_fn_top = (cav_h / 2.0) - 20.0  # +22.8
    y_fn_bot = (cav_h / 2.0) - 37.0  # +5.8

    # Bottom cutout (MicroUSB / Rubber seal): X in [-19.0, +19.0]
    bot_cut_w = 38.0
    x_bc_l = -bot_cut_w / 2.0
    x_bc_r =  bot_cut_w / 2.0

    # Left finger notch: Y in [-10.0, +10.0]
    notch_w = 20.0
    y_fnl_bot = -notch_w / 2.0       # -10.0
    y_fnl_top =  notch_w / 2.0       # +10.0

    # 1. Bottom Exterior Face (Z = 0)
    # Perfectly flat rounded rectangle facing -Z
    outer_pts_ccw = make_rounded_rect_pts(out_w, out_h, r_out, n_arc=12)
    triangulate_and_add(stl, outer_pts_ccw, z0, normal_up=False)

    # 2. Cavity Floor + Cutouts Floor (Z = z_floor = 2.4)
    # The floor at Z = 2.4 covers the cavity plus the 3 through-wall cutouts
    # We construct the 2D boundary of the floor at Z = 2.4:
    # Outer boundaries at the cutouts reach the outer shell!
    # Let's construct this floor polygon in CCW order:
    inner_pts_ccw = make_rounded_rect_pts(cav_w, cav_h, r_in, n_arc=12)
    
    # Floor polygon at Z=2.4 is inner cavity + top cutout + right cutout + bottom cutout
    # Let's trace it precisely:
    # Start at top cutout right edge: (x_tc_r, out_h/2)
    x_half_out = out_w / 2.0
    y_half_out = out_h / 2.0
    x_half_in  = cav_w / 2.0
    y_half_in  = cav_h / 2.0

    # Let's subdivide into 3 clear regions for 100% robust triangulation:
    # Region 1: Inner cavity floor (rounded rectangle: cav_w x cav_h, r_in)
    triangulate_and_add(stl, inner_pts_ccw, z_floor, normal_up=True)

    # Region 2: Top cutout floor rectangle: [x_tc_l, x_tc_r] x [y_half_in, y_half_out]
    p_tc = [
        (x_tc_l, y_half_in),
        (x_tc_r, y_half_in),
        (x_tc_r, y_half_out),
        (x_tc_l, y_half_out)
    ]
    triangulate_and_add(stl, p_tc, z_floor, normal_up=True)

    # Region 3: Right cutout floor rectangle: [x_half_in, x_half_out] x [y_fn_bot, y_fn_top]
    p_rc = [
        (x_half_in,  y_fn_bot),
        (x_half_out, y_fn_bot),
        (x_half_out, y_fn_top),
        (x_half_in,  y_fn_top)
    ]
    triangulate_and_add(stl, p_rc, z_floor, normal_up=True)

    # Region 4: Bottom cutout floor rectangle: [x_bc_l, x_bc_r] x [-y_half_out, -y_half_in]
    p_bc = [
        (x_bc_l, -y_half_out),
        (x_bc_r, -y_half_out),
        (x_bc_r, -y_half_in),
        (x_bc_l, -y_half_in)
    ]
    triangulate_and_add(stl, p_bc, z_floor, normal_up=True)

    # Region 5: Left finger notch step at Z = z_notch (9.0):
    # From X = [-x_half_out, -x_half_in] x [y_fnl_bot, y_fnl_top]
    p_ln = [
        (-x_half_out, y_fnl_bot),
        (-x_half_in,  y_fnl_bot),
        (-x_half_in,  y_fnl_top),
        (-x_half_out, y_fnl_top)
    ]
    triangulate_and_add(stl, p_ln, z_notch, normal_up=True)

    # 3. Outer Vertical Walls (from Z=0 to Z_top or cutout)
    # The outer perimeter consists of segments that go either to z_floor (at cutouts)
    # or to z_top (everywhere else).
    # Let's discretize outer perimeter with high resolution:
    # A point is in a cutout if:
    # - Top cutout: Y == y_half_out and x_tc_l <= X <= x_tc_r -> wall height = z_floor
    # - Right cutout: X == x_half_out and y_fn_bot <= Y <= y_fn_top -> wall height = z_floor
    # - Bottom cutout: Y == -y_half_out and x_bc_l <= X <= x_bc_r -> wall height = z_floor
    # - Left finger notch: X == -x_half_out and y_fnl_bot <= Y <= y_fnl_top -> wall height = z_notch

    # Build outer segments:
    # We construct the 4 walls and 4 corners explicitly:
    cx_out = x_half_out - r_out
    cy_out = y_half_out - r_out
    n_corner = 12

    # Corner arcs (all reach z_top):
    # TR: from 0 to pi/2
    arc_tr = [(cx_out + r_out * math.cos(i*math.pi/(2*n_corner)), cy_out + r_out * math.sin(i*math.pi/(2*n_corner))) for i in range(n_corner+1)]
    # TL: from pi/2 to pi
    arc_tl = [(-cx_out + r_out * math.cos(math.pi/2 + i*math.pi/(2*n_corner)), cy_out + r_out * math.sin(math.pi/2 + i*math.pi/(2*n_corner))) for i in range(n_corner+1)]
    # BL: from pi to 3*pi/2
    arc_bl = [(-cx_out + r_out * math.cos(math.pi + i*math.pi/(2*n_corner)), -cy_out + r_out * math.sin(math.pi + i*math.pi/(2*n_corner))) for i in range(n_corner+1)]
    # BR: from 3*pi/2 to 2*pi
    arc_br = [(cx_out + r_out * math.cos(3*math.pi/2 + i*math.pi/(2*n_corner)), -cy_out + r_out * math.sin(3*math.pi/2 + i*math.pi/(2*n_corner))) for i in range(n_corner+1)]

    # Top outer wall: from (-cx_out, y_half_out) to (cx_out, y_half_out)
    # Contains top cutout [x_tc_l, x_tc_r]
    # Seg 1: (-cx_out, y_half_out) -> (x_tc_l, y_half_out), height: z0 -> z_top
    add_wall_quads(stl, (-cx_out, y_half_out), (x_tc_l, y_half_out), z0, z_top)
    # Seg 2 (cutout edge): (x_tc_l, y_half_out) -> (x_tc_r, y_half_out), height: z0 -> z_floor
    add_wall_quads(stl, (x_tc_l, y_half_out), (x_tc_r, y_half_out), z0, z_floor)
    # Seg 3: (x_tc_r, y_half_out) -> (cx_out, y_half_out), height: z0 -> z_top
    add_wall_quads(stl, (x_tc_r, y_half_out), (cx_out, y_half_out), z0, z_top)

    # TR corner arc:
    for i in range(n_corner):
        add_wall_quads(stl, arc_tr[i], arc_tr[i+1], z0, z_top)

    # Right outer wall: from (x_half_out, cy_out) to (x_half_out, -cy_out)
    # Contains right cutout [y_fn_bot, y_fn_top]
    # Seg 1: (x_half_out, cy_out) -> (x_half_out, y_fn_top), height: z0 -> z_top
    add_wall_quads(stl, (x_half_out, cy_out), (x_half_out, y_fn_top), z0, z_top)
    # Seg 2 (cutout edge): (x_half_out, y_fn_top) -> (x_half_out, y_fn_bot), height: z0 -> z_floor
    add_wall_quads(stl, (x_half_out, y_fn_top), (x_half_out, y_fn_bot), z0, z_floor)
    # Seg 3: (x_half_out, y_fn_bot) -> (x_half_out, -cy_out), height: z0 -> z_top
    add_wall_quads(stl, (x_half_out, y_fn_bot), (x_half_out, -cy_out), z0, z_top)

    # BR corner arc:
    for i in range(n_corner):
        add_wall_quads(stl, arc_br[i], arc_br[i+1], z0, z_top)

    # Bottom outer wall: from (cx_out, -y_half_out) to (-cx_out, -y_half_out)
    # Contains bottom cutout [x_bc_l, x_bc_r]
    # Seg 1: (cx_out, -y_half_out) -> (x_bc_r, -y_half_out), height: z0 -> z_top
    add_wall_quads(stl, (cx_out, -y_half_out), (x_bc_r, -y_half_out), z0, z_top)
    # Seg 2 (cutout edge): (x_bc_r, -y_half_out) -> (x_bc_l, -y_half_out), height: z0 -> z_floor
    add_wall_quads(stl, (x_bc_r, -y_half_out), (x_bc_l, -y_half_out), z0, z_floor)
    # Seg 3: (x_bc_l, -y_half_out) -> (-cx_out, -y_half_out), height: z0 -> z_top
    add_wall_quads(stl, (x_bc_l, -y_half_out), (-cx_out, -y_half_out), z0, z_top)

    # BL corner arc:
    for i in range(n_corner):
        add_wall_quads(stl, arc_bl[i], arc_bl[i+1], z0, z_top)

    # Left outer wall: from (-x_half_out, -cy_out) to (-x_half_out, cy_out)
    # Contains left finger notch [y_fnl_bot, y_fnl_top], height goes to z_notch
    # Seg 1: (-x_half_out, -cy_out) -> (-x_half_out, y_fnl_bot), height: z0 -> z_top
    add_wall_quads(stl, (-x_half_out, -cy_out), (-x_half_out, y_fnl_bot), z0, z_top)
    # Seg 2 (finger notch edge): (-x_half_out, y_fnl_bot) -> (-x_half_out, y_fnl_top), height: z0 -> z_notch
    add_wall_quads(stl, (-x_half_out, y_fnl_bot), (-x_half_out, y_fnl_top), z0, z_notch)
    # Seg 3: (-x_half_out, y_fnl_top) -> (-x_half_out, cy_out), height: z0 -> z_top
    add_wall_quads(stl, (-x_half_out, y_fnl_top), (-x_half_out, cy_out), z0, z_top)

    # TL corner arc:
    for i in range(n_corner):
        add_wall_quads(stl, arc_tl[i], arc_tl[i+1], z0, z_top)

    # 4. Inner Cavity Walls (Facing inward, so vector order reversed)
    cx_in = x_half_in - r_in
    cy_in = y_half_in - r_in

    arc_in_tr = [(cx_in + r_in * math.cos(i*math.pi/(2*n_corner)), cy_in + r_in * math.sin(i*math.pi/(2*n_corner))) for i in range(n_corner+1)]
    arc_in_tl = [(-cx_in + r_in * math.cos(math.pi/2 + i*math.pi/(2*n_corner)), cy_in + r_in * math.sin(math.pi/2 + i*math.pi/(2*n_corner))) for i in range(n_corner+1)]
    arc_in_bl = [(-cx_in + r_in * math.cos(math.pi + i*math.pi/(2*n_corner)), -cy_in + r_in * math.sin(math.pi + i*math.pi/(2*n_corner))) for i in range(n_corner+1)]
    arc_in_br = [(cx_in + r_in * math.cos(3*math.pi/2 + i*math.pi/(2*n_corner)), -cy_in + r_in * math.sin(3*math.pi/2 + i*math.pi/(2*n_corner))) for i in range(n_corner+1)]

    # Inner walls go from z_floor to z_top (CW direction so normal faces inward)
    # Top inner wall: from (cx_in, y_half_in) to (-cx_in, y_half_in)
    # Has top cutout [x_tc_l, x_tc_r] which is OPEN!
    add_wall_quads(stl, (cx_in, y_half_in), (x_tc_r, y_half_in), z_floor, z_top)
    add_wall_quads(stl, (x_tc_l, y_half_in), (-cx_in, y_half_in), z_floor, z_top)

    # TL corner inner: from (-cx_in, y_half_in) to (-x_half_in, cy_in)
    for i in range(n_corner, 0, -1):
        add_wall_quads(stl, arc_in_tl[i], arc_in_tl[i-1], z_floor, z_top)

    # Left inner wall: from (-x_half_in, cy_in) to (-x_half_in, -cy_in)
    # Left notch is from y_fnl_bot to y_fnl_top, top rim drops to z_notch
    add_wall_quads(stl, (-x_half_in, cy_in), (-x_half_in, y_fnl_top), z_floor, z_top)
    add_wall_quads(stl, (-x_half_in, y_fnl_top), (-x_half_in, y_fnl_bot), z_floor, z_notch)
    add_wall_quads(stl, (-x_half_in, y_fnl_bot), (-x_half_in, -cy_in), z_floor, z_top)

    # BL corner inner: from (-x_half_in, -cy_in) to (-cx_in, -y_half_in)
    for i in range(n_corner, 0, -1):
        add_wall_quads(stl, arc_in_bl[i], arc_in_bl[i-1], z_floor, z_top)

    # Bottom inner wall: from (-cx_in, -y_half_in) to (cx_in, -y_half_in)
    # Has bottom cutout [x_bc_l, x_bc_r]
    add_wall_quads(stl, (-cx_in, -y_half_in), (x_bc_l, -y_half_in), z_floor, z_top)
    add_wall_quads(stl, (x_bc_r, -y_half_in), (cx_in, -y_half_in), z_floor, z_top)

    # BR corner inner: from (cx_in, -y_half_in) to (x_half_in, -cy_in)
    for i in range(n_corner, 0, -1):
        add_wall_quads(stl, arc_in_br[i], arc_in_br[i-1], z_floor, z_top)

    # Right inner wall: from (x_half_in, -cy_in) to (x_half_in, cy_in)
    # Has right cutout [y_fn_bot, y_fn_top]
    add_wall_quads(stl, (x_half_in, -cy_in), (x_half_in, y_fn_bot), z_floor, z_top)
    add_wall_quads(stl, (x_half_in, y_fn_top), (x_half_in, cy_in), z_floor, z_top)

    # TR corner inner: from (x_half_in, cy_in) to (cx_in, y_half_in)
    for i in range(n_corner, 0, -1):
        add_wall_quads(stl, arc_in_tr[i], arc_in_tr[i-1], z_floor, z_top)

    # 5. Cutout End Faces (Vertical end walls closing the thickness of the wall)
    # Top cutout ends:
    # Left end at x_tc_l: from y_half_in to y_half_out, z_floor to z_top
    add_wall_quads(stl, (x_tc_l, y_half_in), (x_tc_l, y_half_out), z_floor, z_top)
    # Right end at x_tc_r: from y_half_out to y_half_in, z_floor to z_top
    add_wall_quads(stl, (x_tc_r, y_half_out), (x_tc_r, y_half_in), z_floor, z_top)

    # Right cutout ends:
    # Top end at y_fn_top: from x_half_out to x_half_in, z_floor to z_top
    add_wall_quads(stl, (x_half_out, y_fn_top), (x_half_in, y_fn_top), z_floor, z_top)
    # Bottom end at y_fn_bot: from x_half_in to x_half_out, z_floor to z_top
    add_wall_quads(stl, (x_half_in, y_fn_bot), (x_half_out, y_fn_bot), z_floor, z_top)

    # Bottom cutout ends:
    # Right end at x_bc_r: from -y_half_in to -y_half_out, z_floor to z_top
    add_wall_quads(stl, (x_bc_r, -y_half_in), (x_bc_r, -y_half_out), z_floor, z_top)
    # Left end at x_bc_l: from -y_half_out to -y_half_in, z_floor to z_top
    add_wall_quads(stl, (x_bc_l, -y_half_out), (x_bc_l, -y_half_in), z_floor, z_top)

    # Left finger notch ends (from z_notch to z_top):
    # Bottom end at y_fnl_bot: from -x_half_out to -x_half_in
    add_wall_quads(stl, (-x_half_out, y_fnl_bot), (-x_half_in, y_fnl_bot), z_notch, z_top)
    # Top end at y_fnl_top: from -x_half_in to -x_half_out
    add_wall_quads(stl, (-x_half_in, y_fnl_top), (-x_half_out, y_fnl_top), z_notch, z_top)

    # 6. Top Rim Horizontal Faces (At Z = z_top)
    # The top rim consists of 3 distinct disconnected wall sections:
    # Section A: Top-Right Corner & Upper Right Wall:
    # Outer: from (x_tc_r, y_half_out) along arc_tr to (x_half_out, y_fn_top)
    # Inner: from (x_half_in, y_fn_top) along inner arc_in_tr to (x_tc_r, y_half_in)
    poly_tr = [(x_tc_r, y_half_out)] + [(cx_out, y_half_out)] + arc_tr + [(x_half_out, y_fn_top)]
    poly_tr += [(x_half_in, y_fn_top)] + [(x_half_in, cy_in)]
    for i in range(n_corner, -1, -1):
        poly_tr.append(arc_in_tr[i])
    poly_tr += [(x_tc_r, y_half_in)]
    triangulate_and_add(stl, poly_tr, z_top, normal_up=True)

    # Section B: Bottom-Right Corner & Lower Right Wall:
    # Outer: from (x_half_out, y_fn_bot) along outer BR arc to (x_bc_r, -y_half_out)
    # Inner: from (x_bc_r, -y_half_in) along inner BR arc to (x_half_in, y_fn_bot)
    poly_br = [(x_half_out, y_fn_bot)] + [(x_half_out, -cy_out)] + arc_br + [(x_bc_r, -y_half_out)]
    poly_br += [(x_bc_r, -y_half_in)] + [(cx_in, -y_half_in)]
    for i in range(n_corner, -1, -1):
        poly_br.append(arc_in_br[i])
    poly_br += [(x_half_in, y_fn_bot)]
    triangulate_and_add(stl, poly_br, z_top, normal_up=True)

    # Section C1: Left Wall Upper Half & Top-Left Corner:
    # Outer: from (-x_half_out, y_fnl_top) to (-x_half_out, cy_out) along arc_tl to (x_tc_l, y_half_out)
    # Inner: from (x_tc_l, y_half_in) along arc_in_tl to (-x_half_in, y_fnl_top)
    poly_tl = [(-x_half_out, y_fnl_top)] + [(-x_half_out, cy_out)] + arc_tl + [(x_tc_l, y_half_out)]
    poly_tl += [(x_tc_l, y_half_in)] + [(-cx_in, y_half_in)]
    for i in range(n_corner, -1, -1):
        poly_tl.append(arc_in_tl[i])
    poly_tl += [(-x_half_in, y_fnl_top)]
    triangulate_and_add(stl, poly_tl, z_top, normal_up=True)

    # Section C2: Left Wall Lower Half & Bottom-Left Corner:
    # Outer: from (x_bc_l, -y_half_out) along arc_bl to (-x_half_out, y_fnl_bot)
    # Inner: from (-x_half_in, y_fnl_bot) along arc_in_bl to (x_bc_l, -y_half_in)
    poly_bl = [(x_bc_l, -y_half_out)] + [(-cx_out, -y_half_out)] + arc_bl + [(-x_half_out, y_fnl_bot)]
    poly_bl += [(-x_half_in, y_fnl_bot)] + [(-x_half_in, -cy_in)]
    for i in range(n_corner, -1, -1):
        poly_bl.append(arc_in_bl[i])
    poly_bl += [(x_bc_l, -y_half_in)]
    triangulate_and_add(stl, poly_bl, z_top, normal_up=True)

    return stl

if __name__ == '__main__':
    base_dir = "/Users/aubreyhan/Tools/git_repo/PersonalDev/3D"
    stl_path = os.path.join(base_dir, "Columbus_P1M2_Dock.stl")
    stl = build_model()
    is_ok, n_tri, unmatched = stl.check_manifold()
    print(f"Manifold check: is_ok={is_ok}, triangles={n_tri}, unmatched_edges={unmatched}")
    stl.save_binary(stl_path, name="P1M2_Dock")
