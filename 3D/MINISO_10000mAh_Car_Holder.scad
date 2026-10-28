// ==============================================================================
// 名创优品 10000mAh 迷你自带双线 22.5W 移动电源车载固定底盒 (参数化设计)
// Car Mount Holder for MINISO 10000mAh Mini Dual-Cable Power Bank (22.5W)
// ==============================================================================
// 模型特点：
// 1. 易抽取设计：前后开大尺寸人体工学 U 型手指捏取槽，单手即可轻松提起取出充电。
// 2. 自带双线出线缺口：左右两侧开出线导槽，充电宝在底座内即可直接拉出 Type-C / 苹果线供电。
// 3. 车载专用固定底翼：加宽底座法兰，专为 3M VHB 汽车耐温无痕强力胶带设计，亦预留沉头螺丝孔。
// 4. 底部通风散热/推顶孔：防止 22.5W 快充发热积温，同时便于底部手指顶出及杂屑排出。
// 5. 顶部导向倒角：入口微倒喇叭口，行车途中单手盲插平滑无阻。
// ==============================================================================

$fn = 60; // 渲染圆滑度

// --- 充电宝测量与匹配参数 ---
pb_width      = 67.0; // 充电宝机身宽度 (mm)
pb_thickness  = 23.5; // 充电宝机身厚度 (mm)
pb_height     = 87.0; // 充电宝机身立放总高 (mm)

// --- 底盒工程公差与结构参数 ---
clearance_w   = 2.5;  // 宽度单侧间隙约 1.25mm，保证插拔顺畅不卡滞，且预留减震贴绒空间
clearance_d   = 2.0;  // 厚度单侧间隙约 1.00mm
wall_t        = 2.8;  // 壁厚 (mm)，适配 0.4mm 喷嘴 7 层壁厚，耐高温抗形变
floor_t       = 3.2;  // 底部承重底板厚度 (mm)
holder_h      = 50.0; // 底盒容纳高度 (mm)，外露约 37mm 方便手持

// --- 功能缺口参数 ---
finger_cutout_w     = 38.0; // 前后手指捏取槽宽度 (mm)
finger_cutout_depth = 31.0; // 手指捏取槽自顶部向下开口深度 (mm)
cable_slot_w        = 13.0; // 左右侧边自带数据线出线槽宽度 (mm)
cable_slot_depth    = 34.0; // 自带线出线槽开口深度 (mm)

// --- 车载底座固定翼参数 ---
flange_x      = 10.5; // 左右加宽底翼尺寸 (mm)
flange_y      = 8.5;  // 前后加宽底翼尺寸 (mm)
flange_r      = 6.0;  // 底板外边缘倒圆角半径 (mm)
screw_hole_d  = 4.2;  // M4 沉头螺丝孔径 (mm)
countersink_d = 8.0;  // 沉头直径 (mm)
countersink_h = 2.0;  // 沉头深度 (mm)

// --- 底部通风口参数 ---
vent_w        = 32.0; // 底部散热孔长 (mm)
vent_d        = 14.0; // 底部散热孔宽 (mm)

// --- 计算尺寸 ---
inner_w = pb_width + clearance_w;       // 69.5 mm
inner_d = pb_thickness + clearance_d;   // 25.5 mm
outer_w = inner_w + 2 * wall_t;         // 75.1 mm
outer_d = inner_d + 2 * wall_t;         // 31.1 mm
base_w  = outer_w + 2 * flange_x;       // 96.1 mm
base_d  = outer_d + 2 * flange_y;       // 48.1 mm
total_h = floor_t + holder_h;           // 53.2 mm

// 辅助模块：带圆角的矩形立方体
module rounded_box(w, d, h, r) {
    translate([-w/2 + r, -d/2 + r, 0])
    minkowski() {
        cube([w - 2*r, d - 2*r, h - 1]);
        cylinder(r=r, h=1);
    }
}

// 主体设计
module car_powerbank_holder() {
    difference() {
        // 1. 实心外形组合：底座法兰 + 垂直仓体
        union() {
            // 底部加宽固定法兰 (带圆角防割伤)
            rounded_box(base_w, base_d, floor_t, flange_r);
            
            // 垂直外壁主体 (带轻微外圆角)
            translate([0, 0, floor_t])
                rounded_box(outer_w, outer_d, holder_h, 2.0);
        }
        
        // 2. 减去内腔 (容纳充电宝)
        translate([-inner_w/2, -inner_d/2, floor_t])
            cube([inner_w, inner_d, holder_h + 2]);
            
        // 3. 减去顶部导向喇叭口 (45度导向斜切)
        translate([0, 0, total_h - 1.5])
            hull() {
                translate([-inner_w/2, -inner_d/2, 0])
                    cube([inner_w, inner_d, 0.1]);
                translate([-(inner_w+3)/2, -(inner_d+3)/2, 2])
                    cube([inner_w+3, inner_d+3, 0.1]);
            }
            
        // 4. 减去前壁与后壁的手指捏取 U 型缺口 (抽取更轻松)
        // 前壁缺口
        translate([-finger_cutout_w/2, -(outer_d+2)/2, total_h - finger_cutout_depth])
            cube([finger_cutout_w, wall_t + 2, finger_cutout_depth + 1]);
        translate([0, -outer_d/2, total_h - finger_cutout_depth])
            rotate([90, 0, 0])
            cylinder(d=finger_cutout_w, h=wall_t+2, center=true);
            
        // 后壁缺口
        translate([-finger_cutout_w/2, inner_d/2 - 1, total_h - finger_cutout_depth])
            cube([finger_cutout_w, wall_t + 2, finger_cutout_depth + 1]);
        translate([0, outer_d/2, total_h - finger_cutout_depth])
            rotate([90, 0, 0])
            cylinder(d=finger_cutout_w, h=wall_t+2, center=true);
            
        // 5. 减去左侧与右侧的自带数据线出线槽 (在盒内即可拉线充电)
        // 左侧槽
        translate([-(outer_w+2)/2, -cable_slot_w/2, total_h - cable_slot_depth])
            cube([wall_t + 2, cable_slot_w, cable_slot_depth + 1]);
        translate([-outer_w/2, 0, total_h - cable_slot_depth])
            rotate([0, 90, 0])
            cylinder(d=cable_slot_w, h=wall_t+2, center=true);
            
        // 右侧槽
        translate([inner_w/2 - 1, -cable_slot_w/2, total_h - cable_slot_depth])
            cube([wall_t + 2, cable_slot_w, cable_slot_depth + 1]);
        translate([outer_w/2, 0, total_h - cable_slot_depth])
            rotate([0, 90, 0])
            cylinder(d=cable_slot_w, h=wall_t+2, center=true);
            
        // 6. 减去底部中心通风与推顶通孔
        translate([-vent_w/2, -vent_d/2, -1])
            cube([vent_w, vent_d, floor_t + 2]);
            
        // 7. 减去左右底翼 M4 沉头螺丝孔 (可选螺丝或纯背胶固定)
        for (side = [-1, 1]) {
            hole_x = side * (outer_w/2 + flange_x/2);
            // 螺栓贯穿孔
            translate([hole_x, 0, -1])
                cylinder(d=screw_hole_d, h=floor_t + 2);
            // 沉头沉孔
            translate([hole_x, 0, floor_t - countersink_h])
                cylinder(d=countersink_d, h=countersink_h + 1);
        }
    }
}

// 实例化模型
car_powerbank_holder();
