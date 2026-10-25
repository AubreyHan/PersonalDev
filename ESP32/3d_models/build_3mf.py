"""构建适配 Bambu Lab X1C (0.4mm 喷嘴) + Bambu PETG-HF 的 3MF 切片工程文件。

包含：
  1. 旋合螺纹机身 (esp32-s3-touch-amoled-1_75-body.stl) - 含 12点钟 45° 2525 天线台与穿线通道
  2. 旋合螺纹 MagSafe 后盖 (esp32-s3-touch-amoled-1_75-magsafe-lid.stl)
  3. 45° 天线仓保护盖 (esp32-s3-touch-amoled-1_75-antenna-cover.stl)
  4. 针对 PETG 材料、螺纹旋合与天线透波优化的完整打印工艺预设
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
COVER_STL = os.path.join(HERE, "esp32-s3-touch-amoled-1_75-antenna-cover.stl")
OUT_3MF = os.path.join(HERE, "esp32-s3-touch-amoled-1_75-print-project.3mf")
BACKUP_3MF = os.path.join(HERE, "original_backup", "esp32-s3-touch-amoled-1_75-print-project.3mf")


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
    print("解析机身、后盖与天线盖 STL 模型...")
    v_body, t_body = parse_stl(BODY_STL)
    v_lid, t_lid = parse_stl(LID_STL)
    v_cover, t_cover = parse_stl(COVER_STL)

    z_center_b = (min(v[2] for v in v_body) + max(v[2] for v in v_body)) / 2.0
    z_center_l = (min(v[2] for v in v_lid) + max(v[2] for v in v_lid)) / 2.0
    z_center_c = (min(v[2] for v in v_cover) + max(v[2] for v in v_cover)) / 2.0

    print(f"  机身: 面片数 {len(t_body)}, 顶点数 {len(v_body)}")
    print(f"  后盖: 面片数 {len(t_lid)}, 顶点数 {len(v_lid)}")
    print(f"  天线盖: 面片数 {len(t_cover)}, 顶点数 {len(v_cover)}")

    temp_dir = tempfile.mkdtemp(prefix="bambu_3mf_")

    try:
        # 1. 从既有 3MF 提取模板资源 (图片与基础配置)
        source_3mf = OUT_3MF if os.path.exists(OUT_3MF) else BACKUP_3MF
        with zipfile.ZipFile(source_3mf, "r") as z_src:
            z_src.extractall(temp_dir)

        # 2. 写入各个零件的 3D/Objects 模型文件
        obj1_path = os.path.join(temp_dir, "3D", "Objects", "object_1.model")
        obj2_path = os.path.join(temp_dir, "3D", "Objects", "object_2.model")
        obj3_path = os.path.join(temp_dir, "3D", "Objects", "object_3.model")
        write_object_model(obj1_path, 1, "00010000-81cb-4c03-9d28-80fed5dfa1dc", v_body, t_body, z_center_b)
        write_object_model(obj2_path, 3, "00020000-81cb-4c03-9d28-80fed5dfa1dc", v_lid, t_lid, z_center_l)
        write_object_model(obj3_path, 5, "00030000-81cb-4c03-9d28-80fed5dfa1dc", v_cover, t_cover, z_center_c)

        # 写入 3D/_rels/3dmodel.model.rels (注册所有 3 个对象的引用关系)
        rels_path = os.path.join(temp_dir, "3D", "_rels", "3dmodel.model.rels")
        os.makedirs(os.path.dirname(rels_path), exist_ok=True)
        with open(rels_path, "w", encoding="utf-8") as f:
            f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
            f.write('<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n')
            f.write(' <Relationship Target="/3D/Objects/object_1.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n')
            f.write(' <Relationship Target="/3D/Objects/object_2.model" Id="rel-2" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n')
            f.write(' <Relationship Target="/3D/Objects/object_3.model" Id="rel-3" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n')
            f.write('</Relationships>\n')

        # 3. 写入 3D/3dmodel.model (托盘排布)
        # 热床排布：X1C 尺寸 256x256，机身放在 X=80, Y=125；后盖放在 X=165, Y=125；天线盖放在 X=125, Y=205
        model_3d_path = os.path.join(temp_dir, "3D", "3dmodel.model")
        with open(model_3d_path, "w", encoding="utf-8") as f:
            f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
            f.write('<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" xmlns:BambuStudio="http://schemas.bambulab.com/package/2021" xmlns:p="http://schemas.microsoft.com/3dmanufacturing/production/2015/06" requiredextensions="p">\n')
            f.write(' <metadata name="Application">BambuStudio-02.08.02.61</metadata>\n')
            f.write(' <metadata name="BambuStudio:3mfVersion">1</metadata>\n')
            f.write(' <metadata name="CreationDate">2026-10-25</metadata>\n')
            f.write(' <metadata name="ModificationDate">2026-10-25</metadata>\n')
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
            f.write('  <object id="6" p:UUID="00000003-61cb-4c03-9d28-80fed5dfa1dc" type="model">\n')
            f.write('   <components>\n')
            f.write(f'    <component p:path="/3D/Objects/object_3.model" objectid="5" p:UUID="00030000-b206-40ff-9872-83e8017abed1" transform="1 0 0 0 1 0 0 0 1 0 0 {z_center_c:.4f}"/>\n')
            f.write('   </components>\n')
            f.write('  </object>\n')
            f.write(' </resources>\n')
            f.write(' <build p:UUID="2c7c17d8-22b5-4d84-8835-1976022ea369">\n')
            f.write('  <item objectid="2" p:UUID="00000002-b1ec-4553-aec9-835e5b724bb4" transform="1 0 0 0 1 0 0 0 1 80.0 125.0 0" printable="1"/>\n')
            f.write('  <item objectid="4" p:UUID="00000004-b1ec-4553-aec9-835e5b724bb4" transform="1 0 0 0 1 0 0 0 1 165.0 125.0 0" printable="1"/>\n')
            f.write('  <item objectid="6" p:UUID="00000006-b1ec-4553-aec9-835e5b724bb4" transform="1 0 0 0 1 0 0 0 1 125.0 205.0 0" printable="1"/>\n')
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
            f.write('  <object id="6">\n')
            f.write('    <metadata key="name" value="esp32-s3-touch-amoled-1_75-antenna-cover.stl"/>\n')
            f.write('    <metadata key="extruder" value="1"/>\n')
            f.write(f'    <metadata face_count="{len(t_cover)}"/>\n')
            f.write('    <part id="5" subtype="normal_part" uuid="ef8ea843-eff0-4322-a547-ddc46ca9a133">\n')
            f.write('      <metadata key="name" value="esp32-s3-touch-amoled-1_75-antenna-cover.stl"/>\n')
            f.write(f'      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 {z_center_c:.4f} 0 0 0 1"/>\n')
            f.write('      <metadata key="source_file" value="esp32-s3-touch-amoled-1_75-antenna-cover.stl"/>\n')
            f.write('      <metadata key="source_object_id" value="0"/>\n')
            f.write('      <metadata key="source_volume_id" value="0"/>\n')
            f.write('      <metadata key="source_offset_x" value="0"/>\n')
            f.write('      <metadata key="source_offset_y" value="0"/>\n')
            f.write(f'      <metadata key="source_offset_z" value="{z_center_c:.4f}"/>\n')
            f.write(f'      <mesh_stat face_count="{len(t_cover)}" edges_fixed="0" degenerate_facets="0" facets_removed="0" facets_reversed="0" backwards_edges="0"/>\n')
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
            f.write('    <model_instance>\n')
            f.write('      <metadata key="object_id" value="6"/>\n')
            f.write('      <metadata key="instance_id" value="0"/>\n')
            f.write('      <metadata key="identify_id" value="69"/>\n')
            f.write('    </model_instance>\n')
            f.write('  </plate>\n')
            f.write('  <assemble>\n')
            f.write('  </assemble>\n')
            f.write('</config>\n')

        # 5. 更新 Metadata/project_settings.config (适配 Bambu X1C + Bambu PETG HF + 0.16mm 精密工艺)
        proj_cfg_path = os.path.join(temp_dir, "Metadata", "project_settings.config")
        with open(proj_cfg_path, "r", encoding="utf-8") as f:
            proj_data = json.load(f)

        proj_updates = {
            "printer_model": "Bambu Lab X1 Carbon",
            "printer_settings_id": "Bambu Lab X1 Carbon 0.4 nozzle",
            "print_settings_id": "0.16mm Thread Enclosure PETG @BBL X1C",
            "filament_settings_id": ["Bambu PETG HF @BBL X1C"],
            "filament_type": ["PETG"],
            "filament_vendor": ["Bambu Lab"],
            "nozzle_diameter": ["0.4"],
            "curr_bed_type": "Textured PEI Plate",
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
            "layer_height": "0.16",
            "initial_layer_print_height": "0.2",
            "wall_loops": "4",
            "top_shell_layers": "5",
            "bottom_shell_layers": "4",
            "sparse_infill_density": "25%",
            "sparse_infill_pattern": "gyroid",
            "outer_wall_speed": ["100"],
            "inner_wall_speed": ["150"],
            "precise_outer_wall": "1",
            "elefant_foot_compensation": "0.15",
            "bridge_flow": "0.9",
            "seam_position": "aligned",
            "enable_support": "1",
            "support_type": "tree(auto)",
            "support_style": "tree_slim",
            "support_threshold_angle": "35",
            "support_top_z_distance": "0.2",
            "support_bottom_z_distance": "0.2",
            "support_on_build_plate_only": "0",
        }
        proj_data.update(proj_updates)

        with open(proj_cfg_path, "w", encoding="utf-8") as f:
            json.dump(proj_data, f, indent=4)

        # 6. 打包输出
        with zipfile.ZipFile(OUT_3MF, "w", compression=zipfile.ZIP_DEFLATED) as z:
            for root, dirs, files in os.walk(temp_dir):
                for file in files:
                    full_p = os.path.join(root, file)
                    rel_p = os.path.relpath(full_p, temp_dir)
                    z.write(full_p, rel_p)

        print(f"成功更新 Bambu Studio 3MF 工程文件: {OUT_3MF}")

    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    generate_3mf()
