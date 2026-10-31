import os
import sys
import json
import zipfile
import subprocess
import shutil
import math
import struct
import re

BASE_DIR = "/Users/aubreyhan/Tools/git_repo/PersonalDev/3D"
BAMBU_CLI = "/Applications/BambuStudio.app/Contents/MacOS/BambuStudio"
MACHINE_JSON = "/Applications/BambuStudio.app/Contents/Resources/profiles/BBL/machine/Bambu Lab X1 Carbon 0.4 nozzle.json"
FILAMENT_JSON = "/Applications/BambuStudio.app/Contents/Resources/profiles/BBL/filament/Bambu PETG HF @BBL X1C.json"

SRC_STL = os.path.join(BASE_DIR, "CapHanger6x (1).stl")
PREP_STL = os.path.join(BASE_DIR, "CapHanger6x_Prepared.stl")
FINAL_3MF = os.path.join(BASE_DIR, "CapHanger6x_X1C_PETG-HF.3mf")

def prepare_stl(src_file, dst_file):
    print(f"Reading original STL: {src_file}")
    with open(src_file, 'rb') as f:
        header = f.read(80)
        count = struct.unpack('<I', f.read(4))[0]
        triangles = []
        for _ in range(count):
            data = f.read(50)
            floats = struct.unpack('<12f', data[:48])
            normal = floats[:3]
            v1 = floats[3:6]
            v2 = floats[6:9]
            v3 = floats[9:12]
            triangles.append((normal, v1, v2, v3))

    min_z = min(min(v[2] for v in (v1, v2, v3)) for norm, v1, v2, v3 in triangles)

    # Rotate 45 degrees around Z axis to fit 256x256 bed
    rad = math.radians(45.0)
    cos_a = math.cos(rad)
    sin_a = math.sin(rad)

    def rot_xy(x, y):
        return (x * cos_a - y * sin_a, x * sin_a + y * cos_a)

    rot_min_x = rot_min_y = float('inf')
    rot_max_x = rot_max_y = float('-inf')

    for norm, v1, v2, v3 in triangles:
        for v in (v1, v2, v3):
            rx, ry = rot_xy(v[0], v[1])
            if rx < rot_min_x: rot_min_x = rx
            if rx > rot_max_x: rot_max_x = rx
            if ry < rot_min_y: rot_min_y = ry
            if ry > rot_max_y: rot_max_y = ry

    target_cx = (rot_min_x + rot_max_x) / 2.0
    target_cy = (rot_min_y + rot_max_y) / 2.0

    final_min_x = final_min_y = final_min_z = float('inf')
    final_max_x = final_max_y = final_max_z = float('-inf')

    with open(dst_file, 'wb') as f:
        f.write(header)
        f.write(struct.pack('<I', count))
        for norm, v1, v2, v3 in triangles:
            tnorm = (norm[0]*cos_a - norm[1]*sin_a, norm[0]*sin_a + norm[1]*cos_a, norm[2])
            tv1 = (v1[0]*cos_a - v1[1]*sin_a - target_cx, v1[0]*sin_a + v1[1]*cos_a - target_cy, v1[2] - min_z)
            tv2 = (v2[0]*cos_a - v2[1]*sin_a - target_cx, v2[0]*sin_a + v2[1]*cos_a - target_cy, v2[2] - min_z)
            tv3 = (v3[0]*cos_a - v3[1]*sin_a - target_cx, v3[0]*sin_a + v3[1]*cos_a - target_cy, v3[2] - min_z)
            for tv in (tv1, tv2, tv3):
                if tv[0] < final_min_x: final_min_x = tv[0]
                if tv[0] > final_max_x: final_max_x = tv[0]
                if tv[1] < final_min_y: final_min_y = tv[1]
                if tv[1] > final_max_y: final_max_y = tv[1]
                if tv[2] < final_min_z: final_min_z = tv[2]
                if tv[2] > final_max_z: final_max_z = tv[2]
            
            packed = struct.pack('<12fH', 
                tnorm[0], tnorm[1], tnorm[2],
                tv1[0], tv1[1], tv1[2],
                tv2[0], tv2[1], tv2[2],
                tv3[0], tv3[1], tv3[2],
                0
            )
            f.write(packed)

    print(f"Prepared STL saved to: {dst_file}")
    print(f"  Dimensions: {final_max_x - final_min_x:.2f} x {final_max_y - final_min_y:.2f} x {final_max_z - final_min_z:.2f} mm")
    print(f"  Z range: [{final_min_z:.2f}, {final_max_z:.2f}] mm")

def build_3mf():
    prepare_stl(SRC_STL, PREP_STL)

    raw_3mf_path = "/tmp/raw_cap_hanger.3mf"
    unpack_dir = "/tmp/unpack_cap_hanger"
    inter_3mf_path = "/tmp/inter_cap_hanger.3mf"
    slice_out_dir = "/tmp/slice_cap_hanger"

    # Clean previous temp files
    for p in [raw_3mf_path, inter_3mf_path]:
        if os.path.exists(p): os.remove(p)
    for d in [unpack_dir, slice_out_dir]:
        if os.path.exists(d): shutil.rmtree(d)
    os.makedirs(unpack_dir, exist_ok=True)
    os.makedirs(slice_out_dir, exist_ok=True)

    # 1. Custom process profile for PETG-HF on X1C with Textured PEI Plate
    custom_process = {
        "type": "process",
        "name": "0.20mm PETG-HF Cap Hanger @X1C",
        "inherits": "0.20mm Standard @BBL X1C",
        "from": "user",
        "setting_id": "UP_CAP_HANGER_01",
        "instantiation": "true",
        "wall_loops": "4",
        "top_shell_layers": "5",
        "bottom_shell_layers": "5",
        "sparse_infill_pattern": "gyroid",
        "sparse_infill_density": "25%",
        "brim_type": "outer_only",
        "brim_width": "5",
        "enable_support": "0",
        "initial_layer_speed": ["40", "40"],
        "initial_layer_infill_speed": ["50", "50"],
        "outer_wall_speed": ["120", "120"],
        "inner_wall_speed": ["180", "180"],
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

    temp_process_path = "/tmp/bambu_hanger_process.json"
    with open(temp_process_path, "w") as f:
        json.dump(custom_process, f, indent=4)

    # 2. Run BambuStudio CLI to export base 3MF
    print("Exporting initial 3MF from BambuStudio CLI...")
    cmd = [
        BAMBU_CLI,
        "--load-settings", f"{MACHINE_JSON};{temp_process_path}",
        "--load-filaments", FILAMENT_JSON,
        "--export-3mf", raw_3mf_path,
        PREP_STL
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        print("Error exporting 3MF:", res.stderr)
        sys.exit(1)

    # 3. Unpack 3MF
    with zipfile.ZipFile(raw_3mf_path, 'r') as z:
        z.extractall(unpack_dir)

    # 4. Modify project_settings.config to guarantee exact PETG-HF + Textured PEI Plate parameters
    proj_cfg_path = os.path.join(unpack_dir, "Metadata", "project_settings.config")
    with open(proj_cfg_path, 'r') as f:
        proj_cfg = json.load(f)

    proj_cfg["curr_bed_type"] = "Textured PEI Plate"
    proj_cfg["textured_plate_temp"] = ["70"]
    proj_cfg["textured_plate_temp_initial_layer"] = ["70"]
    proj_cfg["nozzle_temperature"] = ["245"]
    proj_cfg["nozzle_temperature_initial_layer"] = ["245"]
    proj_cfg["additional_cooling_fan_speed"] = ["0"]   # Auxiliary fan speed 0%
    proj_cfg["close_fan_the_first_x_layers"] = ["3"]
    proj_cfg["fan_min_speed"] = ["10"]
    proj_cfg["fan_max_speed"] = ["40"]
    proj_cfg["wall_loops"] = "4"
    proj_cfg["top_shell_layers"] = "5"
    proj_cfg["bottom_shell_layers"] = "5"
    proj_cfg["brim_type"] = "outer_only"
    proj_cfg["brim_width"] = "5"
    proj_cfg["enable_support"] = "0"
    proj_cfg["sparse_infill_pattern"] = "gyroid"
    proj_cfg["sparse_infill_density"] = "25%"
    proj_cfg["process_settings_id"] = "0.20mm PETG-HF Cap Hanger @X1C"

    with open(proj_cfg_path, 'w') as f:
        json.dump(proj_cfg, f, indent=4)

    # 5. Position object dead-center on 256x256 bed (128.0, 128.0, 0.0)
    model_xml_path = os.path.join(unpack_dir, "3D", "3dmodel.model")
    with open(model_xml_path, 'r') as f:
        content = f.read()

    content = re.sub(
        r'<item\s+objectid="2"\s+p:UUID="([^"]+)"\s+transform="[^"]+"\s+printable="1"/>',
        r'<item objectid="2" p:UUID="\1" transform="1 0 0 0 1 0 0 0 1 128 128 0" printable="1"/>',
        content
    )
    with open(model_xml_path, 'w') as f:
        f.write(content)

    # 6. Repack into intermediate 3MF
    with zipfile.ZipFile(inter_3mf_path, 'w', zipfile.ZIP_DEFLATED) as zout:
        for root, dirs, files in os.walk(unpack_dir):
            for file in files:
                full = os.path.join(root, file)
                rel = os.path.relpath(full, unpack_dir)
                zout.write(full, rel)

    # 7. Slicing with BambuStudio CLI
    print("Slicing plate with BambuStudio CLI...")
    cmd_slice = [
        BAMBU_CLI,
        "--slice", "0",
        "--outputdir", slice_out_dir,
        inter_3mf_path
    ]
    res_slice = subprocess.run(cmd_slice, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res_slice.returncode != 0:
        print("Slice error:", res_slice.stderr)
        sys.exit(1)

    gcode_path = os.path.join(slice_out_dir, "plate_1.gcode")
    result_json_path = os.path.join(slice_out_dir, "result.json")
    assert os.path.exists(gcode_path), "plate_1.gcode not produced!"

    with open(result_json_path) as f:
        slice_result = json.load(f)

    plate_info = slice_result['sliced_plates'][0]
    pred_time_sec = plate_info['total_predication']
    hours = int(pred_time_sec // 3600)
    minutes = int((pred_time_sec % 3600) // 60)
    filament_used_g = plate_info.get('total_weight', 0.0)
    print(f"Slice SUCCESS!")
    print(f"  Estimated print time: {hours}h {minutes}m ({pred_time_sec:.1f}s)")
    print(f"  Filament used: {filament_used_g:.1f}g")

    # 8. Copy slice thumbnails and plate gcode into final 3MF
    shutil.copyfile(inter_3mf_path, FINAL_3MF)
    with zipfile.ZipFile(FINAL_3MF, 'a', zipfile.ZIP_DEFLATED) as zout:
        zout.write(gcode_path, "Metadata/plate_1.gcode")
        # Add generated thumbnails if any
        for thumb in ["plate_1.png", "plate_1_small.png", "plate_no_light_1.png", "pick_1.png", "top_1.png"]:
            thumb_p = os.path.join(slice_out_dir, thumb)
            if os.path.exists(thumb_p):
                zout.write(thumb_p, f"Metadata/{thumb}")

    print(f"\nFinal sliced 3MF project generated at:")
    print(f"  {FINAL_3MF} ({os.path.getsize(FINAL_3MF) / 1024:.1f} KB)")
    return FINAL_3MF, hours, minutes, filament_used_g

if __name__ == "__main__":
    build_3mf()
