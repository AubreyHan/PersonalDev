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
        print(f"Manifold check: {len(self.triangles)} triangles, {len(edges)} directed edges, unmatched: {unmatched}")
        return unmatched == 0

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
        print(f"Saved {filepath}: {len(self.triangles)} triangles")


def triangulate_2d_quadrant(r_hole, R_corner=5.0, c_xy=22.0, n_arc=16):
    """
    Returns CCW triangles for quadrant 1 (X >= 0, Y >= 0).
    Boundary goes from (0,0) -> (27,0) -> corner arc -> (0,27) -> (0,0).
    Hole is centered at (c_xy, c_xy) with radius r_hole.
    """
    triangles = []
    
    # 1. TR: Annular corner sector (theta in [0, pi/2])
    for i in range(n_arc):
        a0 = 0.5 * math.pi * i / n_arc
        a1 = 0.5 * math.pi * (i + 1) / n_arc
        h0 = (c_xy + r_hole * math.cos(a0), c_xy + r_hole * math.sin(a0))
        h1 = (c_xy + r_hole * math.cos(a1), c_xy + r_hole * math.sin(a1))
        c0 = (c_xy + R_corner * math.cos(a0), c_xy + R_corner * math.sin(a0))
        c1 = (c_xy + R_corner * math.cos(a1), c_xy + R_corner * math.sin(a1))
        triangles.append((c0, c1, h1))
        triangles.append((c0, h1, h0))

    # 2. TL: [0, 22] x [22, 27]
    v0 = (0.0, c_xy)
    v1 = (c_xy - r_hole, c_xy)
    v2 = (c_xy - r_hole, c_xy + R_corner)
    v3 = (0.0, c_xy + R_corner)
    triangles.append((v0, v1, v2))
    triangles.append((v0, v2, v3))
    
    p_top_right = (c_xy, c_xy + R_corner)
    h_top = (c_xy, c_xy + r_hole)
    triangles.append((v2, h_top, p_top_right))
    
    for i in range(n_arc):
        a0 = 0.5 * math.pi + 0.5 * math.pi * i / n_arc
        a1 = 0.5 * math.pi + 0.5 * math.pi * (i + 1) / n_arc
        h0 = (c_xy + r_hole * math.cos(a0), c_xy + r_hole * math.sin(a0))
        h1 = (c_xy + r_hole * math.cos(a1), c_xy + r_hole * math.sin(a1))
        triangles.append((v2, h1, h0))

    # 3. BR: [22, 27] x [0, 22]
    b0 = (c_xy, 0.0)
    b1 = (c_xy + R_corner, 0.0)
    b2 = (c_xy + R_corner, c_xy - r_hole)
    b3 = (c_xy, c_xy - r_hole)
    triangles.append((b0, b1, b2))
    triangles.append((b0, b2, b3))
    
    p_right_top = (c_xy + R_corner, c_xy)
    h_right = (c_xy + r_hole, c_xy)
    triangles.append((b2, p_right_top, h_right))
    
    for i in range(n_arc):
        a0 = 1.5 * math.pi + 0.5 * math.pi * i / n_arc
        a1 = 1.5 * math.pi + 0.5 * math.pi * (i + 1) / n_arc
        h0 = (c_xy + r_hole * math.cos(a0), c_xy + r_hole * math.sin(a0))
        h1 = (c_xy + r_hole * math.cos(a1), c_xy + r_hole * math.sin(a1))
        triangles.append((b2, h0, h1))

    # 4. BL: [0, 22] x [0, 22]
    c_sub = c_xy - r_hole
    m0 = (0.0, 0.0)
    m1 = (c_sub, 0.0)
    m2 = (c_sub, c_sub)
    m3 = (0.0, c_sub)
    triangles.append((m0, m1, m2))
    triangles.append((m0, m2, m3))
    
    s0 = (0.0, c_sub)
    s1 = (c_sub, c_sub)
    s2 = (c_sub, c_xy)
    s3 = (0.0, c_xy)
    triangles.append((s0, s1, s2))
    triangles.append((s0, s2, s3))
    
    t0 = (c_sub, 0.0)
    t1 = (c_xy, 0.0)
    t2 = (c_xy, c_sub)
    t3 = (c_sub, c_sub)
    triangles.append((t0, t1, t2))
    triangles.append((t0, t2, t3))
    
    for i in range(n_arc):
        a0 = math.pi + 0.5 * math.pi * i / n_arc
        a1 = math.pi + 0.5 * math.pi * (i + 1) / n_arc
        h0 = (c_xy + r_hole * math.cos(a0), c_xy + r_hole * math.sin(a0))
        h1 = (c_xy + r_hole * math.cos(a1), c_xy + r_hole * math.sin(a1))
        triangles.append((m2, h0, h1))

    return triangles


def get_full_2d_mesh(r_hole, R_corner=5.0, c_xy=22.0, n_arc=16):
    """
    Combines 4 quadrants using symmetry.
    Returns list of CCW triangles for the entire plate face.
    """
    quad1 = triangulate_2d_quadrant(r_hole, R_corner, c_xy, n_arc)
    all_tris = []
    
    # Q1: (+X, +Y) -> CCW
    for t in quad1:
        all_tris.append(t)
        
    # Q2: (-X, +Y) -> flip X -> reverses winding, so reverse (a, c, b) to keep CCW
    for t in quad1:
        a = (-t[0][0], t[0][1])
        b = (-t[1][0], t[1][1])
        c = (-t[2][0], t[2][1])
        all_tris.append((a, c, b))
        
    # Q3: (-X, -Y) -> flip X and Y -> winding preserved
    for t in quad1:
        a = (-t[0][0], -t[0][1])
        b = (-t[1][0], -t[1][1])
        c = (-t[2][0], -t[2][1])
        all_tris.append((a, b, c))
        
    # Q4: (+X, -Y) -> flip Y -> reverses winding, so reverse (a, c, b)
    for t in quad1:
        a = (t[0][0], -t[0][1])
        b = (t[1][0], -t[1][1])
        c = (t[2][0], -t[2][1])
        all_tris.append((a, c, b))
        
    return all_tris


def get_outer_perimeter(R_corner=5.0, c_xy=22.0, n_arc=16):
    """
    Returns ordered CCW vertices of the outer perimeter (54x54 with R5 corners).
    """
    pts = []
    # 4 corners in CCW order: TR (+,+), TL (-,+), BL (-,-), BR (+,-)
    centers = [(c_xy, c_xy), (-c_xy, c_xy), (-c_xy, -c_xy), (c_xy, -c_xy)]
    base_angles = [0.0, 0.5 * math.pi, 1.0 * math.pi, 1.5 * math.pi]
    
    for c, base_ang in zip(centers, base_angles):
        for i in range(n_arc + 1):
            ang = base_ang + 0.5 * math.pi * i / n_arc
            pts.append((c[0] + R_corner * math.cos(ang), c[1] + R_corner * math.sin(ang)))
            
    # Remove consecutive duplicate points if any
    unique_pts = [pts[0]]
    for p in pts[1:]:
        if math.hypot(p[0] - unique_pts[-1][0], p[1] - unique_pts[-1][1]) > 1e-6:
            unique_pts.append(p)
    if math.hypot(unique_pts[0][0] - unique_pts[-1][0], unique_pts[0][1] - unique_pts[-1][1]) < 1e-6:
        unique_pts.pop()
    return unique_pts


def get_hole_perimeter(center, r_hole, n_pts=64):
    """
    Returns CCW vertices of a circular hole.
    """
    pts = []
    for i in range(n_pts):
        ang = 2.0 * math.pi * i / n_pts
        pts.append((center[0] + r_hole * math.cos(ang), center[1] + r_hole * math.sin(ang)))
    return pts


def build_enclosure_cover(countersunk=True, filename="bottom_cover.stl"):
    stl = STLWriter()
    
    thickness = 2.0
    R_corner = 5.0
    c_xy = 22.0
    n_arc = 16
    n_hole = 4 * n_arc # 64
    
    r_through = 1.7  # 3.4 mm dia for M3
    r_cs_bottom = 3.2 if countersunk else 1.7 # 6.4 mm dia for M3 countersink
    cs_depth = 1.5 if countersunk else 0.0
    
    hole_centers = [
        (c_xy, c_xy),
        (-c_xy, c_xy),
        (-c_xy, -c_xy),
        (c_xy, -c_xy)
    ]
    
    # 1. TOP FACE (Z = thickness)
    # Face normal is +Z. CCW triangles in 2D have normal +Z.
    top_tris = get_full_2d_mesh(r_through, R_corner, c_xy, n_arc)
    for t in top_tris:
        v0 = (t[0][0], t[0][1], thickness)
        v1 = (t[1][0], t[1][1], thickness)
        v2 = (t[2][0], t[2][1], thickness)
        stl.add_triangle(v0, v1, v2)
        
    # 2. BOTTOM FACE (Z = 0)
    # Face normal is -Z. CCW triangles in 2D must be reversed (v0, v2, v1) for normal -Z.
    bot_tris = get_full_2d_mesh(r_cs_bottom, R_corner, c_xy, n_arc)
    for t in bot_tris:
        v0 = (t[0][0], t[0][1], 0.0)
        v1 = (t[1][0], t[1][1], 0.0)
        v2 = (t[2][0], t[2][1], 0.0)
        stl.add_triangle(v0, v2, v1)
        
    # 3. OUTER WALLS (connecting bottom perimeter to top perimeter)
    # Perimeter is CCW in XY. Looking from outside, moving along perimeter:
    # bottom p0 -> p1, top p1 -> p0.
    # Outward normal: p0_bot -> p1_bot -> p1_top -> p0_top
    outer_pts = get_outer_perimeter(R_corner, c_xy, n_arc)
    m = len(outer_pts)
    for i in range(m):
        p0 = outer_pts[i]
        p1 = outer_pts[(i + 1) % m]
        
        b0 = (p0[0], p0[1], 0.0)
        b1 = (p1[0], p1[1], 0.0)
        t1 = (p1[0], p1[1], thickness)
        t0 = (p0[0], p0[1], thickness)
        
        # CCW quad: b0 -> b1 -> t1 -> t0
        stl.add_quad(b0, b1, t1, t0)
        
    # 4. HOLE WALLS
    # Hole perimeter is CCW in XY. Inward surface facing hole center:
    # For a hole, interior of solid is outside the hole.
    # Looking from hole axis out towards the wall:
    # Top edge goes p0 -> p1 (CCW), but normal points into hole (-radial),
    # so: t0 -> t1 -> b1 -> b0 is CCW when viewed from hole center!
    for c in hole_centers:
        if countersunk:
            # Conical section: from Z=0 (r=3.2) to Z=cs_depth (r=1.7)
            pts_bot = get_hole_perimeter(c, r_cs_bottom, n_hole)
            pts_mid = get_hole_perimeter(c, r_through, n_hole)
            pts_top = get_hole_perimeter(c, r_through, n_hole)
            
            for i in range(n_hole):
                pb0 = (pts_bot[i][0], pts_bot[i][1], 0.0)
                pb1 = (pts_bot[(i+1)%n_hole][0], pts_bot[(i+1)%n_hole][1], 0.0)
                pm0 = (pts_mid[i][0], pts_mid[i][1], cs_depth)
                pm1 = (pts_mid[(i+1)%n_hole][0], pts_mid[(i+1)%n_hole][1], cs_depth)
                
                # Cone wall: pm0 -> pm1 -> pb1 -> pb0
                stl.add_quad(pm0, pm1, pb1, pb0)
                
            # Cylindrical section: from Z=cs_depth to Z=thickness
            for i in range(n_hole):
                pm0 = (pts_mid[i][0], pts_mid[i][1], cs_depth)
                pm1 = (pts_mid[(i+1)%n_hole][0], pts_mid[(i+1)%n_hole][1], cs_depth)
                pt0 = (pts_top[i][0], pts_top[i][1], thickness)
                pt1 = (pts_top[(i+1)%n_hole][0], pts_top[(i+1)%n_hole][1], thickness)
                
                # Cylinder wall: pt0 -> pt1 -> pm1 -> pm0
                stl.add_quad(pt0, pt1, pm1, pm0)
        else:
            # Straight through cylinder
            pts_bot = get_hole_perimeter(c, r_through, n_hole)
            pts_top = get_hole_perimeter(c, r_through, n_hole)
            
            for i in range(n_hole):
                pb0 = (pts_bot[i][0], pts_bot[i][1], 0.0)
                pb1 = (pts_bot[(i+1)%n_hole][0], pts_bot[(i+1)%n_hole][1], 0.0)
                pt0 = (pts_top[i][0], pts_top[i][1], thickness)
                pt1 = (pts_top[(i+1)%n_hole][0], pts_top[(i+1)%n_hole][1], thickness)
                
                # Cylinder wall: pt0 -> pt1 -> pb1 -> pb0
                stl.add_quad(pt0, pt1, pb1, pb0)
                
    is_manifold = stl.check_manifold()
    stl.save_binary(filename, name=os.path.basename(filename))
    return is_manifold

print("Testing model generation...")
ok1 = build_enclosure_cover(countersunk=True, filename="/Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Enclosure_Bottom_Cover_Countersunk.stl")
ok2 = build_enclosure_cover(countersunk=False, filename="/Users/aubreyhan/Tools/git_repo/PersonalDev/3D/Enclosure_Bottom_Cover_Flat.stl")
print(f"Countersunk manifold: {ok1}, Flat manifold: {ok2}")
