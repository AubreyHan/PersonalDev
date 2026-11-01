// ==============================================================================
// 微雪 ESP32-S3-Touch-AMOLED-1.75-G (官方真实硬件布局版)
// MagSafe 磁吸轻薄卡扣外壳 - 3D 打印工程源码 (OpenSCAD)
//
// 官方真实物理接口坐标 (以屏幕正面为准):
// - 9 点钟方向 (West / -X): USB Type-C 接口
// - 8 点钟与 10 点钟:     ES7210 双麦克风孔 (MIC1 / MIC2, 148° 与 212°)
// - 3 点钟方向 (East / +X): PWR / TF卡槽 / BOOT(Reset) 右侧全景一体化大镂空窗 (320°~40°)
//                         (手指可直接触碰微动按键，TF卡轻松拔插)
// - 12 点钟方向 (North / +Y): 25x25mm GNSS 陶瓷天线一体化外置隔离仓 (超薄全密封透射罩)
// - 6 点钟方向 (South / -Y):  8-Pin 2.54mm 扩展排母避空保护区
// - 底部外侧:            MagSafe 磁吸嵌入平齐沉槽 (OD 54.2mm, ID 45.8mm, 深 1.2mm)
// ==============================================================================

$fn = 120; // 圆弧高精度切片

// ----------------- 1. 核心几何尺寸 (单位: mm) -----------------
pcb_dia          = 48.96;   // 官方 PCB 外径
pcb_tol          = 0.64;    // 3D 打印单边 0.32mm 装配公差
cavity_dia       = pcb_dia + pcb_tol; // 49.60mm 内腔
screen_view_dia  = 44.20;   // 官方正面有效视窗直径 (露出 43.76mm 显示区，压住外缘黑边)
glass_pocket_h   = 2.20;    // 屏幕玻璃内沉台阶厚度

wall_t           = 2.70;    // 主机外壁厚度
case_dia         = cavity_dia + wall_t * 2; // 外壳外径 55.00mm (与 54mm MagSafe 圈完美呼应)

bottom_floor_t   = 2.00;    // 底部基准厚度
parting_z        = 5.80;    // 上下壳卡扣分型面高度
total_height     = 10.80;   // 整机超薄厚度仅 10.8mm

// MagSafe 磁吸槽
magsafe_od       = 54.20;   // 磁吸环槽外径
magsafe_id       = 45.80;   // 磁吸环槽内径
magsafe_depth    = 1.20;    // 磁吸环沉槽深度 (与底面完全平齐)

// 25x25mm GNSS 陶瓷天线仓
ant_cav_w        = 25.60;   // 天线仓内净宽 (容纳 25x25mm 天线 + 0.6mm 公差)
ant_cav_l        = 25.60;   // 天线仓内净长
ant_wall_t       = 1.80;    // 天线仓壁厚
ant_pod_w        = ant_cav_w + ant_wall_t * 2; // 29.20mm
ant_pod_l        = ant_cav_l + ant_wall_t * 2; // 29.20mm
ant_center_y     = case_dia / 2 + ant_pod_l / 2 - 5.50; // 与主圆盘平滑交融过渡

// 真实接口尺寸
typec_w          = 11.20;   // Type-C 避空宽度
typec_h          = 5.60;    // Type-C 避空高度

// ----------------- 2. 2D 轮廓生成 -----------------
module case_2d_profile() {
    hull() {
        circle(d = case_dia);
        translate([0, ant_center_y, 0])
            offset(r = 3.5)
                square([ant_pod_w - 7.0, ant_pod_l - 7.0], center = true);
    }
}

// ----------------- 3. 底壳模块 (Bottom Shell) -----------------
module bottom_shell() {
    difference() {
        // 主体拉伸
        linear_extrude(height = parting_z, convexity = 10)
            case_2d_profile();

        // A. 底部 MagSafe 环形沉槽 (平整底面上的环形磁吸槽)
        translate([0, 0, -0.01])
            difference() {
                cylinder(d = magsafe_od, h = magsafe_depth + 0.01);
                cylinder(d = magsafe_id, h = magsafe_depth + 0.02);
            }

        // B. 内部 PCB 主腔
        translate([0, 0, bottom_floor_t])
            cylinder(d = cavity_dia, h = parting_z);

        // C. 12 点钟外置天线仓下半部内腔
        translate([0, ant_center_y, bottom_floor_t])
            rounded_box_center(ant_cav_w, ant_cav_l, parting_z + 1, r = 1.5);

        // D. IPEX 微同轴天线走线过孔
        translate([-1.8, cavity_dia / 2 - 5.0, bottom_floor_t])
            cube([3.6, ant_center_y - cavity_dia / 2 + 7.0, 2.4]);

        // E. 9 点钟方向 Type-C 接口开孔 (West, X = -23.8mm)
        translate([-case_dia / 2 - 4.0, -typec_w / 2, bottom_floor_t + 0.3])
            cube([10.0, typec_w, typec_h]);

        // F. 3 点钟方向 (East / +X) 一体化全景大镂空窗 (覆盖 PWR、TF卡槽、BOOT键)
        // 从 -40° 到 +40° (跨度 80°)，开放下半部
        difference() {
            cylinder(d = case_dia + 2.0, h = parting_z + 0.1);
            cylinder(d = cavity_dia - 0.1, h = parting_z + 0.2);
            // 扇形遮罩
            rotate([0, 0, -40])
                linear_extrude(height = parting_z + 1)
                    polygon(points = [[0, 0], [-40, 0], [-40, 40], [-40, -40], [0, -40], [40, -40]]);
        }

        // G. 4 处卡扣母槽 (避开右侧大镂空与左侧 Type-C)
        for (ang = [50, 130, 230, 310]) {
            rotate([0, 0, ang])
                translate([cavity_dia / 2 - 0.2, -3.0, parting_z - 1.8])
                    cube([1.2, 6.0, 1.6]);
        }
    }
}

// ----------------- 4. 上壳模块 (Top Bezel Shell) -----------------
module top_shell() {
    top_h = total_height - parting_z;

    difference() {
        union() {
            // 上壳主体拉伸
            translate([0, 0, parting_z])
                linear_extrude(height = top_h, convexity = 10)
                    case_2d_profile();

            // 4 处下探弹性卡扣公扣
            for (ang = [50, 130, 230, 310]) {
                rotate([0, 0, ang])
                    translate([cavity_dia / 2 - 0.7, -2.7, parting_z - 2.1])
                        snap_tab();
            }
        }

        // A. 正面 AMOLED 圆形视窗透光/触控开口
        translate([0, 0, parting_z + top_h - 1.8])
            cylinder(d = screen_view_dia, h = 2.0);

        // B. 屏幕玻璃内沉槽 (压紧玻璃边缘，让 AMOLED 与上框平齐)
        translate([0, 0, parting_z - 0.01])
            cylinder(d = cavity_dia, h = glass_pocket_h + 0.1);

        // C. 12 点钟外置天线仓顶部容纳槽 (保留 1.2mm 极薄高透 RF 顶盖)
        translate([0, ant_center_y, parting_z - 0.01])
            rounded_box_center(ant_cav_w, ant_cav_l, top_h - 1.2, r = 1.5);

        // D. 9 点钟方向 Type-C 接口上半部避空 (West)
        translate([-case_dia / 2 - 4.0, -typec_w / 2, parting_z - 0.1])
            cube([10.0, typec_w, 2.5]);

        // E. 3 点钟方向 (East) 一体化大镂空窗的上半部避空
        // 让手指能从上侧更舒适地伸入按压开关
        translate([case_dia / 2 - 6.0, -18.0, parting_z - 0.01])
            cube([10.0, 36.0, 1.8]);

        // F. 10 点钟与 8 点钟 ES7210 双麦克风拾音孔 (MIC1 & MIC2, flanking Type-C)
        rotate([0, 0, 148])
            translate([case_dia / 2 - 4.0, 0, parting_z + 1.2])
                rotate([0, 90, 0]) cylinder(d = 1.5, h = 10);
        rotate([0, 0, 212])
            translate([case_dia / 2 - 4.0, 0, parting_z + 1.2])
                rotate([0, 90, 0]) cylinder(d = 1.5, h = 10);
    }
}
    }
}

// 辅助模块：卡扣
module snap_tab() {
    cube([0.85, 5.4, 2.2]);
    translate([0.85, 0, 0.4])
        rotate([0, -90, 0])
            linear_extrude(height = 0.85)
                polygon(points = [[0, 0], [1.2, 0], [0.6, 0.45]]);
}

// 辅助模块：圆角方体
module rounded_box_center(w, l, h, r = 1.5) {
    translate([-w/2 + r, -l/2 + r, 0])
        minkowski() {
            cube([w - 2*r, l - 2*r, h / 2]);
            cylinder(r = r, h = h / 2);
        }
}

// 默认显示装配态
color("DimGray", 0.95) bottom_shell();
translate([0, 0, 0]) color("WhiteSmoke", 0.95) top_shell();
