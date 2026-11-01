#!/usr/bin/env python3
"""
Generates production-ready Bambu Lab X1C .3mf Project Packages for
ESP32-S3-Touch-AMOLED-1.75-G MagSafe Enclosure.

Optimized for:
- Printer: Bambu Lab X1 Carbon 0.4 nozzle
- Filament: Bambu PETG-HF @BBL X1C (or PLA)
- Bed: Textured PEI Plate (70°C bed temp, aux fan 0% to prevent warp)
- Print Layout: Bottom Shell flat on bed (0,0,0), Top Bezel flipped 180° for textured front face.
"""

import os
import sys
import json
import zipfile
import subprocess
import shutil

BASE_DIR = "/Users/aubreyhan/Tools/git_repo/PersonalDev/3D"
BAMBU_CLI = "/Applications/BambuStudio.app/Contents/MacOS/BambuStudio"
MACHINE_JSON = "/Applications/BambuStudio.app/Contents/Resources/profiles/BBL/machine/Bambu Lab X1 Carbon 0.4 nozzle.json"
FILAMENT_JSON = "/Applications/BambuStudio.app/Contents/Resources/profiles/BBL/filament/Bambu PETG HF @BBL X1C.json"

def create_enclosure_3mf():
    out_3mf_name = "ESP32_S3_AMOLED_GPS_Case_X1C_PETG-HF.3mf"
    print(f"\n=======================================================")
    print(f"Generating Bambu X1C 3MF: {out_3mf_name}")
    print(f"=======================================================")

    custom_process = {
        "type": "process",
        "name": "0.20mm PETG-HF AMOLED Case @X1C",
        "inherits": "0.20mm Standard @BBL X1C",
        "from": "user",
        "setting_id": "UP_GPS_01",
        "instantiation": "true",
        "wall_loops": "4",
        "top_shell_layers": "5",
        "bottom_shell_layers": "5",
        "sparse_infill_pattern": "gyroid",
        "sparse_infill_density": "25%",
        "brim_type": "no_brim",
        "auxiliary_fan": 0,
        "additional_cooling_fan_speed": ["0"],
        "close_fan_the_first_x_layers": ["3"],
        "fan_min_speed": ["10"],
        "fan_max_speed": ["40"],
        "initial_layer_speed": ["40", "40"],
        "initial_layer_infill_speed": ["50", "50"],
        "outer_wall_speed": ["120", "120"],
        "inner_wall_speed": ["160", "160"],
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

    temp_process_path = "/tmp/bambu_petg_hf_gps_case.json"
    with open(temp_process_path, "w") as f:
        json.dump(custom_process, f, indent=4)

    f_bot = os.path.join(BASE_DIR, "ESP32_S3_AMOLED_GPS_Case_Bottom.stl")
    f_top = os.path.join(BASE_DIR, "ESP32_S3_AMOLED_GPS_Case_Top.stl")
    raw_3mf_path = os.path.join("/tmp", "raw_" + out_3mf_name)

    # 1. Run BambuStudio CLI to export base 3MF with both models
    cmd = [
        BAMBU_CLI,
        "--load-settings", f"{MACHINE_JSON};{temp_process_path}",
        "--load-filaments", FILAMENT_JSON,
        "--export-3mf", raw_3mf_path,
        f_bot, f_top
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        print("Error exporting 3MF:", res.stderr)
        sys.exit(1)

    # 2. Unpack 3MF to inject Textured PEI Plate and optimal bed transforms
    unpack_dir = os.path.join("/tmp", "unpack_" + out_3mf_name)
    if os.path.exists(unpack_dir):
        shutil.rmtree(unpack_dir)
    os.makedirs(unpack_dir, exist_ok=True)

    with zipfile.ZipFile(raw_3mf_path, 'r') as z:
        z.extractall(unpack_dir)

    # 3. Update project_settings.config
    proj_cfg_path = os.path.join(unpack_dir, "Metadata", "project_settings.config")
    with open(proj_cfg_path, 'r') as f:
        proj_cfg = json.load(f)

    proj_cfg["curr_bed_type"] = "Textured PEI Plate"
    proj_cfg["textured_plate_temp"] = ["70"]
    proj_cfg["textured_plate_temp_initial_layer"] = ["70"]
    proj_cfg["nozzle_temperature"] = ["245"]
    proj_cfg["nozzle_temperature_initial_layer"] = ["245"]
    proj_cfg["auxiliary_fan"] = ["0"]
    proj_cfg["wall_loops"] = "4"
    proj_cfg["top_shell_layers"] = "5"
    proj_cfg["bottom_shell_layers"] = "5"
    proj_cfg["brim_type"] = "no_brim"
    proj_cfg["sparse_infill_pattern"] = "gyroid"
    proj_cfg["sparse_infill_density"] = "25%"
    proj_cfg["process_settings_id"] = "0.20mm PETG-HF AMOLED Case @X1C"

    with open(proj_cfg_path, 'w') as f:
        json.dump(proj_cfg, f, indent=4)

    # 4. Position and arrange objects side-by-side on 256x256 bed:
    # Plate Center is (128, 128).
    # Object 1 (Bottom Shell): placed at X = 88.0, Y = 128.0 (flat on bed)
    # Object 2 (Top Bezel Shell): placed at X = 168.0, Y = 128.0 (flipped 180° around X so front bezel touches PEI bed)
    model_xml_path = os.path.join(unpack_dir, "3D", "3dmodel.model")
    with open(model_xml_path, 'r') as f:
        content = f.read()

    import re
    # We find all <item ... /> tags and replace their transform
    items = re.findall(r'<item\s+objectid="(\d+)"\s+p:UUID="([^"]+)"\s+transform="[^"]+"\s+printable="1"/>', content)
    if len(items) >= 2:
        id1, uuid1 = items[0]
        id2, uuid2 = items[1]
        # Item 1: Bottom shell flat at (88, 128, 0)
        # transform: [1 0 0, 0 1 0, 0 0 1, 88 128 0]
        t1 = f'<item objectid="{id1}" p:UUID="{uuid1}" transform="1 0 0 0 1 0 0 0 1 88 128 0" printable="1"/>'
        # Item 2: Top shell flipped 180 deg around X (matrix [1 0 0, 0 -1 0, 0 0 -1, 168 128 10.8])
        # After 180° X flip: top face at Z=10.8 becomes Z=0 resting on the textured bed!
        t2 = f'<item objectid="{id2}" p:UUID="{uuid2}" transform="1 0 0 0 -1 0 0 0 -1 168 128 10.8" printable="1"/>'

        # Replace first item
        content = re.sub(
            rf'<item\s+objectid="{id1}"\s+p:UUID="{uuid1}"\s+transform="[^"]+"\s+printable="1"/>',
            t1,
            content
        )
        # Replace second item
        content = re.sub(
            rf'<item\s+objectid="{id2}"\s+p:UUID="{uuid2}"\s+transform="[^"]+"\s+printable="1"/>',
            t2,
            content
        )
        with open(model_xml_path, 'w') as f:
            f.write(content)

    # 5. Repack into intermediate 3MF
    inter_3mf_path = os.path.join("/tmp", "inter_" + out_3mf_name)
    with zipfile.ZipFile(inter_3mf_path, 'w', zipfile.ZIP_DEFLATED) as zout:
        for root, dirs, files in os.walk(unpack_dir):
            for file in files:
                full = os.path.join(root, file)
                rel = os.path.relpath(full, unpack_dir)
                zout.write(full, rel)

    # 6. Slicing with BambuStudio CLI
    slice_out_dir = os.path.join("/tmp", "slice_" + out_3mf_name)
    if os.path.exists(slice_out_dir):
        shutil.rmtree(slice_out_dir)
    os.makedirs(slice_out_dir, exist_ok=True)

    cmd_slice = [
        BAMBU_CLI,
        "--slice", "0",
        "--outputdir", slice_out_dir,
        inter_3mf_path
    ]
    res_slice = subprocess.run(cmd_slice, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res_slice.returncode != 0:
        print("Slice note/warning:", res_slice.stderr)

    gcode_path = os.path.join(slice_out_dir, "plate_1.gcode")
    result_json_path = os.path.join(slice_out_dir, "result.json")

    final_3mf_path = os.path.join(BASE_DIR, out_3mf_name)
    shutil.copyfile(inter_3mf_path, final_3mf_path)

    if os.path.exists(gcode_path):
        with open(result_json_path) as f:
            slice_result = json.load(f)
        est_sec = slice_result['sliced_plates'][0]['total_predication']
        est_min = est_sec / 60.0
        print(f"Slice Succeeded! Estimated Print Time: {est_min:.1f} mins ({est_sec:.0f} seconds)")

        # Embed sliced gcode
        with zipfile.ZipFile(final_3mf_path, 'a', zipfile.ZIP_DEFLATED) as zout:
            zout.write(gcode_path, "Metadata/plate_1.gcode")

    print(f"Saved Final Production 3MF: {final_3mf_path} ({os.path.getsize(final_3mf_path)} bytes)")


if __name__ == "__main__":
    create_enclosure_3mf()
