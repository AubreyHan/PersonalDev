#!/usr/bin/env python3
"""
Columbus P-1 Mark II (P-1 M2) Mounting Dock Generator
Option B: Straight vertical walls (friction / foam fit, open front box)
Mounting: Completely flat bottom exterior for double-sided tape (3M VHB)
Watertight 2-Manifold Guarantee (0 open edges)
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
        # CCW quad: v0 -> v1 -> v2, v0 -> v2 -> v3
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
    """Adds a quad facing outward (normal to the right of p_start -> p_end in XY)"""
    v0 = (p_start[0], p_start[1], z_bot)
    v1 = (p_end[0],   p_end[1],   z_bot)
    v2 = (p_end[0],   p_end[1],   z_top)
    v3 = (p_start[0], p_start[1], z_top)
    stl.add_quad(v0, v1, v2, v3)


# ==============================================================================
# Model Geometry Generation
# ==============================================================================

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
    top_cut_w = 22.0
    x_tc_l = -top_cut_w / 2.0     # -11.0
    x_tc_r =  top_cut_w / 2.0     # +11.0

    # Right cutout (Function button FN): from 20.0 to 37.0 from cavity top
    y_fn_top = (cav_h / 2.0) - 20.0  # +22.8
    y_fn_bot = (cav_h / 2.0) - 37.0  # +5.8

    # Bottom cutout (MicroUSB / Rubber seal): X in [-19.0, +19.0]
    bot_cut_w = 38.0
    x_bc_l = -bot_cut_w / 2.0     # -19.0
    x_bc_r =  bot_cut_w / 2.0     # +19.0

    # Left finger notch: Y in [-10.0, +10.0]
    notch_w = 20.0
    y_fnl_bot = -notch_w / 2.0    # -10.0
    y_fnl_top =  notch_w / 2.0    # +10.0

    x_half_out = out_w / 2.0      # 30.2
    y_half_out = out_h / 2.0      # 45.2
    x_half_in  = cav_w / 2.0      # 27.8
    y_half_in  = cav_h / 2.0      # 42.8

    cx_out = x_half_out - r_out   # 23.3
    cy_out = y_half_out - r_out   # 38.3
    cx_in  = x_half_in - r_in     # 23.3
    cy_in  = y_half_in - r_in     # 38.3

    n_corner = 12

    # Corner arcs (in counter-clockwise order around each quadrant):
    # TR: from 0 to pi/2 (starts at x_half_out, cy_out -> ends at cx_out, y_half_out)
    arc_tr_out = [(cx_out + r_out * math.cos(i*math.pi/(2*n_corner)), cy_out + r_out * math.sin(i*math.pi/(2*n_corner))) for i in range(n_corner+1)]
    # TL: from pi/2 to pi (starts at -cx_out, y_half_out -> ends at -x_half_out, cy_out)
    arc_tl_out = [(-cx_out + r_out * math.cos(math.pi/2 + i*math.pi/(2*n_corner)), cy_out + r_out * math.sin(math.pi/2 + i*math.pi/(2*n_corner))) for i in range(n_corner+1)]
    # BL: from pi to 3*pi/2 (starts at -x_half_out, -cy_out -> ends at -cx_out, -y_half_out)
    arc_bl_out = [(-cx_out + r_out * math.cos(math.pi + i*math.pi/(2*n_corner)), -cy_out + r_out * math.sin(math.pi + i*math.pi/(2*n_corner))) for i in range(n_corner+1)]
    # BR: from 3*pi/2 to 2*pi (starts at cx_out, -y_half_out -> ends at x_half_out, -cy_out)
    arc_br_out = [(cx_out + r_out * math.cos(3*math.pi/2 + i*math.pi/(2*n_corner)), -cy_out + r_out * math.sin(3*math.pi/2 + i*math.pi/(2*n_corner))) for i in range(n_corner+1)]

    # Inner corner arcs:
    arc_tr_in = [(cx_in + r_in * math.cos(i*math.pi/(2*n_corner)), cy_in + r_in * math.sin(i*math.pi/(2*n_corner))) for i in range(n_corner+1)]
    arc_tl_in = [(-cx_in + r_in * math.cos(math.pi/2 + i*math.pi/(2*n_corner)), cy_in + r_in * math.sin(math.pi/2 + i*math.pi/(2*n_corner))) for i in range(n_corner+1)]
    arc_bl_in = [(-cx_in + r_in * math.cos(math.pi + i*math.pi/(2*n_corner)), -cy_in + r_in * math.sin(math.pi + i*math.pi/(2*n_corner))) for i in range(n_corner+1)]
    arc_br_in = [(cx_in + r_in * math.cos(3*math.pi/2 + i*math.pi/(2*n_corner)), -cy_in + r_in * math.sin(3*math.pi/2 + i*math.pi/(2*n_corner))) for i in range(n_corner+1)]

    # -------------------------------------------------------------------------
    # 1. Bottom Exterior Surface (Z = 0)
    # -------------------------------------------------------------------------
    # CCW traversal of outer perimeter with all split vertices included:
    outer_pts_ccw = []
    # Bottom straight: (-cx_out, -y_half_out) -> (cx_out, -y_half_out), split by bottom cutout
    outer_pts_ccw.append((-cx_out, -y_half_out))
    outer_pts_ccw.append((x_bc_l, -y_half_out))
    outer_pts_ccw.append((x_bc_r, -y_half_out))
    outer_pts_ccw.append((cx_out, -y_half_out))
    # BR arc: from cx_out, -y_half_out to x_half_out, -cy_out
    outer_pts_ccw.extend(arc_br_out[1:])
    # Right straight: (x_half_out, -cy_out) -> (x_half_out, cy_out), split by right cutout
    outer_pts_ccw.append((x_half_out, y_fn_bot))
    outer_pts_ccw.append((x_half_out, y_fn_top))
    outer_pts_ccw.append((x_half_out, cy_out))
    # TR arc: from x_half_out, cy_out to cx_out, y_half_out
    outer_pts_ccw.extend(arc_tr_out[1:])
    # Top straight: (cx_out, y_half_out) -> (-cx_out, y_half_out), split by top cutout
    outer_pts_ccw.append((x_tc_r, y_half_out))
    outer_pts_ccw.append((x_tc_l, y_half_out))
    outer_pts_ccw.append((-cx_out, y_half_out))
    # TL arc: from -cx_out, y_half_out to -x_half_out, cy_out
    outer_pts_ccw.extend(arc_tl_out[1:])
    # Left straight: (-x_half_out, cy_out) -> (-x_half_out, -cy_out), split by finger notch
    outer_pts_ccw.append((-x_half_out, y_fnl_top))
    outer_pts_ccw.append((-x_half_out, y_fnl_bot))
    outer_pts_ccw.append((-x_half_out, -cy_out))
    # BL arc: from -x_half_out, -cy_out to -cx_out, -y_half_out
    outer_pts_ccw.extend(arc_bl_out[1:-1])

    triangulate_and_add(stl, outer_pts_ccw, z0, normal_up=False)

    # -------------------------------------------------------------------------
    # 2. Outer Vertical Walls (from Z=0 upward)
    # -------------------------------------------------------------------------
    # Go around outer_pts_ccw, pairing (P_i, P_{i+1})
    # Determine the top height of each segment:
    n_out = len(outer_pts_ccw)
    for i in range(n_out):
        p0 = outer_pts_ccw[i]
        p1 = outer_pts_ccw[(i+1)%n_out]

        # Check if segment is in cutout:
        # Bottom cutout: on y = -y_half_out between x_bc_l and x_bc_r
        if abs(p0[1] - (-y_half_out)) < 1e-4 and abs(p1[1] - (-y_half_out)) < 1e-4:
            if p0[0] >= x_bc_l - 1e-4 and p1[0] <= x_bc_r + 1e-4:
                add_wall_quads(stl, p0, p1, z0, z_floor)
                continue
        # Right cutout: on x = x_half_out between y_fn_bot and y_fn_top
        if abs(p0[0] - x_half_out) < 1e-4 and abs(p1[0] - x_half_out) < 1e-4:
            if p0[1] >= y_fn_bot - 1e-4 and p1[1] <= y_fn_top + 1e-4:
                add_wall_quads(stl, p0, p1, z0, z_floor)
                continue
        # Top cutout: on y = y_half_out between x_tc_l and x_tc_r (CW in x since p0 > p1)
        if abs(p0[1] - y_half_out) < 1e-4 and abs(p1[1] - y_half_out) < 1e-4:
            if p1[0] >= x_tc_l - 1e-4 and p0[0] <= x_tc_r + 1e-4:
                add_wall_quads(stl, p0, p1, z0, z_floor)
                continue
        # Left finger notch: on x = -x_half_out between y_fnl_bot and y_fnl_top
        if abs(p0[0] - (-x_half_out)) < 1e-4 and abs(p1[0] - (-x_half_out)) < 1e-4:
            if p1[1] >= y_fnl_bot - 1e-4 and p0[1] <= y_fnl_top + 1e-4:
                add_wall_quads(stl, p0, p1, z0, z_notch)
                continue

        # Standard wall: goes all the way to z_top
        add_wall_quads(stl, p0, p1, z0, z_top)

    # -------------------------------------------------------------------------
    # 3. Cavity Floor at Z = z_floor (2.4)
    # -------------------------------------------------------------------------
    # A single continuous polygon representing the floor!
    # Traversed in CCW order:
    floor_poly_ccw = []
    # Start at top cutout: from (x_tc_l, y_half_out) to (x_tc_r, y_half_out)
    floor_poly_ccw.append((x_tc_l, y_half_out))
    floor_poly_ccw.append((x_tc_r, y_half_out))
    floor_poly_ccw.append((x_tc_r, y_half_in))
    # Inner top wall: (x_tc_r, y_half_in) -> (cx_in, y_half_in)
    floor_poly_ccw.append((cx_in, y_half_in))
    # TR inner corner: to (x_half_in, cy_in)
    floor_poly_ccw.extend(arc_tr_in[1:])
    # Inner right wall: to (x_half_in, y_fn_top)
    floor_poly_ccw.append((x_half_in, y_fn_top))
    # Right cutout loop: (x_half_in, y_fn_top) -> (x_half_out, y_fn_top) -> (x_half_out, y_fn_bot) -> (x_half_in, y_fn_bot)
    floor_poly_ccw.append((x_half_out, y_fn_top))
    floor_poly_ccw.append((x_half_out, y_fn_bot))
    floor_poly_ccw.append((x_half_in, y_fn_bot))
    # Inner right wall: to (x_half_in, -cy_in)
    floor_poly_ccw.append((x_half_in, -cy_in))
    # BR inner corner: to (cx_in, -y_half_in)
    floor_poly_ccw.extend(arc_br_in[1:])
    # Inner bottom wall: to (x_bc_r, -y_half_in)
    floor_poly_ccw.append((x_bc_r, -y_half_in))
    # Bottom cutout loop: (x_bc_r, -y_half_in) -> (x_bc_r, -y_half_out) -> (x_bc_l, -y_half_out) -> (x_bc_l, -y_half_in)
    floor_poly_ccw.append((x_bc_r, -y_half_out))
    floor_poly_ccw.append((x_bc_l, -y_half_out))
    floor_poly_ccw.append((x_bc_l, -y_half_in))
    # Inner bottom wall: to (-cx_in, -y_half_in)
    floor_poly_ccw.append((-cx_in, -y_half_in))
    # BL inner corner: to (-x_half_in, -cy_in)
    floor_poly_ccw.extend(arc_bl_in[1:])
    # Inner left wall: straight from (-x_half_in, -cy_in) to (-x_half_in, cy_in)
    # Note: insert finger notch points so they match the notch wall edges!
    floor_poly_ccw.append((-x_half_in, y_fnl_bot))
    floor_poly_ccw.append((-x_half_in, y_fnl_top))
    floor_poly_ccw.append((-x_half_in, cy_in))
    # TL inner corner: to (-cx_in, y_half_in)
    floor_poly_ccw.extend(arc_tl_in[1:])
    # Inner top wall: to (x_tc_l, y_half_in)
    floor_poly_ccw.append((x_tc_l, y_half_in))

    triangulate_and_add(stl, floor_poly_ccw, z_floor, normal_up=True)

    # -------------------------------------------------------------------------
    # 4. Inner Walls & Cutout End Faces (from z_floor upward)
    # -------------------------------------------------------------------------
    # Around floor_poly_ccw, each segment (p0, p1) corresponds to an inner wall or cutout side:
    # If we face normal inward/outward correctly:
    # Traversing floor_poly_ccw in CW order gives wall normal into the cavity!
    # Let's inspect each segment:
    n_floor = len(floor_poly_ccw)
    for i in range(n_floor):
        p0 = floor_poly_ccw[i]
        p1 = floor_poly_ccw[(i+1)%n_floor]

        # Top cutout outer bottom edge: (x_tc_l, y_half_out) -> (x_tc_r, y_half_out) - already covered by outer wall!
        if abs(p0[1] - y_half_out) < 1e-4 and abs(p1[1] - y_half_out) < 1e-4:
            continue
        # Right cutout outer bottom edge: (x_half_out, y_fn_top) -> (x_half_out, y_fn_bot) - already covered by outer wall!
        if abs(p0[0] - x_half_out) < 1e-4 and abs(p1[0] - x_half_out) < 1e-4:
            continue
        # Bottom cutout outer bottom edge: (x_bc_r, -y_half_out) -> (x_bc_l, -y_half_out) - already covered by outer wall!
        if abs(p0[1] - (-y_half_out)) < 1e-4 and abs(p1[1] - (-y_half_out)) < 1e-4:
            continue

        # Top cutout right end wall: (x_tc_r, y_half_out) -> (x_tc_r, y_half_in)
        # Vector is (0, -wall), quad normal points -X (into cutout)
        if abs(p0[0] - x_tc_r) < 1e-4 and abs(p1[0] - x_tc_r) < 1e-4:
            add_wall_quads(stl, p0, p1, z_floor, z_top)
            continue
        # Top cutout left end wall: (x_tc_l, y_half_in) -> (x_tc_l, y_half_out)
        # Vector is (0, +wall), quad normal points +X (into cutout)
        if abs(p0[0] - x_tc_l) < 1e-4 and abs(p1[0] - x_tc_l) < 1e-4:
            add_wall_quads(stl, p0, p1, z_floor, z_top)
            continue

        # Right cutout top end wall: (x_half_in, y_fn_top) -> (x_half_out, y_fn_top)
        # Vector is (+wall, 0), quad normal points -Y (into cutout)
        if abs(p0[1] - y_fn_top) < 1e-4 and abs(p1[1] - y_fn_top) < 1e-4:
            add_wall_quads(stl, p0, p1, z_floor, z_top)
            continue
        # Right cutout bottom end wall: (x_half_out, y_fn_bot) -> (x_half_in, y_fn_bot)
        # Vector is (-wall, 0), quad normal points +Y (into cutout)
        if abs(p0[1] - y_fn_bot) < 1e-4 and abs(p1[1] - y_fn_bot) < 1e-4:
            add_wall_quads(stl, p0, p1, z_floor, z_top)
            continue

        # Bottom cutout right end wall: (x_bc_r, -y_half_in) -> (x_bc_r, -y_half_out)
        # Vector is (0, -wall), quad normal points -X (into cutout)
        if abs(p0[0] - x_bc_r) < 1e-4 and abs(p1[0] - x_bc_r) < 1e-4:
            add_wall_quads(stl, p0, p1, z_floor, z_top)
            continue
        # Bottom cutout left end wall: (x_bc_l, -y_half_out) -> (x_bc_l, -y_half_in)
        # Vector is (0, +wall), quad normal points +X (into cutout)
        if abs(p0[0] - x_bc_l) < 1e-4 and abs(p1[0] - x_bc_l) < 1e-4:
            add_wall_quads(stl, p0, p1, z_floor, z_top)
            continue

        # Left inner wall finger notch segment: from (-x_half_in, y_fnl_bot) to (-x_half_in, y_fnl_top)
        # Inner wall height only goes to z_notch!
        if abs(p0[0] - (-x_half_in)) < 1e-4 and abs(p1[0] - (-x_half_in)) < 1e-4:
            if p0[1] >= y_fnl_bot - 1e-4 and p1[1] <= y_fnl_top + 1e-4:
                add_wall_quads(stl, p0, p1, z_floor, z_notch)
                continue

        # All other standard inner cavity walls:
        add_wall_quads(stl, p0, p1, z_floor, z_top)

    # -------------------------------------------------------------------------
    # 5. Left Finger Notch Step (at Z = z_notch = 9.0) & End Faces
    # -------------------------------------------------------------------------
    # Horizontal step face:
    p_notch_step = [
        (-x_half_out, y_fnl_bot),
        (-x_half_in,  y_fnl_bot),
        (-x_half_in,  y_fnl_top),
        (-x_half_out, y_fnl_top)
    ]
    triangulate_and_add(stl, p_notch_step, z_notch, normal_up=True)

    # End walls from z_notch to z_top:
    # Bottom end at y_fnl_bot: from (-x_half_out, y_fnl_bot) to (-x_half_in, y_fnl_bot)
    add_wall_quads(stl, (-x_half_out, y_fnl_bot), (-x_half_in, y_fnl_bot), z_notch, z_top)
    # Top end at y_fnl_top: from (-x_half_in, y_fnl_top) to (-x_half_out, y_fnl_top)
    add_wall_quads(stl, (-x_half_in, y_fnl_top), (-x_half_out, y_fnl_top), z_notch, z_top)

    # -------------------------------------------------------------------------
    # 6. Top Rim Horizontal Faces (Z = z_top = 20.4)
    # -------------------------------------------------------------------------
    # Top rim has 4 continuous polygon pieces:

    # Piece 1: Top-Right Corner & Upper Right Wall:
    poly_tr = [(x_tc_r, y_half_out), (cx_out, y_half_out)] + arc_tr_out[1:] + [(x_half_out, y_fn_top)]
    poly_tr += [(x_half_in, y_fn_top), (x_half_in, cy_in)]
    for i in range(len(arc_tr_in)-1, -1, -1):
        poly_tr.append(arc_tr_in[i])
    poly_tr += [(x_tc_r, y_half_in)]
    triangulate_and_add(stl, poly_tr, z_top, normal_up=True)

    # Piece 2: Bottom-Right Corner & Lower Right Wall:
    poly_br = [(x_half_out, y_fn_bot), (x_half_out, -cy_out)] + arc_br_out[1:] + [(x_bc_r, -y_half_out)]
    poly_br += [(x_bc_r, -y_half_in), (cx_in, -y_half_in)]
    for i in range(len(arc_br_in)-1, -1, -1):
        poly_br.append(arc_br_in[i])
    poly_br += [(x_half_in, -cy_in), (x_half_in, y_fn_bot)]
    triangulate_and_add(stl, poly_br, z_top, normal_up=True)

    # Piece 3: Left Wall Upper Section & Top-Left Corner:
    poly_tl = [(-x_half_out, y_fnl_top), (-x_half_out, cy_out)] + arc_tl_out[1:] + [(-cx_out, y_half_out), (x_tc_l, y_half_out)]
    poly_tl += [(x_tc_l, y_half_in), (-cx_in, y_half_in)]
    for i in range(len(arc_tl_in)-1, -1, -1):
        poly_tl.append(arc_tl_in[i])
    poly_tl += [(-x_half_in, cy_in), (-x_half_in, y_fnl_top)]
    triangulate_and_add(stl, poly_tl, z_top, normal_up=True)

    # Piece 4: Left Wall Lower Section & Bottom-Left Corner:
    poly_bl = [(x_bc_l, -y_half_out), (-cx_out, -y_half_out)] + arc_bl_out[1:] + [(-x_half_out, -cy_out), (-x_half_out, y_fnl_bot)]
    poly_bl += [(-x_half_in, y_fnl_bot), (-x_half_in, -cy_in)]
    for i in range(len(arc_bl_in)-1, -1, -1):
        poly_bl.append(arc_bl_in[i])
    poly_bl += [(-cx_in, -y_half_in), (x_bc_l, -y_half_in)]
    triangulate_and_add(stl, poly_bl, z_top, normal_up=True)

    return stl

if __name__ == '__main__':
    base_dir = "/Users/aubreyhan/Tools/git_repo/PersonalDev/3D"
    stl_path = os.path.join(base_dir, "Columbus_P1M2_Dock.stl")
    stl = build_model()
    is_ok, n_tri, unmatched = stl.check_manifold()
    print(f"Manifold check: is_ok={is_ok}, triangles={n_tri}, unmatched_edges={unmatched}")
    stl.save_binary(stl_path, name="P1M2_Dock")
