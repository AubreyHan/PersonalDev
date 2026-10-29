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

def build_project_3mf(stl_filename, out_3mf_filename, model_title):
    print(f"\n=======================================================")
    print(f"Creating 3MF: {out_3mf_filename} for {stl_filename}")
    print(f"=======================================================")
    
    # 1. Custom process profile for PETG-HF on X1C with Textured PEI Plate
    custom_process = {
        "type": "process",
        "name": "0.20mm PETG-HF Bottom Cover @X1C",
        "inherits": "0.20mm Standard @BBL X1C",
        "from": "user",
        "setting_id": "UP001",
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
        "outer_wall_speed": ["150", "150"],
        "inner_wall_speed": ["200", "200"],
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
    
    temp_process_path = "/tmp/bambu_petg_hf_process.json"
    with open(temp_process_path, "w") as f:
        json.dump(custom_process, f, indent=4)
        
    stl_path = os.path.join(BASE_DIR, stl_filename)
    raw_3mf_path = os.path.join("/tmp", "raw_" + out_3mf_filename)
    
    # 2. Run BambuStudio CLI to export base 3MF
    cmd = [
        BAMBU_CLI,
        "--load-settings", f"{MACHINE_JSON};{temp_process_path}",
        "--load-filaments", FILAMENT_JSON,
        "--export-3mf", raw_3mf_path,
        stl_path
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        print("Error exporting 3MF:", res.stderr)
        sys.exit(1)
        
    # 3. Unpack 3MF to inject Textured PEI Plate and center object
    unpack_dir = os.path.join("/tmp", "unpack_" + out_3mf_filename)
    if os.path.exists(unpack_dir):
        shutil.rmtree(unpack_dir)
    os.makedirs(unpack_dir, exist_ok=True)
    
    with zipfile.ZipFile(raw_3mf_path, 'r') as z:
        z.extractall(unpack_dir)
        
    # 4. Modify project_settings.config
    proj_cfg_path = os.path.join(unpack_dir, "Metadata", "project_settings.config")
    with open(proj_cfg_path, 'r') as f:
        proj_cfg = json.load(f)
        
    proj_cfg["curr_bed_type"] = "Textured PEI Plate"
    proj_cfg["textured_plate_temp"] = ["70"]
    proj_cfg["textured_plate_temp_initial_layer"] = ["70"]
    proj_cfg["nozzle_temperature"] = ["245"]
    proj_cfg["nozzle_temperature_initial_layer"] = ["245"]
    proj_cfg["auxiliary_fan"] = 0
    proj_cfg["additional_cooling_fan_speed"] = ["0"]
    proj_cfg["wall_loops"] = 4
    proj_cfg["top_shell_layers"] = 5
    proj_cfg["bottom_shell_layers"] = 5
    proj_cfg["brim_type"] = "no_brim"
    proj_cfg["sparse_infill_pattern"] = "gyroid"
    proj_cfg["sparse_infill_density"] = "25%"
    proj_cfg["process_settings_id"] = "0.20mm PETG-HF Bottom Cover @X1C"
    
    with open(proj_cfg_path, 'w') as f:
        json.dump(proj_cfg, f, indent=4)

    # 5. Center object on bed (128.0, 128.0, 0.0)
    model_xml_path = os.path.join(unpack_dir, "3D", "3dmodel.model")
    with open(model_xml_path, 'r') as f:
        content = f.read()
    # Replace transform in <item>
    # The default item transform is: transform="1 0 0 0 1 0 0 0 1 99.99999 100 0"
    # Object is 54x54 centered at (0,0). So placing at (128, 128, 0) centers it on 256x256 bed!
    import re
    content = re.sub(
        r'<item\s+objectid="2"\s+p:UUID="([^"]+)"\s+transform="[^"]+"\s+printable="1"/>',
        r'<item objectid="2" p:UUID="\1" transform="1 0 0 0 1 0 0 0 1 128 128 0" printable="1"/>',
        content
    )
    with open(model_xml_path, 'w') as f:
        f.write(content)
        
    # 6. Repack into intermediate 3MF
    inter_3mf_path = os.path.join("/tmp", "inter_" + out_3mf_filename)
    with zipfile.ZipFile(inter_3mf_path, 'w', zipfile.ZIP_DEFLATED) as zout:
        for root, dirs, files in os.walk(unpack_dir):
            for file in files:
                full = os.path.join(root, file)
                rel = os.path.relpath(full, unpack_dir)
                zout.write(full, rel)
                
    # 7. Perform real slicing with BambuStudio CLI
    slice_out_dir = os.path.join("/tmp", "slice_" + out_3mf_filename)
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
        print("Slice error:", res_slice.stderr)
        sys.exit(1)
        
    gcode_path = os.path.join(slice_out_dir, "plate_1.gcode")
    result_json_path = os.path.join(slice_out_dir, "result.json")
    assert os.path.exists(gcode_path), "plate_1.gcode not produced!"
    
    with open(result_json_path) as f:
        slice_result = json.load(f)
    print(f"Slice success! Return code: {slice_result['return_code']}, est time: {slice_result['sliced_plates'][0]['total_predication']:.1f}s")
    
    # 8. Embed plate_1.gcode into final 3MF for one-click print readiness
    final_3mf_path = os.path.join(BASE_DIR, out_3mf_filename)
    shutil.copyfile(inter_3mf_path, final_3mf_path)
    
    with zipfile.ZipFile(final_3mf_path, 'a', zipfile.ZIP_DEFLATED) as zout:
        zout.write(gcode_path, "Metadata/plate_1.gcode")
        
    print(f"Final 3MF created: {final_3mf_path} (size: {os.path.getsize(final_3mf_path)} bytes)")

if __name__ == "__main__":
    build_project_3mf(
        "Enclosure_Bottom_Cover_Countersunk.stl",
        "Enclosure_Bottom_Cover_Countersunk_X1C_PETG-HF.3mf",
        "Enclosure Bottom Cover (M3 Countersunk)"
    )
    build_project_3mf(
        "Enclosure_Bottom_Cover_Flat.stl",
        "Enclosure_Bottom_Cover_Flat_X1C_PETG-HF.3mf",
        "Enclosure Bottom Cover (M3 Flat Through)"
    )
    print("\nAll Bambu Studio 3MF projects generated successfully!")
