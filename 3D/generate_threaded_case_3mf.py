#!/usr/bin/env python3
"""
Peak-Optimized Bambu Lab X1C .3mf Project Generator for
ESP32-S3-Touch-AMOLED-1.75-G Threaded Enclosure (PETG-HF @ Textured PEI Plate).
"""

import os
import sys
import json
import zipfile
import subprocess
import shutil
import re

BASE_DIR = "/Users/aubreyhan/Tools/git_repo/PersonalDev/3D"
BAMBU_CLI = "/Applications/BambuStudio.app/Contents/MacOS/BambuStudio"
MACHINE_JSON = "/Applications/BambuStudio.app/Contents/Resources/profiles/BBL/machine/Bambu Lab X1 Carbon 0.4 nozzle.json"
FILAMENT_JSON = "/Applications/BambuStudio.app/Contents/Resources/profiles/BBL/filament/Bambu PETG HF @BBL X1C.json"

def create_threaded_3mf():
    out_3mf_name = "ESP32_S3_AMOLED_Threaded_Case_X1C_PETG-HF.3mf"
    print(f"\n=======================================================")
    print(f"Generating Peak-Optimized Bambu X1C 3MF: {out_3mf_name}")
    print(f"=======================================================")

    body_stl = os.path.join(BASE_DIR, "ESP32_S3_AMOLED_Body_Threaded.stl")
    lid_stl = os.path.join(BASE_DIR, "ESP32_S3_AMOLED_Lid_Threaded.stl")
    raw_3mf_path = os.path.join("/tmp", "raw_" + out_3mf_name)
    final_3mf_path = os.path.join(BASE_DIR, out_3mf_name)

    # 1. Custom High-Precision Process Profile
    custom_process = {
        "type": "process",
        "name": "0.16mm Optimal PETG-HF Threaded Case @X1C",
        "inherits": "0.16mm Optimal @BBL X1C",
        "from": "user",
        "setting_id": "UP_THREADED_OPT_01",
        "instantiation": "true",
        "description": "Peak-performance profile for ESP32-S3 AMOLED threaded enclosure on Bambu X1C with PETG-HF.",
        "layer_height": "0.16",
        "initial_layer_print_height": "0.20",
        "wall_loops": "4",
        "top_shell_layers": "6",
        "bottom_shell_layers": "5",
        "sparse_infill_pattern": "gyroid",
        "sparse_infill_density": "25%",
        "brim_type": "no_brim",
        "seam_position": "back",
        "seam_slope_type": "all",
        "precise_outer_wall": "1",
        "wipe_on_loops": "1",
        "reduce_infill_retraction": "1",
        "auxiliary_fan": 0,
        "additional_cooling_fan_speed": ["0"],
        "close_fan_the_first_x_layers": ["3"],
        "fan_min_speed": ["10"],
        "fan_max_speed": ["40"],
        "overhang_fan_speed": ["90"],
        "initial_layer_speed": ["35", "35"],
        "initial_layer_infill_speed": ["45", "45"],
        "outer_wall_speed": ["50", "50"],
        "inner_wall_speed": ["100", "100"],
        "top_surface_speed": ["50", "50"],
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

    temp_process_path = "/tmp/bambu_petg_hf_threaded.json"
    with open(temp_process_path, "w") as f:
        json.dump(custom_process, f, indent=4)

    # 2. Export base 3MF container with Bambu Studio CLI
    cmd_base = [
        BAMBU_CLI,
        "--load-settings", f"{MACHINE_JSON};{temp_process_path}",
        "--load-filaments", FILAMENT_JSON,
        "--export-3mf", raw_3mf_path,
        body_stl, lid_stl
    ]
    res = subprocess.run(cmd_base, capture_output=True, text=True)
    if res.returncode != 0:
        print("Error exporting base 3MF:", res.stderr)
        sys.exit(1)

    # 3. Unpack 3MF to inject optimal Transforms & Settings
    unpack_dir = os.path.join("/tmp", "unpack_" + out_3mf_name)
    if os.path.exists(unpack_dir):
        shutil.rmtree(unpack_dir)
    os.makedirs(unpack_dir, exist_ok=True)

    with zipfile.ZipFile(raw_3mf_path, 'r') as z:
        z.extractall(unpack_dir)

    # Inject project_settings.config
    proj_cfg_path = os.path.join(unpack_dir, "Metadata", "project_settings.config")
    if os.path.exists(proj_cfg_path):
        with open(proj_cfg_path, 'r') as f:
            proj_cfg = json.load(f)

        proj_cfg["curr_bed_type"] = "Textured PEI Plate"
        proj_cfg["textured_plate_temp"] = ["70"]
        proj_cfg["textured_plate_temp_initial_layer"] = ["70"]
        proj_cfg["nozzle_temperature"] = ["245"]
        proj_cfg["nozzle_temperature_initial_layer"] = ["245"]
        proj_cfg["auxiliary_fan"] = ["0"]
        proj_cfg["additional_cooling_fan_speed"] = ["0"]
        proj_cfg["close_fan_the_first_x_layers"] = ["3"]
        proj_cfg["fan_min_speed"] = ["10"]
        proj_cfg["fan_max_speed"] = ["40"]
        proj_cfg["overhang_fan_speed"] = ["90"]
        proj_cfg["layer_height"] = "0.16"
        proj_cfg["initial_layer_print_height"] = "0.20"
        proj_cfg["wall_loops"] = "4"
        proj_cfg["top_shell_layers"] = "6"
        proj_cfg["bottom_shell_layers"] = "5"
        proj_cfg["sparse_infill_pattern"] = "gyroid"
        proj_cfg["sparse_infill_density"] = "25%"
        proj_cfg["seam_position"] = "back"
        proj_cfg["precise_outer_wall"] = "1"
        proj_cfg["wipe_on_loops"] = "1"
        proj_cfg["outer_wall_speed"] = ["50", "50"]
        proj_cfg["inner_wall_speed"] = ["100", "100"]

        with open(proj_cfg_path, 'w') as f:
            json.dump(proj_cfg, f, indent=4)

    # 4. Orient Body 180° so front screen bezel touches textured PEI bed
    model_xml_path = os.path.join(unpack_dir, "3D", "3dmodel.model")
    with open(model_xml_path, 'r') as f:
        content = f.read()

    items = re.findall(r'<item\s+objectid="(\d+)"\s+p:UUID="([^"]+)"\s+transform="[^"]+"\s+printable="1"/>', content)
    if len(items) >= 2:
        id1, uuid1 = items[0]
        id2, uuid2 = items[1]
        
        # Body (id1): Flipped 180° around X axis so front face is flat on bed
        t1 = f'<item objectid="{id1}" p:UUID="{uuid1}" transform="1 0 0 0 -1 0 0 0 -1 126.63094 88.25 13.0" printable="1"/>'
        # Lid (id2): Upright orientation flat on bed
        t2 = f'<item objectid="{id2}" p:UUID="{uuid2}" transform="1 0 0 0 1 0 0 0 1 73.71905 111.39995 2" printable="1"/>'

        content = re.sub(rf'<item\s+objectid="{id1}"\s+p:UUID="{uuid1}"\s+transform="[^"]+"\s+printable="1"/>', t1, content)
        content = re.sub(rf'<item\s+objectid="{id2}"\s+p:UUID="{uuid2}"\s+transform="[^"]+"\s+printable="1"/>', t2, content)

        with open(model_xml_path, 'w') as f:
            f.write(content)

    # 5. Pack into intermediate 3MF
    inter_3mf_path = os.path.join("/tmp", "inter_" + out_3mf_name)
    with zipfile.ZipFile(inter_3mf_path, 'w', zipfile.ZIP_DEFLATED) as zout:
        for root, dirs, files in os.walk(unpack_dir):
            for file in files:
                full = os.path.join(root, file)
                rel = os.path.relpath(full, unpack_dir)
                zout.write(full, rel)

    # 6. Slicing with BambuStudio CLI to generate Plate G-code
    slice_out_dir = os.path.join("/tmp", "slice_threaded_x1c")
    if os.path.exists(slice_out_dir):
        shutil.rmtree(slice_out_dir)
    os.makedirs(slice_out_dir, exist_ok=True)

    cmd_slice = [
        BAMBU_CLI,
        "--slice", "0",
        "--outputdir", slice_out_dir,
        inter_3mf_path
    ]
    print("Running BambuStudio CLI slice engine...")
    res_slice = subprocess.run(cmd_slice, capture_output=True, text=True)

    gcode_path = os.path.join(slice_out_dir, "plate_1.gcode")
    result_json_path = os.path.join(slice_out_dir, "result.json")

    # 7. Embed plate_1.gcode into final 3MF archive
    if os.path.exists(gcode_path):
        print(f"Generated G-code size: {os.path.getsize(gcode_path):,} bytes")
        shutil.copyfile(gcode_path, os.path.join(unpack_dir, "Metadata", "plate_1.gcode"))
        
        # Repack into final 3MF
        with zipfile.ZipFile(final_3mf_path, 'w', zipfile.ZIP_DEFLATED) as zfinal:
            for root, dirs, files in os.walk(unpack_dir):
                for file in files:
                    full = os.path.join(root, file)
                    rel = os.path.relpath(full, unpack_dir)
                    zfinal.write(full, rel)
                    
        with open(result_json_path) as f:
            slice_res = json.load(f)
        est_sec = slice_res['sliced_plates'][0]['total_predication']
        print(f"SUCCESS! Embedded G-code into {out_3mf_name}")
        print(f"  Estimated print time: {est_sec/60.0:.1f} minutes")
    else:
        shutil.copyfile(inter_3mf_path, final_3mf_path)
        print(f"Saved arranged 3MF: {final_3mf_path}")

if __name__ == "__main__":
    create_threaded_3mf()
