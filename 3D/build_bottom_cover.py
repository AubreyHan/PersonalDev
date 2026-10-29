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

    def check_manifold(self):
        # Check edge manifoldness: each directed edge should match an opposite directed edge
        edges = {}
        for tri in self.triangles:
            for i in range(3):
                p0 = (round(tri[i][0], 4), round(tri[i][1], 4), round(tri[i][2], 4))
                p1 = (round(tri[(i+1)%3][0], 4), round(tri[(i+1)%3][1], 4), round(tri[(i+1)%3][2], 4))
                edges[(p0, p1)] = edges.get((p0, p1), 0) + 1

        unmatched = 0
        non_manifold = 0
        for (p0, p1), count in edges.items():
            rev = edges.get((p1, p0), 0)
            if count != 1 or rev != 1:
                unmatched += 1
        print(f"Manifold check: {len(edges)} directed edges, unmatched/non-manifold: {unmatched}")
        return unmatched == 0

print("STLWriter ready")
