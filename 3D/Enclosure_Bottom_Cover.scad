// ==============================================================================
// 外壳底盖模型 (Enclosure Bottom Cover)
// 适配图纸规格：
// - 外形尺寸：54.0 mm x 54.0 mm (44mm孔距 + 2xR5外边距)
// - 四角圆角：R 5.0 mm (四个角为r5)
// - 螺丝孔位：44.0 mm x 44.0 mm 间距，适配 M3 螺丝
// - 底盖厚度：2.0 mm (底盖厚度2mm)
// ==============================================================================

$fn = 64; // 圆弧细分度，数值越大打印边缘越圆滑细腻

// --- 基础几何参数 ---
plate_size          = 54.0;  // 底板外边长 (mm)
plate_thickness     = 2.0;   // 底板厚度 (mm)
corner_radius       = 5.0;   // 四角外圆角半径 R5 (mm)

// --- M3 固定螺丝孔参数 ---
hole_pitch          = 44.0;  // 四角螺丝孔中心距 (mm, 44x44mm方形排布)
m3_hole_dia         = 3.4;   // M3 穿孔直径 (mm, 预留 0.4mm 3D打印公差)

// --- 沉头选项 (Countersink) ---
countersunk         = true;  // true: 平头沉头孔(DIN 7991), 螺丝头平齐不突出; false: 普通直通孔
countersunk_dia     = 6.4;   // 沉头最大外径 (mm)
countersunk_depth   = 1.5;   // 沉头锥角深度 (mm, 90度沉头锥)

// --- 可选功能：内嵌定位防侧滑台阶 (Alignment Lip) ---
has_inner_lip       = false; // 是否生成嵌入机身内壁的防侧滑止口台阶
lip_height          = 1.2;   // 定位台阶向上凸出高度 (mm)
lip_outer_size      = 49.5;  // 台阶外轮廓边长 (mm, 适配外壳内壁约50mm)
lip_wall            = 1.2;   // 台阶环壁厚 (mm)
lip_corner_r        = 2.75;  // 台阶外角圆角 (mm)

// --- 可选功能：内部 PCB 固定预留孔 (图纸中标注的 36mm 孔距) ---
has_pcb_holes       = false; // 是否在底盖上打穿 PCB 固定孔 (36mm横向孔距)
pcb_hole_pitch_x    = 36.0;  // PCB 横向孔距 (mm)
pcb_hole_pitch_y    = 44.0;  // PCB 纵向孔距 (mm)
pcb_hole_dia        = 2.5;   // PCB 螺丝孔径 (mm, 适用于 M2/M2.5)


// ==============================================================================
// 辅助建模模块
// ==============================================================================

// 二维圆角矩形
module rounded_square_2d(size, r) {
    w = size - 2 * r;
    offset(r = r) {
        square([w, w], center = true);
    }
}

// 三维带圆角挤出板
module rounded_plate(size, r, h) {
    linear_extrude(height = h) {
        rounded_square_2d(size, r);
    }
}

// 单个 M3 沉头/通孔切除几何体
module m3_screw_cutout(h, cs, cs_dia, cs_depth, hole_d) {
    if (cs) {
        // 底面沉头锥度 (Z从 0 到 cs_depth)
        translate([0, 0, -0.01])
            cylinder(d1 = cs_dia, d2 = hole_d, h = cs_depth + 0.01);
        // 穿透直通圆柱 (Z从 cs_depth 到 h + 0.02)
        translate([0, 0, cs_depth - 0.01])
            cylinder(d = hole_d, h = h - cs_depth + 0.03);
    } else {
        // 直通孔
        translate([0, 0, -0.01])
            cylinder(d = hole_d, h = h + 0.02);
    }
}

// 内嵌定位止口台阶 (如果启用)
module alignment_lip() {
    linear_extrude(height = lip_height) {
        difference() {
            rounded_square_2d(lip_outer_size, lip_corner_r);
            rounded_square_2d(lip_outer_size - 2 * lip_wall, max(0.5, lip_corner_r - lip_wall));
        }
    }
}


// ==============================================================================
// 主模型组合
// ==============================================================================

module enclosure_bottom_cover() {
    c_xy = hole_pitch / 2.0; // 22.0 mm
    
    difference() {
        union() {
            // 1. 主平板 (54x54mm, 四角R5, 厚度2mm)
            rounded_plate(plate_size, corner_radius, plate_thickness);
            
            // 2. 可选定位台阶
            if (has_inner_lip) {
                translate([0, 0, plate_thickness])
                    alignment_lip();
            }
        }
        
        // 3. 切除 4 个角上的 M3 螺丝孔 (孔距 44mm x 44mm)
        for (sx = [-1, 1]) {
            for (sy = [-1, 1]) {
                translate([sx * c_xy, sy * c_xy, 0])
                    m3_screw_cutout(plate_thickness + (has_inner_lip ? lip_height : 0),
                                    countersunk, countersunk_dia, countersunk_depth, m3_hole_dia);
            }
        }
        
        // 4. 可选 PCB 内部安装孔 (横向 36mm 孔距)
        if (has_pcb_holes) {
            pcb_cx = pcb_hole_pitch_x / 2.0; // 18.0 mm
            pcb_cy = pcb_hole_pitch_y / 2.0; // 22.0 mm
            for (sx = [-1, 1]) {
                for (sy = [-1, 1]) {
                    translate([sx * pcb_cx, sy * pcb_cy, -0.01])
                        cylinder(d = pcb_hole_dia, h = plate_thickness + 0.02);
                }
            }
        }
    }
}

// 渲染模型
enclosure_bottom_cover();
