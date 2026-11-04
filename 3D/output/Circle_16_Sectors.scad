// ============================================================================
// Parametric 16-Sector Circle Model (16等分扇形圆模型 / 圆面积推导教具)
// Specifications:
//   - Circle Diameter: 120.0 mm (Radius = 60.0 mm)
//   - Thickness: 6.0 mm
//   - Number of Sectors: 16 (22.5° per sector)
//   - Print Clearance & Center Tip Relief for 16-piece physical assembly
// ============================================================================

$fn = 180;

/* [Dimensions / 基本尺寸] */
// 圆整体直径 (mm)
circle_diameter = 120.0;
// 扇形厚度 (mm)
thickness = 6.0;
// 等分数量
num_sectors = 16;

/* [Manufacturing & Print Tolerance / 打印公差与倒角] */
// 相邻扇形侧面单边装配间隙 (mm)，确保16块拼合不卡死
side_clearance = 0.10;
// 圆心尖端避空半径 (mm)，消除0.4mm喷嘴在22.5°尖角处的堆料干涉
center_relief_r = 0.60;
// 上下底面微倒角高度 (mm)，消除首层象脚并提升手感
edge_chamfer = 0.40;

/* [Render Mode / 输出模式] */
// 0 = 16等分圆盘拼合状态 (带径向微间距), 1 = 单个扇形块, 2 = 打印盘排版(径向展开2.5mm防粘连), 3 = 圆面积公式推导平行四边形排列
render_mode = 0; // [0, 1, 2, 3]
// 盘面展示/打印时的径向展开间距 (mm)
radial_explode = (render_mode == 2) ? 3.0 : 0.4;

radius = circle_diameter / 2.0;
sector_angle = 360.0 / num_sectors; // 22.5 deg

// 2D profile of a single sector with side clearance and center tip relief
module sector_2d(r, angle, clr, tip_r, delta_r = 0) {
    eff_r = r + delta_r;
    offset(r = -clr + delta_r) {
        difference() {
            intersection() {
                circle(r = r);
                polygon(points = [
                    [0, 0],
                    [r * 2 * cos(-angle / 2), r * 2 * sin(-angle / 2)],
                    [r * 2, 0],
                    [r * 2 * cos(angle / 2),  r * 2 * sin(angle / 2)]
                ]);
            }
            if (tip_r > 0) {
                circle(r = tip_r);
            }
        }
    }
}

// 3D single sector with top & bottom chamfers
module sector_3d() {
    hull() {
        // Bottom chamfer face
        linear_extrude(height = 0.01)
            sector_2d(radius, sector_angle, side_clearance, center_relief_r, -edge_chamfer);
        // Main body
        translate([0, 0, edge_chamfer])
            linear_extrude(height = thickness - 2 * edge_chamfer)
                sector_2d(radius, sector_angle, side_clearance, center_relief_r, 0);
        // Top chamfer face
        translate([0, 0, thickness - 0.01])
            linear_extrude(height = 0.01)
                sector_2d(radius, sector_angle, side_clearance, center_relief_r, -edge_chamfer);
    }
}

// Mode 0 & Mode 2: 16 sectors arranged in a circle
module circle_16_sectors(explode_dist = 0.4) {
    for (i = [0 : num_sectors - 1]) {
        ang = i * sector_angle;
        rotate([0, 0, ang])
            translate([explode_dist, 0, 0])
                color((i % 2 == 0) ? "#38bdf8" : "#f59e0b")
                    sector_3d();
    }
}

// Mode 3: Parallelogram arrangement (8 bottom + 8 top interleaved)
module parallelogram_16_sectors() {
    arc_step = 2 * radius * sin(sector_angle / 2);
    for (i = [0 : (num_sectors / 2) - 1]) {
        // Bottom row pointing up
        translate([i * arc_step, 0, 0])
            rotate([0, 0, 90])
                color("#38bdf8")
                    sector_3d();
        // Top row pointing down, interleaved
        translate([(i + 0.5) * arc_step, radius * cos(sector_angle / 2), 0])
            rotate([0, 0, -90])
                color("#f59e0b")
                    sector_3d();
    }
}

if (render_mode == 1) {
    sector_3d();
} else if (render_mode == 3) {
    parallelogram_16_sectors();
} else {
    circle_16_sectors(radial_explode);
}
