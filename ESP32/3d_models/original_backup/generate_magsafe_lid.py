import math
import struct
from collections import defaultdict

def create_slicer_optimized_lid(out_stl_path):
    N_THETA = 144  # 2.5 deg resolution
    thetas = [i * 2.0 * math.pi / N_THETA for i in range(N_THETA)]
    
    # Radii definitions
    R_HUB = 5.0           # Center solid hub (dia 10.0mm)
    R_WIN_OUT = 20.5      # Large GPS/cooling window outer edge (dia 41.0mm)
    R_STEP_IN = 21.6      # Inner ridge start (dia 43.2mm)
    R_MAG_IN = 22.4       # MagSafe inner radius (dia 44.8mm)
    R_MAG_OUT = 27.6      # MagSafe outer radius (dia 55.2mm)
    R_OUT = 28.5          # Overall outer radius (dia 57.0mm)
    
    R_PLUG_IN = 22.0      # Plug inner wall radius
    R_PLUG = 24.35        # Plug outer radius (dia 48.70mm)
    R_PLUG_TIP = 23.85    # Plug lead-in chamfer tip
    
    # Z levels (Vertical build axis in 3D printer: Z=0 is heatbed!)
    # Total height = 5.07mm
    Z_BED = 0.00          # Flat on heatbed (Z=0)
    Z_GROOVE = 0.80       # MagSafe groove ceiling (0.80mm deep recess from bed)
    Z_CHAMF = 0.40        # 45-deg chamfer for self-supporting bridging
    Z_SHOULDER = 2.57     # Seating shoulder on body rim
    Z_PLUG_CHAMF = 4.57   # Plug lead-in chamfer start
    Z_TOP = 5.07          # Tip of plug (insertion into body)

    # 4 large quadrant windows for GPS signals & heat dissipation
    # In car orientation: Top (Skyward) is +Y (+90 deg), Left is -X (180 deg), Bottom is -Y (270 deg), Right is +X (0 deg)
    def is_center_window(deg):
        deg = deg % 360.0
        quads = [(65, 115), (155, 205), (245, 295), (335, 360), (0, 25)]
        for q_min, q_max in quads:
            if q_min <= deg <= q_max:
                return True
        return False

    triangles = []

    def add_quad(p1, p2, p3, p4):
        # Two triangles with normal matching right-hand rule
        triangles.append((p1, p2, p3))
        triangles.append((p1, p3, p4))

    def add_tri(p1, p2, p3):
        triangles.append((p1, p2, p3))

    for i in range(N_THETA):
        th1 = thetas[i]
        th2 = thetas[(i + 1) % N_THETA]
        deg_mid = (math.degrees((th1 + th2) / 2.0)) % 360.0
        
        c1, s1 = math.cos(th1), math.sin(th1)
        c2, s2 = math.cos(th2), math.sin(th2)
        
        has_win = is_center_window(deg_mid)
        
        # Point generator: (r, z) -> (x, y, z)
        # Note: X = r*cos(th), Y = r*sin(th), Z = height
        def pt1(r, z): return (round(r * c1, 4), round(r * s1, 4), round(z, 4))
        def pt2(r, z): return (round(r * c2, 4), round(r * s2, 4), round(z, 4))

        # --- 1. BED LEVEL: CENTER HUB (r from 0 to R_HUB) ---
        # Bottom face on heatbed (Z_BED = 0.0)
        # Normal pointing down (-Z)
        add_tri((0.0, 0.0, Z_BED), pt1(R_HUB, Z_BED), pt2(R_HUB, Z_BED))
        # Top face of hub at Z_SHOULDER
        # Normal pointing up (+Z)
        add_tri((0.0, 0.0, Z_SHOULDER), pt2(R_HUB, Z_SHOULDER), pt1(R_HUB, Z_SHOULDER))

        # --- 2. WINDOW / SPOKE ZONE (R_HUB to R_WIN_OUT) ---
        if not has_win:
            # Solid cross spoke:
            # Bottom face on heatbed
            add_quad(pt1(R_HUB, Z_BED), pt1(R_WIN_OUT, Z_BED), pt2(R_WIN_OUT, Z_BED), pt2(R_HUB, Z_BED))
            # Top face at Z_SHOULDER
            add_quad(pt1(R_HUB, Z_SHOULDER), pt2(R_HUB, Z_SHOULDER), pt2(R_WIN_OUT, Z_SHOULDER), pt1(R_WIN_OUT, Z_SHOULDER))
        else:
            # Open window: vertical side walls
            # Hub inner wall: from Z_BED to Z_SHOULDER
            add_quad(pt1(R_HUB, Z_BED), pt2(R_HUB, Z_BED), pt2(R_HUB, Z_SHOULDER), pt1(R_HUB, Z_SHOULDER))
            # Window outer wall at R_WIN_OUT: from Z_SHOULDER down to Z_BED
            add_quad(pt1(R_WIN_OUT, Z_SHOULDER), pt2(R_WIN_OUT, Z_SHOULDER), pt2(R_WIN_OUT, Z_BED), pt1(R_WIN_OUT, Z_BED))

        # Radial side walls at window-spoke boundaries
        deg_prev = math.degrees(thetas[(i - 1) % N_THETA] + thetas[i]) / 2.0 % 360.0
        has_win_prev = is_center_window(deg_prev)
        if has_win_prev != has_win:
            if has_win:
                add_quad(pt1(R_HUB, Z_BED), pt1(R_HUB, Z_SHOULDER), pt1(R_WIN_OUT, Z_SHOULDER), pt1(R_WIN_OUT, Z_BED))
            else:
                add_quad(pt1(R_HUB, Z_BED), pt1(R_WIN_OUT, Z_BED), pt1(R_WIN_OUT, Z_SHOULDER), pt1(R_HUB, Z_SHOULDER))

        # --- 3. ZONE FROM R_WIN_OUT TO R_STEP_IN ---
        # Bottom face on heatbed
        add_quad(pt1(R_WIN_OUT, Z_BED), pt1(R_STEP_IN, Z_BED), pt2(R_STEP_IN, Z_BED), pt2(R_WIN_OUT, Z_BED))
        # Top face at Z_SHOULDER
        add_quad(pt1(R_WIN_OUT, Z_SHOULDER), pt2(R_WIN_OUT, Z_SHOULDER), pt2(R_STEP_IN, Z_SHOULDER), pt1(R_STEP_IN, Z_SHOULDER))

        # --- 4. RIDGE STEP: R_STEP_IN TO R_MAG_IN ---
        # Bottom face of inner retaining ridge on heatbed (Z_BED = 0.0)
        # Width: 21.6mm to 22.4mm (0.8mm solid bed adhesion ring)
        add_quad(pt1(R_STEP_IN, Z_BED), pt1(R_MAG_IN, Z_BED), pt2(R_MAG_IN, Z_BED), pt2(R_STEP_IN, Z_BED))
        # Top face at Z_SHOULDER
        add_quad(pt1(R_STEP_IN, Z_SHOULDER), pt2(R_STEP_IN, Z_SHOULDER), pt2(R_PLUG_IN, Z_SHOULDER), pt1(R_PLUG_IN, Z_SHOULDER))
        
        # MagSafe groove inner wall: rises from Z_BED to Z_GROOVE at R_MAG_IN
        add_quad(pt1(R_MAG_IN, Z_BED), pt2(R_MAG_IN, Z_BED), pt2(R_MAG_IN, Z_GROOVE), pt1(R_MAG_IN, Z_GROOVE))

        # --- 5. MAGSAFE GROOVE (R_MAG_IN TO R_MAG_OUT) ---
        # Recessed groove floor at Z_GROOVE (0.80mm deep)
        # Normal pointing down towards bed (-Z)
        add_quad(pt1(R_MAG_IN, Z_GROOVE), pt2(R_MAG_IN, Z_GROOVE), pt2(R_MAG_OUT, Z_GROOVE), pt1(R_MAG_OUT, Z_GROOVE))

        # MagSafe groove outer wall: drops from Z_GROOVE down to Z_BED at R_MAG_OUT
        add_quad(pt1(R_MAG_OUT, Z_GROOVE), pt2(R_MAG_OUT, Z_GROOVE), pt2(R_MAG_OUT, Z_BED), pt1(R_MAG_OUT, Z_BED))

        # --- 6. OUTER RETAINING RIM (R_MAG_OUT TO R_OUT) ---
        # Flat bed adhesion ring at Z_BED (27.6mm to 28.5mm, 0.9mm solid ring)
        add_quad(pt1(R_MAG_OUT, Z_BED), pt1(R_OUT, Z_BED), pt2(R_OUT, Z_BED), pt2(R_MAG_OUT, Z_BED))
        # Outer vertical cylinder wall from Z_BED to Z_SHOULDER at R_OUT (57.0mm OD)
        add_quad(pt1(R_OUT, Z_BED), pt2(R_OUT, Z_BED), pt2(R_OUT, Z_SHOULDER), pt1(R_OUT, Z_SHOULDER))
        
        # Mating shoulder resting face on body rim: from R_OUT (28.5) down to R_PLUG (24.35) at Z_SHOULDER
        # Normal pointing up (+Z)
        add_quad(pt1(R_PLUG, Z_SHOULDER), pt2(R_PLUG, Z_SHOULDER), pt2(R_OUT, Z_SHOULDER), pt1(R_OUT, Z_SHOULDER))

        # --- 7. PLUG LIP (R_PLUG = 24.35mm, R_PLUG_IN = 22.0mm) ---
        # Plug outer cylinder: from Z_SHOULDER up to Z_PLUG_CHAMF
        add_quad(pt1(R_PLUG, Z_SHOULDER), pt1(R_PLUG, Z_PLUG_CHAMF), pt2(R_PLUG, Z_PLUG_CHAMF), pt2(R_PLUG, Z_SHOULDER))
        # Plug lead-in chamfer: 45-deg inward taper to R_PLUG_TIP (23.85mm) at Z_TOP (5.07mm)
        add_quad(pt1(R_PLUG, Z_PLUG_CHAMF), pt1(R_PLUG_TIP, Z_TOP), pt2(R_PLUG_TIP, Z_TOP), pt2(R_PLUG, Z_PLUG_CHAMF))
        # Plug annular tip face at Z_TOP from R_PLUG_IN to R_PLUG_TIP
        add_quad(pt1(R_PLUG_IN, Z_TOP), pt2(R_PLUG_IN, Z_TOP), pt2(R_PLUG_TIP, Z_TOP), pt1(R_PLUG_TIP, Z_TOP))
        # Plug inner cylinder: from Z_TOP down to Z_SHOULDER at R_PLUG_IN
        add_quad(pt1(R_PLUG_IN, Z_TOP), pt1(R_PLUG_IN, Z_SHOULDER), pt2(R_PLUG_IN, Z_SHOULDER), pt2(R_PLUG_IN, Z_TOP))

    # Write out binary STL
    with open(out_stl_path, 'wb') as f:
        f.write(b'ESP32-S3-AMOLED MagSafe Lid - Slicer Optimized (Flat Bed Z=0)'.ljust(80, b'\x00'))
        f.write(struct.pack('<I', len(triangles)))
        for tri in triangles:
            p1, p2, p3 = tri
            u = (p2[0]-p1[0], p2[1]-p1[1], p2[2]-p1[2])
            v = (p3[0]-p1[0], p3[1]-p1[1], p3[2]-p1[2])
            nx = u[1]*v[2] - u[2]*v[1]
            ny = u[2]*v[0] - u[0]*v[2]
            nz = u[0]*v[1] - u[1]*v[0]
            l = math.hypot(math.hypot(nx, ny), nz)
            if l > 1e-9:
                nx, ny, nz = nx/l, ny/l, nz/l
            else:
                nx, ny, nz = 0.0, 0.0, 0.0
            f.write(struct.pack('<3f9fH', nx, ny, nz, p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], p3[0], p3[1], p3[2], 0))
    print(f"Generated {len(triangles)} triangles to {out_stl_path}")

create_slicer_optimized_lid("/Users/aubreyhan/.gemini/antigravity-cli/brain/80b5301e-d076-4ec5-aef3-d3844ffd990d/scratch/esp32-s3-touch-amoled-1_75-magsafe-lid-slicer.stl")
