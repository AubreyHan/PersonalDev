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

    def add_box(self, x0, x1, y0, y1, z0, z1):
        v = [
            (x0, y0, z0), # 0
            (x1, y0, z0), # 1
            (x1, y1, z0), # 2
            (x0, y1, z0), # 3
            (x0, y0, z1), # 4
            (x1, y0, z1), # 5
            (x1, y1, z1), # 6
            (x0, y1, z1), # 7
        ]
        # Bottom (-Z)
        self.add_quad(v[0], v[3], v[2], v[1])
        # Top (+Z)
        self.add_quad(v[4], v[5], v[6], v[7])
        # Front (-Y)
        self.add_quad(v[0], v[1], v[5], v[4])
        # Right (+X)
        self.add_quad(v[1], v[2], v[6], v[5])
        # Back (+Y)
        self.add_quad(v[2], v[3], v[7], v[6])
        # Left (-X)
        self.add_quad(v[3], v[0], v[4], v[7])

    def add_tri_prism_z(self, p0, p1, p2, z0, z1):
        """
        Extrudes triangle (p0, p1, p2) in XY from z0 to z1.
        p0, p1, p2 must be in CCW order in XY plane.
        """
        v0 = (p0[0], p0[1], z0)
        v1 = (p1[0], p1[1], z0)
        v2 = (p2[0], p2[1], z0)
        v3 = (p0[0], p0[1], z1)
        v4 = (p1[0], p1[1], z1)
        v5 = (p2[0], p2[1], z1)
        
        # Bottom (-Z)
        self.add_triangle(v0, v2, v1)
        # Top (+Z)
        self.add_triangle(v3, v4, v5)
        # Side 0-1
        self.add_quad(v0, v1, v4, v3)
        # Side 1-2
        self.add_quad(v1, v2, v5, v4)
        # Side 2-0
        self.add_quad(v2, v0, v3, v5)

    def save_binary(self, filepath, name="miniso_dock"):
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

def build_model():
    stl = STLWriter()
    
    # Dimensions:
    # Inner cavity (for 67.0 x 23.5 power bank)
    w_in = 69.5
    d_in = 25.5
    x_in_half = w_in / 2.0   # 34.75
    y_in_half = d_in / 2.0   # 12.75
    
    wall_t = 2.8
    floor_t = 3.2
    h_wall = 50.0
    z_top = floor_t + h_wall # 53.2
    
    x_out_half = x_in_half + wall_t # 37.55
    y_out_half = y_in_half + wall_t # 15.55
    
    # Base flange
    base_w = 96.0
    base_d = 48.0
    xb_half = base_w / 2.0  # 48.0
    yb_half = base_d / 2.0  # 24.0
    c_rad = 6.0             # 6mm corner chamfer
    
    # Bottom vent
    xv_half = 16.0 # 32mm wide
    yv_half = 7.0  # 14mm deep
    
    # Cable slot (Left & Right)
    cable_slot_half_w = 6.5 # 13mm wide
    z_cable_slot = 19.0
    
    # Finger cutout (Front & Back)
    finger_cutout_half_w = 19.0 # 38mm wide
    z_finger_cutout = 22.0
    
    # ----------------------------------------------------
    # 1. Base Flange (Z from 0 to floor_t)
    # ----------------------------------------------------
    # Central body of base (between -xb_half + c_rad and xb_half - c_rad):
    # Left wing (excluding corner corners):
    # Split into:
    # Outer left wing: X: -xb_half to -xb_half + c_rad, Y: -yb_half + c_rad to yb_half - c_rad
    stl.add_box(-xb_half, -xb_half + c_rad, -yb_half + c_rad, yb_half - c_rad, 0, floor_t)
    # Outer right wing: X: xb_half - c_rad to xb_half, Y: -yb_half + c_rad to yb_half - c_rad
    stl.add_box(xb_half - c_rad, xb_half, -yb_half + c_rad, yb_half - c_rad, 0, floor_t)
    
    # Main mid-left wing: X: -xb_half + c_rad to -xv_half, Y: -yb_half to yb_half
    stl.add_box(-xb_half + c_rad, -xv_half, -yb_half, yb_half, 0, floor_t)
    # Main mid-right wing: X: xv_half to xb_half - c_rad, Y: -yb_half to yb_half
    stl.add_box(xv_half, xb_half - c_rad, -yb_half, yb_half, 0, floor_t)
    
    # Front base central strip: X: -xv_half to xv_half, Y: -yb_half to -yv_half
    stl.add_box(-xv_half, xv_half, -yb_half, -yv_half, 0, floor_t)
    # Rear base central strip: X: -xv_half to xv_half, Y: yv_half to yb_half
    stl.add_box(-xv_half, xv_half, yv_half, yb_half, 0, floor_t)
    
    # 4 Chamfered Base Corners (45-degree triangular prisms):
    # Top-Right (X > 0, Y > 0): p0=(xb_half-c_rad, yb_half-c_rad), p1=(xb_half-c_rad, yb_half), p2=(xb_half, yb_half-c_rad) (CCW order: p0 -> p2 -> p1)
    stl.add_tri_prism_z((xb_half - c_rad, yb_half - c_rad), (xb_half, yb_half - c_rad), (xb_half - c_rad, yb_half), 0, floor_t)
    
    # Top-Left (X < 0, Y > 0): p0=(-xb_half+c_rad, yb_half-c_rad), p1=(-xb_half+c_rad, yb_half), p2=(-xb_half, yb_half-c_rad) (CCW order: p0 -> p1 -> p2)
    stl.add_tri_prism_z((-xb_half + c_rad, yb_half - c_rad), (-xb_half + c_rad, yb_half), (-xb_half, yb_half - c_rad), 0, floor_t)
    
    # Bottom-Left (X < 0, Y < 0): p0=(-xb_half+c_rad, -yb_half+c_rad), p1=(-xb_half, -yb_half+c_rad), p2=(-xb_half+c_rad, -yb_half) (CCW order: p0 -> p2 -> p1)
    stl.add_tri_prism_z((-xb_half + c_rad, -yb_half + c_rad), (-xb_half + c_rad, -yb_half), (-xb_half, -yb_half + c_rad), 0, floor_t)
    
    # Bottom-Right (X > 0, Y < 0): p0=(xb_half-c_rad, -yb_half+c_rad), p1=(xb_half, -yb_half+c_rad), p2=(xb_half-c_rad, -yb_half) (CCW order: p0 -> p1 -> p2)
    stl.add_tri_prism_z((xb_half - c_rad, -yb_half + c_rad), (xb_half, -yb_half + c_rad), (xb_half - c_rad, -yb_half), 0, floor_t)
    
    # ----------------------------------------------------
    # 2. Main Wall Corner L-Pillars (Z from floor_t to z_top)
    # Four corners hold the power bank securely from Z=3.2 to 53.2
    # ----------------------------------------------------
    # Rear-Left Corner Pillar
    stl.add_box(-x_out_half, -finger_cutout_half_w, y_in_half, y_out_half, floor_t, z_top)
    stl.add_box(-x_out_half, -x_in_half, cable_slot_half_w, y_in_half, floor_t, z_top)
    
    # Rear-Right Corner Pillar
    stl.add_box(finger_cutout_half_w, x_out_half, y_in_half, y_out_half, floor_t, z_top)
    stl.add_box(x_in_half, x_out_half, cable_slot_half_w, y_in_half, floor_t, z_top)
    
    # Front-Left Corner Pillar
    stl.add_box(-x_out_half, -finger_cutout_half_w, -y_out_half, -y_in_half, floor_t, z_top)
    stl.add_box(-x_out_half, -x_in_half, -y_in_half, -cable_slot_half_w, floor_t, z_top)
    
    # Front-Right Corner Pillar
    stl.add_box(finger_cutout_half_w, x_out_half, -y_out_half, -y_in_half, floor_t, z_top)
    stl.add_box(x_in_half, x_out_half, -y_in_half, -cable_slot_half_w, floor_t, z_top)
    
    # ----------------------------------------------------
    # 3. Lower Wall Sections (below cutouts)
    # ----------------------------------------------------
    # Front wall lower portion (below finger cutout: Z from floor_t to z_finger_cutout)
    stl.add_box(-finger_cutout_half_w, finger_cutout_half_w, -y_out_half, -y_in_half, floor_t, z_finger_cutout)
    
    # Rear wall lower portion (below finger cutout: Z from floor_t to z_finger_cutout)
    stl.add_box(-finger_cutout_half_w, finger_cutout_half_w, y_in_half, y_out_half, floor_t, z_finger_cutout)
    
    # Left wall lower portion (below cable slot: Z from floor_t to z_cable_slot)
    stl.add_box(-x_out_half, -x_in_half, -cable_slot_half_w, cable_slot_half_w, floor_t, z_cable_slot)
    
    # Right wall lower portion (below cable slot: Z from floor_t to z_cable_slot)
    stl.add_box(x_in_half, x_out_half, -cable_slot_half_w, cable_slot_half_w, floor_t, z_cable_slot)
    
    output_filename = "MINISO_10000mAh_Car_Holder.stl"
    stl.save_binary(output_filename, name="miniso_dock")
    return output_filename

if __name__ == '__main__':
    fn = build_model()
