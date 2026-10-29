#!/usr/bin/env python3
"""
Enclosure Bottom Cover 3D Model Generator
Specifically designed based on technical drawing:
- Outer dimension: 54.0 mm x 54.0 mm (44mm hole pitch + 2 x R5 corner arc)
- Corner fillets: R 5.0 mm (四个角 R5)
- Bottom thickness: 2.0 mm (底盖厚度 2mm)
- 4 x M3 screw mounting holes at 44.0 mm x 44.0 mm pitch (centers at +/-22.0, +/-22.0)
  - Through hole diameter: 3.4 mm (for M3 screw clearance)
  - Countersunk version: 90-degree DIN 7991 M3 countersink (top dia 6.4mm, depth 1.5mm)
  - Flat through version: 3.4mm straight cylinder

Generates 100% 2-manifold binary STL files compatible with Bambu Studio, PrusaSlicer, Cura, etc.
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

    def save_binary(self, filepath, name="cover"):
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
# Model Construction
# ==============================================================================

def get_outer_pts(R=5.0, c=22.0, n=16):
    """
    Returns ordered CCW perimeter points of 54x54 plate with R5 corners.
    """
    pts = []
    # 4 corners in CCW order: TR (+,+), TL (-,+), BL (-,-), BR (+,-)
    corners = [
        (c, c, 0.0),
        (-c, c, 0.5 * math.pi),
        (-c, -c, 1.0 * math.pi),
        (c, -c, 1.5 * math.pi)
    ]
    for cx, cy, base_ang in corners:
        for i in range(n + 1):
            a = base_ang + 0.5 * math.pi * i / n
            pts.append((cx + R * math.cos(a), cy + R * math.sin(a)))

    # Deduplicate consecutive points
    res = [pts[0]]
    for p in pts[1:]:
        if math.hypot(p[0]-res[-1][0], p[1]-res[-1][1]) > 1e-5:
            res.append(p)
    if math.hypot(res[0][0]-res[-1][0], res[0][1]-res[-1][1]) < 1e-5:
        res.pop()
    return res


def build_cover(countersunk=True, filepath="cover.stl"):
    stl = STLWriter()
    n_corner = 16
    n_hole = 32
    thickness = 2.0
    r_through = 1.7  # 3.4 mm dia for M3 screw clearance
    r_cs = 3.2 if countersunk else 1.7  # 6.4 mm dia for M3 countersink head
    cs_depth = 1.5 if countersunk else 0.0

    outer = get_outer_pts(5.0, 22.0, n_corner)
    hole_centers = [
        (22.0, 22.0),
        (-22.0, 22.0),
        (-22.0, -22.0),
        (22.0, -22.0)
    ]

    def make_face_mesh(r_h):
        pts_all = list(outer)
        hole_indices = []
        hole_loops = []
        for cx, cy in hole_centers:
            hole_indices.append(len(pts_all))
            # CW circle for earcut hole format
            h = [(cx + r_h * math.cos(-2.0*math.pi*i/n_hole), cy + r_h * math.sin(-2.0*math.pi*i/n_hole)) for i in range(n_hole)]
            hole_loops.append(h)
            pts_all.extend(h)

        flat_data = []
        for p in pts_all:
            flat_data.extend([p[0], p[1]])
        tri_indices = earcut(flat_data, hole_indices, 2)
        return pts_all, tri_indices, hole_loops

    top_pts, top_indices, top_holes = make_face_mesh(r_through)
    bot_pts, bot_indices, bot_holes = make_face_mesh(r_cs)

    # 1. TOP FACE (Z = thickness, normal = +Z)
    for i in range(0, len(top_indices), 3):
        i0, i1, i2 = top_indices[i], top_indices[i+1], top_indices[i+2]
        p0 = (top_pts[i0][0], top_pts[i0][1], thickness)
        p1 = (top_pts[i1][0], top_pts[i1][1], thickness)
        p2 = (top_pts[i2][0], top_pts[i2][1], thickness)
        sa = (p1[0]-p0[0])*(p2[1]-p0[1]) - (p1[1]-p0[1])*(p2[0]-p0[0])
        if sa > 0:
            stl.add_triangle(p0, p1, p2)
        else:
            stl.add_triangle(p0, p2, p1)

    # 2. BOTTOM FACE (Z = 0.0, normal = -Z)
    for i in range(0, len(bot_indices), 3):
        i0, i1, i2 = bot_indices[i], bot_indices[i+1], bot_indices[i+2]
        p0 = (bot_pts[i0][0], bot_pts[i0][1], 0.0)
        p1 = (bot_pts[i1][0], bot_pts[i1][1], 0.0)
        p2 = (bot_pts[i2][0], bot_pts[i2][1], 0.0)
        sa = (p1[0]-p0[0])*(p2[1]-p0[1]) - (p1[1]-p0[1])*(p2[0]-p0[0])
        if sa > 0:
            stl.add_triangle(p0, p2, p1)
        else:
            stl.add_triangle(p0, p1, p2)

    # 3. OUTER WALLS (connecting Z=0 to Z=thickness)
    m = len(outer)
    for i in range(m):
        p0 = outer[i]
        p1 = outer[(i+1)%m]
        b0 = (p0[0], p0[1], 0.0)
        b1 = (p1[0], p1[1], 0.0)
        t1 = (p1[0], p1[1], thickness)
        t0 = (p0[0], p0[1], thickness)
        stl.add_quad(b0, b1, t1, t0)

    # 4. HOLE WALLS
    for k in range(4):
        h_top = top_holes[k]
        h_bot = bot_holes[k]
        cx, cy = hole_centers[k]

        if countersunk:
            # Transition circle at Z = cs_depth with r = r_through
            h_mid = [(cx + r_through * math.cos(-2.0*math.pi*i/n_hole), cy + r_through * math.sin(-2.0*math.pi*i/n_hole)) for i in range(n_hole)]

            # Conical bevel: from Z=0 (r=3.2) to Z=cs_depth (r=1.7)
            for i in range(n_hole):
                pb0 = (h_bot[i][0], h_bot[i][1], 0.0)
                pb1 = (h_bot[(i+1)%n_hole][0], h_bot[(i+1)%n_hole][1], 0.0)
                pm0 = (h_mid[i][0], h_mid[i][1], cs_depth)
                pm1 = (h_mid[(i+1)%n_hole][0], h_mid[(i+1)%n_hole][1], cs_depth)
                stl.add_quad(pm0, pb0, pb1, pm1)

            # Straight cylinder: from Z=cs_depth to Z=thickness
            for i in range(n_hole):
                pm0 = (h_mid[i][0], h_mid[i][1], cs_depth)
                pm1 = (h_mid[(i+1)%n_hole][0], h_mid[(i+1)%n_hole][1], cs_depth)
                pt0 = (h_top[i][0], h_top[i][1], thickness)
                pt1 = (h_top[(i+1)%n_hole][0], h_top[(i+1)%n_hole][1], thickness)
                stl.add_quad(pt0, pm0, pm1, pt1)
        else:
            # Full straight cylinder through-hole
            for i in range(n_hole):
                pb0 = (h_bot[i][0], h_bot[i][1], 0.0)
                pb1 = (h_bot[(i+1)%n_hole][0], h_bot[(i+1)%n_hole][1], 0.0)
                pt0 = (h_top[i][0], h_top[i][1], thickness)
                pt1 = (h_top[(i+1)%n_hole][0], h_top[(i+1)%n_hole][1], thickness)
                stl.add_quad(pt0, pb0, pb1, pt1)

    is_manifold, num_tris, unmatched = stl.check_manifold()
    print(f"[{os.path.basename(filepath)}] Manifold: {is_manifold} (triangles: {num_tris}, unmatched edges: {unmatched})")
    assert is_manifold, f"Mesh {filepath} has {unmatched} unmatched non-manifold edges!"
    stl.save_binary(filepath, name=os.path.basename(filepath))
    return is_manifold


if __name__ == "__main__":
    base_dir = os.path.dirname(os.path.abspath(__file__))
    f1 = os.path.join(base_dir, "Enclosure_Bottom_Cover_Countersunk.stl")
    f2 = os.path.join(base_dir, "Enclosure_Bottom_Cover_Flat.stl")

    print("Building Enclosure Bottom Cover models...")
    build_cover(countersunk=True, filepath=f1)
    build_cover(countersunk=False, filepath=f2)
    print("All models successfully built and verified 100% manifold!")
