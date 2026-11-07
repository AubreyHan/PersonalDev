#!/usr/bin/env python3
"""
Bambu Lab X1C + Bambu PETG Basic .3mf generator for obj_1_Filtre.stl
(Ø40 x 3 mm disc). Model geometry and position are NOT modified;
only machine / filament / process settings are optimized.
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
PROFILES = "/Applications/BambuStudio.app/Contents/Resources/profiles/BBL"
MACHINE_JSON = f"{PROFILES}/machine/Bambu Lab X1 Carbon 0.4 nozzle.json"
FILAMENT_JSON = f"{PROFILES}/filament/Bambu PETG Basic @BBL X1C.json"

STL = os.path.join(BASE_DIR, "obj_1_Filtre.stl")
OUT_NAME = "Filtre_X1C_PETG-Basic.3mf"

# Process overrides on top of "0.20mm Standard @BBL X1C"
# (also re-applied to project_settings.config)
PROCESS = {
    "layer_height": "0.2",
    "initial_layer_print_height": "0.2",
    # Shells: 3 mm part -> 0.8 bottom + 1.2 core + 1.0 top
    "wall_loops": "3",
    "top_shell_layers": "5",
    "top_shell_thickness": "1.0",
    "bottom_shell_layers": "4",
    "bottom_shell_thickness": "0",
    "sparse_infill_pattern": "gyroid",
    "sparse_infill_density": "25%",
    "top_surface_pattern": "monotonicline",
    "bottom_surface_pattern": "monotonic",
    "wall_infill_order": "inner wall/outer wall/infill",
    "seam_position": "back",
    "brim_type": "no_brim",
    "enable_support": "0",
    "ironing_type": "no ironing",
    "enable_arc_fitting": "1",
    "elefant_foot_compensation": "0.15",
    # Speeds for PETG (slicer still caps at 15 mm^3/s volumetric limit)
    "initial_layer_speed": ["40", "40"],
    "initial_layer_infill_speed": ["80", "80"],
    "outer_wall_speed": ["150", "150"],
    "inner_wall_speed": ["200", "200"],
    "top_surface_speed": ["150", "150"],
    "sparse_infill_speed": ["250", "250"],
    "internal_solid_infill_speed": ["200", "200"],
}

# Keep official Bambu PETG Basic @BBL X1C values (255/245 °C, fan 20-40%,
# flow 0.95, 15 mm^3/s); only select the recommended plate.
FILAMENT = {
    "curr_bed_type": "Textured PEI Plate",
    "textured_plate_temp": ["70"],
    "textured_plate_temp_initial_layer": ["70"],
}


def _index_profiles():
    idx = {}
    for sub in ("machine", "process", "filament"):
        d = os.path.join(PROFILES, sub)
        for fn in os.listdir(d):
            if fn.endswith(".json"):
                try:
                    with open(os.path.join(d, fn)) as f:
                        j = json.load(f)
                    idx[(sub, j.get("name", fn[:-5]))] = j
                except Exception:
                    pass
    return idx


_IDX = None


def resolve(sub, name_or_json):
    """Flatten Bambu system profile inheritance (inherits + include)."""
    global _IDX
    if _IDX is None:
        _IDX = _index_profiles()
    j = _IDX[(sub, name_or_json)] if isinstance(name_or_json, str) else name_or_json
    out = {}
    if j.get("inherits"):
        out.update(resolve(sub, j["inherits"]))
    for inc in j.get("include", []) or []:
        out.update(resolve(sub, inc))
    out.update({k: v for k, v in j.items() if k not in ("inherits", "include")})
    return out


def flatten_to(sub, name_or_json, path, **extra):
    p = resolve(sub, name_or_json)
    p.update(extra)
    p.pop("inherits", None)
    p["instantiation"] = "true"
    with open(path, "w") as f:
        json.dump(p, f, indent=2)
    return path


def zip_dir(src, dst):
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _, files in os.walk(src):
            for fn in files:
                full = os.path.join(root, fn)
                z.write(full, os.path.relpath(full, src))


def main():
    out_path = os.path.join(BASE_DIR, OUT_NAME)
    work = "/tmp/filtre_3mf"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    process = {
        "type": "process",
        "name": "0.20mm Optimal PETG Basic Filtre @X1C",
        "inherits": "0.20mm Standard @BBL X1C",
        "from": "user",
        "instantiation": "true",
        "compatible_printers": ["Bambu Lab X1 Carbon 0.4 nozzle"],
        **PROCESS,
    }
    proc_path = flatten_to("process", process, os.path.join(work, "process.json"))
    mach_path = flatten_to("machine", "Bambu Lab X1 Carbon 0.4 nozzle",
                           os.path.join(work, "machine.json"))
    fil_path = flatten_to("filament", "Bambu PETG Basic @BBL X1C",
                          os.path.join(work, "filament.json"))

    raw = os.path.join(work, "raw.3mf")
    res = subprocess.run([
        BAMBU_CLI,
        "--load-settings", f"{mach_path};{proc_path}",
        "--load-filaments", fil_path,
        "--export-3mf", raw, STL,
    ], capture_output=True, text=True)
    if not os.path.exists(raw):
        print("Export failed:", res.stdout[-2000:], res.stderr[-2000:])
        sys.exit(1)

    unpack = os.path.join(work, "unpack")
    with zipfile.ZipFile(raw) as z:
        z.extractall(unpack)

    cfg_path = os.path.join(unpack, "Metadata", "project_settings.config")
    with open(cfg_path) as f:
        cfg = json.load(f)
    for k, v in {**PROCESS, **FILAMENT}.items():
        if k in cfg and isinstance(cfg[k], list) and not isinstance(v, list):
            v = [v] * len(cfg[k])
        if k in cfg and isinstance(cfg[k], list) and isinstance(v, list) and len(v) != len(cfg[k]):
            v = [v[0]] * len(cfg[k])
        cfg[k] = v
    with open(cfg_path, "w") as f:
        json.dump(cfg, f, indent=4)

    # Keep the model exactly as supplied: identity build transform, so the
    # object sits at its original STL coordinates (no rotation / re-arrange).
    model_path = os.path.join(unpack, "3D", "3dmodel.model")
    with open(model_path) as f:
        xml = f.read()
    xml = re.sub(r'(<item\s+objectid="\d+"[^>]*?transform=")[^"]+(")',
                 r"\g<1>1 0 0 0 1 0 0 0 1 0 0 0\g<2>", xml)
    with open(model_path, "w") as f:
        f.write(xml)

    inter = os.path.join(work, "inter.3mf")
    zip_dir(unpack, inter)

    slice_dir = os.path.join(work, "slice")
    os.makedirs(slice_dir)
    res = subprocess.run([BAMBU_CLI, "--slice", "0", "--outputdir", slice_dir, inter],
                         capture_output=True, text=True)
    gcode = os.path.join(slice_dir, "plate_1.gcode")
    if os.path.exists(gcode):
        shutil.copyfile(gcode, os.path.join(unpack, "Metadata", "plate_1.gcode"))
        shutil.copyfile(gcode, os.path.join(work, "plate_1.gcode"))
        zip_dir(unpack, out_path)
        try:
            with open(os.path.join(slice_dir, "result.json")) as f:
                r = json.load(f)
            p = r["sliced_plates"][0]
            print(f"Estimated time: {p['total_predication'] / 60:.1f} min")
        except Exception as e:  # noqa
            print("result.json parse:", e)
        print("Saved (sliced):", out_path)
    else:
        print("Slice failed, saving unsliced project.", res.stdout[-1500:], res.stderr[-1500:])
        shutil.copyfile(inter, out_path)
        print("Saved:", out_path)


if __name__ == "__main__":
    main()
