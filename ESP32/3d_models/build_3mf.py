"""构建适配 Bambu Lab X1C (0.4mm 喷嘴) + Bambu PETG-HF 的 3MF 切片工程文件。

包含：
  1. 旋合螺纹机身 (esp32-s3-touch-amoled-1_75-body.stl)
  2. 旋合螺纹 MagSafe 后盖 (esp32-s3-touch-amoled-1_75-magsafe-lid.stl)
  3. 针对 45° 梯形螺纹与 PETG 材料优化的完整打印工艺预设
"""

import json
import math
import os
import shutil
import struct
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
BODY_STL = os.path.join(HERE, "esp32-s3-touch-amoled-1_75-body.stl")
LID_STL = os.path.join(HERE, "esp32-s3-touch-amoled-1_75-magsafe-lid.stl")
OUT_3MF = os.path.join(HERE, "esp32-s3-touch-amoled-1_75-print-project.3mf")


def parse_stl(path):
    with open(path, "rb") as f:
        data = f.read()
    n_triangles = struct.unpack("<I", data[80:84])[0]
    v_map = {}
    vertices = []
    triangles = []
    for i in range(n_triangles):
        offset = 84 + i * 50
        floats = struct.unpack("<12f", data[offset:offset + 48])
        tri_v = []
        for j in range(3):
            vx = round(floats[3 + j * 3], 4)
            vy = round(floats[4 + j * 3], 4)
            vz = round(floats[5 + j * 3], 4)
            key = (vx, vy, vz)
            if key not in v_map:
                v_map[key] = len(vertices)
                vertices.append(key)
            tri_v.append(v_map[key])
        triangles.append(tri_v)
    return vertices, triangles


def write_object_model(xml_path, obj_id, uuid_str, vertices, triangles, z_offset):
    with open(xml_path, "w", encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        f.write('<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" xmlns:BambuStudio="http://schemas.bambulab.com/package/2021" xmlns:p="http://schemas.microsoft.com/3dmanufacturing/production/2015/06" requiredextensions="p">\n')
        f.write(' <metadata name="BambuStudio:3mfVersion">1</metadata>\n')
        f.write(' <resources>\n')
        f.write(f'  <object id="{obj_id}" p:UUID="{uuid_str}" type="model">\n')
        f.write('   <mesh>\n')
        f.write('    <vertices>\n')
        for vx, vy, vz in vertices:
            # 居中 Z 轴坐标
            f.write(f'     <vertex x="{vx:.4f}" y="{vy:.4f}" z="{vz - z_offset:.4f}"/>\n')
        f.write('    </vertices>\n')
        f.write('    <triangles>\n')
        for v1, v2, v3 in triangles:
            f.write(f'     <triangle v1="{v1}" v2="{v2}" v3="{v3}"/>\n')
        f.write('    </triangles>\n')
        f.write('   </mesh>\n')
        f.write('  </object>\n')
        f.write(' </resources>\n')
        f.write(' <build/>\n')
        f.write('</model>\n')


def generate_3mf():
    print("解析机身与后盖 STL 模型...")
    v_body, t_body = parse_stl(BODY_STL)
    v_lid, t_lid = parse_stl(LID_STL)

    z_min_b = min(v[2] for v in v_body)
    z_max_b = max(v[2] for v in v_body)
    z_center_b = (z_min_b + z_max_b) / 2.0

    z_min_l = min(v[2] for v in v_lid)
    z_max_l = max(v[2] for v in v_lid)
    z_center_l = (z_min_l + z_max_l) / 2.0

    print(f"  机身: 高度 {z_max_b - z_min_b:.2f}mm, 面片数 {len(t_body)}, 顶点数 {len(v_body)}")
    print(f"  后盖: 高度 {z_max_l - z_min_l:.2f}mm, 面片数 {len(t_lid)}, 顶点数 {len(v_lid)}")

    temp_dir = tempfile.mkdtemp(prefix="bambu_3mf_")

    try:
        # 1. 复制原工程中的基础配置和图片资源
        orig_extracted = "/tmp/x3mf"
        for item in os.listdir(orig_extracted):
            src = os.path.join(orig_extracted, item)
            dst = os.path.join(temp_dir, item)
            if os.path.isdir(src):
                shutil.copytree(src, dst)
            else:
                shutil.copy2(src, dst)

        # 2. 写入 3D/Objects/object_1.model (机身) 与 object_2.model (后盖)
        obj1_path = os.path.join(temp_dir, "3D", "Objects", "object_1.model")
        obj2_path = os.path.join(temp_dir, "3D", "Objects", "object_2.model")
        write_object_model(obj1_path, 1, "00010000-81cb-4c03-9d28-80fed5dfa1dc", v_body, t_body, z_center_b)
        write_object_model(obj2_path, 3, "00020000-81cb-4c03-9d28-80fed5dfa1dc", v_lid, t_lid, z_center_l)

        # 3. 写入 3D/3dmodel.model
        # 热床排布：X1C 尺寸 256x256，机身放在 X=85, Y=128；后盖放在 X=170, Y=128
        model_3d_path = os.path.join(temp_dir, "3D", "3dmodel.model")
        with open(model_3d_path, "w", encoding="utf-8") as f:
            f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
            f.write('<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" xmlns:BambuStudio="http://schemas.bambulab.com/package/2021" xmlns:p="http://schemas.microsoft.com/3dmanufacturing/production/2015/06" requiredextensions="p">\n')
            f.write(' <metadata name="Application">BambuStudio-02.08.02.61</metadata>\n')
            f.write(' <metadata name="BambuStudio:3mfVersion">1</metadata>\n')
            f.write(' <metadata name="CreationDate">2026-10-24</metadata>\n')
            f.write(' <metadata name="ModificationDate">2026-10-24</metadata>\n')
            f.write(' <resources>\n')
            f.write('  <object id="2" p:UUID="00000001-61cb-4c03-9d28-80fed5dfa1dc" type="model">\n')
            f.write('   <components>\n')
            f.write(f'    <component p:path="/3D/Objects/object_1.model" objectid="1" p:UUID="00010000-b206-40ff-9872-83e8017abed1" transform="1 0 0 0 1 0 0 0 1 0 0 {z_center_b:.4f}"/>\n')
            f.write('   </components>\n')
            f.write('  </object>\n')
            f.write('  <object id="4" p:UUID="00000002-61cb-4c03-9d28-80fed5dfa1dc" type="model">\n')
            f.write('   <components>\n')
            f.write(f'    <component p:path="/3D/Objects/object_2.model" objectid="3" p:UUID="00020000-b206-40ff-9872-83e8017abed1" transform="1 0 0 0 1 0 0 0 1 0 0 {z_center_l:.4f}"/>\n')
            f.write('   </components>\n')
            f.write('  </object>\n')
            f.write(' </resources>\n')
            f.write(' <build p:UUID="2c7c17d8-22b5-4d84-8835-1976022ea369">\n')
            f.write('  <item objectid="2" p:UUID="00000002-b1ec-4553-aec9-835e5b724bb4" transform="1 0 0 0 1 0 0 0 1 85.0 128.0 0" printable="1"/>\n')
            f.write('  <item objectid="4" p:UUID="00000004-b1ec-4553-aec9-835e5b724bb4" transform="1 0 0 0 1 0 0 0 1 170.0 128.0 0" printable="1"/>\n')
            f.write(' </build>\n')
            f.write('</model>\n')

        # 4. 写入 Metadata/model_settings.config
        model_cfg_path = os.path.join(temp_dir, "Metadata", "model_settings.config")
        with open(model_cfg_path, "w", encoding="utf-8") as f:
            f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
            f.write('<config>\n')
            f.write('  <object id="2">\n')
            f.write('    <metadata key="name" value="esp32-s3-touch-amoled-1_75-body.stl"/>\n')
            f.write('    <metadata key="extruder" value="1"/>\n')
            f.write(f'    <metadata face_count="{len(t_body)}"/>\n')
            f.write('    <part id="1" subtype="normal_part" uuid="d3d2ec03-193e-40dd-981e-680b9ddae7b8">\n')
            f.write('      <metadata key="name" value="esp32-s3-touch-amoled-1_75-body.stl"/>\n')
            f.write(f'      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 {z_center_b:.4f} 0 0 0 1"/>\n')
            f.write('      <metadata key="source_file" value="esp32-s3-touch-amoled-1_75-body.stl"/>\n')
            f.write('      <metadata key="source_object_id" value="0"/>\n')
            f.write('      <metadata key="source_volume_id" value="0"/>\n')
            f.write('      <metadata key="source_offset_x" value="0"/>\n')
            f.write('      <metadata key="source_offset_y" value="0"/>\n')
            f.write(f'      <metadata key="source_offset_z" value="{z_center_b:.4f}"/>\n')
            f.write(f'      <mesh_stat face_count="{len(t_body)}" edges_fixed="0" degenerate_facets="0" facets_removed="0" facets_reversed="0" backwards_edges="0"/>\n')
            f.write('    </part>\n')
            f.write('  </object>\n')
            f.write('  <object id="4">\n')
            f.write('    <metadata key="name" value="esp32-s3-touch-amoled-1_75-magsafe-lid.stl"/>\n')
            f.write('    <metadata key="extruder" value="1"/>\n')
            f.write(f'    <metadata face_count="{len(t_lid)}"/>\n')
            f.write('    <part id="3" subtype="normal_part" uuid="cc6ca843-eff0-4322-a547-ddc46ca9a122">\n')
            f.write('      <metadata key="name" value="esp32-s3-touch-amoled-1_75-magsafe-lid.stl"/>\n')
            f.write(f'      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 {z_center_l:.4f} 0 0 0 1"/>\n')
            f.write('      <metadata key="source_file" value="esp32-s3-touch-amoled-1_75-magsafe-lid.stl"/>\n')
            f.write('      <metadata key="source_object_id" value="0"/>\n')
            f.write('      <metadata key="source_volume_id" value="0"/>\n')
            f.write('      <metadata key="source_offset_x" value="0"/>\n')
            f.write('      <metadata key="source_offset_y" value="0"/>\n')
            f.write(f'      <metadata key="source_offset_z" value="{z_center_l:.4f}"/>\n')
            f.write(f'      <mesh_stat face_count="{len(t_lid)}" edges_fixed="0" degenerate_facets="0" facets_removed="0" facets_reversed="0" backwards_edges="0"/>\n')
            f.write('    </part>\n')
            f.write('  </object>\n')
            f.write('  <plate>\n')
            f.write('    <metadata key="plater_id" value="1"/>\n')
            f.write('    <metadata key="plater_name" value=""/>\n')
            f.write('    <metadata key="locked" value="false"/>\n')
            f.write('    <metadata key="filament_map_mode" value="Auto For Flush"/>\n')
            f.write('    <metadata key="gcode_file" value=""/>\n')
            f.write('    <metadata key="thumbnail_file" value="Metadata/plate_1.png"/>\n')
            f.write('    <metadata key="thumbnail_no_light_file" value="Metadata/plate_no_light_1.png"/>\n')
            f.write('    <metadata key="top_file" value="Metadata/top_1.png"/>\n')
            f.write('    <metadata key="pick_file" value="Metadata/pick_1.png"/>\n')
            f.write('    <model_instance>\n')
            f.write('      <metadata key="object_id" value="2"/>\n')
            f.write('      <metadata key="instance_id" value="0"/>\n')
            f.write('      <metadata key="identify_id" value="47"/>\n')
            f.write('    </model_instance>\n')
            f.write('    <model_instance>\n')
            f.write('      <metadata key="object_id" value="4"/>\n')
            f.write('      <metadata key="instance_id" value="0"/>\n')
            f.write('      <metadata key="identify_id" value="58"/>\n')
            f.write('    </model_instance>\n')
            f.write('  </plate>\n')
            f.write('  <assemble>\n')
            f.write('  </assemble>\n')
            f.write('</config>\n')

        # 5. 更新 Metadata/project_settings.config (适配 Bambu X1C + Bambu PETG HF + 0.16mm 精密工艺)
        proj_cfg_path = os.path.join(temp_dir, "Metadata", "project_settings.config")
        with open(proj_cfg_path, "r", encoding="utf-8") as f:
            proj_data = json.load(f)

        # 覆写关键工艺与耗材设置
        proj_updates = {
            "printer_model": "Bambu Lab X1 Carbon",
            "printer_settings_id": "Bambu Lab X1 Carbon 0.4 nozzle",
            "print_settings_id": "0.16mm Thread Enclosure PETG @BBL X1C",
            "filament_settings_id": ["Bambu PETG HF @BBL X1C"],
            "filament_type": ["PETG"],
            "filament_vendor": ["Bambu Lab"],
            "nozzle_diameter": ["0.4"],
            "curr_bed_type": "Textured PEI Plate",
            # 温度与风扇设置
            "nozzle_temperature": ["245"],
            "nozzle_temperature_initial_layer": ["230"],
            "textured_plate_temp": ["70"],
            "textured_plate_temp_initial_layer": ["70"],
            "hot_plate_temp": ["70"],
            "hot_plate_temp_initial_layer": ["70"],
            "fan_min_speed": ["20"],
            "fan_max_speed": ["60"],
            "overhang_fan_speed": ["90"],
            "filament_flow_ratio": ["0.95"],
            "filament_max_volumetric_speed": ["21"],
            # 螺纹外壳结构优化
            "layer_height": 0.16,
            "initial_layer_print_height": 0.2,
            "wall_loops": 4,
            "top_shell_layers": 5,
            "bottom_shell_layers": 4,
            "sparse_infill_density": "25%",
            "sparse_infill_pattern": "gyroid",
            "outer_wall_speed": ["100"],
            "inner_wall_speed": ["150"],
            "precise_outer_wall": 1,
            "elefant_foot_compensation": 0.15,
            "bridge_flow": 0.9,
            "seam_position": "aligned",
            # 树状支撑
            "enable_support": 1,
            "support_type": "tree(auto)",
            "support_threshold_angle": 35,
            "support_top_z_distance": 0.2,
            "support_bottom_z_distance": 0.2,
            "support_on_build_plate_only": 0,
        }
        proj_data.update(proj_updates)

        with open(proj_cfg_path, "w", encoding="utf-8") as f:
            json.dump(proj_data, f, indent=4)

        # 6. 打包为 .3mf 文件
        if os.path.exists(OUT_3MF):
            # 备份先前的 3mf
            backup_path = os.path.join(HERE, "original_backup", "esp32-s3-touch-amoled-1_75-print-project.3mf")
            if not os.path.exists(backup_path):
                shutil.copy2(OUT_3MF, backup_path)

        with zipfile.ZipFile(OUT_3MF, "w", compression=zipfile.ZIP_DEFLATED) as z:
            for root, dirs, files in os.walk(temp_dir):
                for file in files:
                    full_p = os.path.join(root, file)
                    rel_p = os.path.relpath(full_p, temp_dir)
                    z.write(full_p, rel_p)

        print(f"成功生成 Bambu Studio 3MF 工程文件: {OUT_3MF}")

    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    generate_3mf()
