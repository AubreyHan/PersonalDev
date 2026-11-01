// ==============================================================================
// ESP32-S3-Touch-AMOLED-1.75-G (GNSS/GPS 版) MagSafe 磁吸轻薄卡扣外壳
// 设计标准：一体化天线外置隔离仓 + 底部 MagSafe 平整磁吸圈 + 免螺丝 Snap-fit 卡扣
// ==============================================================================

$fn = 120; // 打印高平滑度圆弧切片

// ----------------- 核心参数配置 (单位: mm) -----------------
// PCB 与屏幕尺寸
pcb_dia          = 49.0;    // PCB 外径
pcb_tol          = 0.6;     // 直径装配间隙公差 (单边 0.3mm)
cavity_dia       = pcb_dia + pcb_tol; // 内部圆台内径 49.6mm
screen_view_dia  = 45.4;    // 屏幕正面视窗有效开孔直径 (露出45mm显示区，压住玻璃黑边)
pcb_thickness    = 1.6;     // PCB 板厚
comp_bottom_h    = 2.2;     // PCB 背面元器件安全避空高度
screen_glass_h   = 2.4;     // 正面屏幕与触控玻璃厚度

// 外壳轮廓与壁厚
wall_t           = 2.6;     // 主圆盘外壁厚度
case_dia         = cavity_dia + wall_t * 2; // 主壳外径 ≈ 54.8 ~ 55.0mm (与MagSafe外径54mm呼应)
bottom_floor_t   = 2.0;     // 底部底板基准厚度
top_bezel_t      = 1.8;     // 顶部屏幕压框厚度
total_body_h     = bottom_floor_t + comp_bottom_h + pcb_thickness + screen_glass_h + top_bezel_t; // ≈ 10.0mm 超薄设计

// MagSafe 磁吸槽参数 (适配市售标准 MagSafe 引磁片 / 磁吸铁圈)
magsafe_od       = 54.2;    // 磁吸环槽外径 (标准环54.0mm + 0.2mm公差)
magsafe_id       = 45.8;    // 磁吸环槽内径 (标准环46.0mm - 0.2mm公差)
magsafe_depth    = 1.2;     // 磁吸槽深度 (磁吸片贴合后与底壳绝对平齐)

// 顶部外置 GNSS/GPS 陶瓷天线仓参数 (标配 25x25mm 大尺寸高增益陶瓷天线)
ant_w            = 25.6;    // 天线仓内净宽 (容纳标准 25x25mm 陶瓷天线 + 0.6mm 装配公差)
ant_l            = 25.6;    // 天线仓内净长
ant_h            = 6.5;     // 天线仓内深度 (兼容 4.0mm 被动陶瓷片及 6.5mm 带 LNA 放大器主动天线)
ant_wall_t       = 1.8;     // 天线仓壁厚
ant_pod_w        = ant_w + ant_wall_t * 2; // 外宽 ≈ 29.2mm
ant_pod_l        = ant_l + ant_wall_t * 2; // 外长 ≈ 29.2mm
ant_center_y     = case_dia / 2 + ant_pod_l / 2 - 5.5; // 嵌入主圆盘交融过渡深度
wire_tunnel_w    = 3.6;     // IPEX 射频微同轴线走线槽宽
wire_tunnel_h    = 2.4;     // 走线槽高

// 接口开孔
typec_w          = 11.2;    // Type-C 插口避空宽度 (兼容粗胶壳线头)
typec_h          = 6.0;     // Type-C 插口避空高度
tf_slot_w        = 12.0;    // TF 卡槽宽度
tf_slot_h        = 2.0;     // TF 卡槽厚度

// 卡扣 (Snap-fit) 分型面与参数
parting_z        = bottom_floor_t + comp_bottom_h + pcb_thickness; // 分型面设在 PCB 上表面
snap_depth       = 0.45;    // 卡扣倒勾咬合深度
snap_w           = 6.0;     // 单个卡扣宽度
snap_h           = 2.2;     // 卡扣高度

// ----------------- 模块定义 -----------------

// 外壳整体外部轮廓 2D 截面 (含主圆盘及 12 点钟外置天线过渡造型)
module case_outer_profile_2d() {
    hull() {
        circle(d = case_dia);
        translate([0, ant_center_y, 0])
            offset(r = 3.0) square([ant_pod_w - 6.0, ant_pod_l - 6.0], center = true);
    }
}

// 1. 底壳模型 (Bottom Base Shell)
module bottom_shell() {
    difference() {
        // 实体外轮廓拉伸
        linear_extrude(height = parting_z, convexity = 10)
            case_outer_profile_2d();

        // 底部 MagSafe 凹槽 (完全平整底面上的环形凹槽)
        translate([0, 0, -0.01])
            difference() {
                cylinder(d = magsafe_od, h = magsafe_depth + 0.01);
                cylinder(d = magsafe_id, h = magsafe_depth + 0.02);
            }

        // PCB 主仓空腔 (避空背面芯片、LDO、引脚与焊接件)
        translate([0, 0, bottom_floor_t])
            cylinder(d = cavity_dia, h = parting_z);

        // 天线仓底座过线孔及下沉槽
        translate([0, ant_center_y, bottom_floor_t])
            rounded_box_center(ant_w, ant_l, parting_z + 1, r = 1.5);

        // IPEX 微轴同轴走线隧道 (连接主仓与天线仓)
        translate([-wire_tunnel_w / 2, cavity_dia / 2 - 4.0, bottom_floor_t])
            cube([wire_tunnel_w, ant_center_y - cavity_dia / 2 + 6.0, wire_tunnel_h]);

        // 6点钟方向 USB Type-C 接口下半部开孔
        translate([-typec_w / 2, -case_dia / 2 - 2, bottom_floor_t + 0.4])
            cube([typec_w, 8.0, typec_h]);

        // 9点钟方向 MicroSD (TF) 卡槽开孔
        translate([-case_dia / 2 - 2, -tf_slot_w / 2, bottom_floor_t + 0.6])
            cube([8.0, tf_slot_w, tf_slot_h]);

        // 免螺丝拆卸撬槽 (Pry Notch: 3点钟与9点钟各设一处隐形微槽)
        for (a = [0, 180]) {
            rotate([0, 0, a])
                translate([case_dia / 2 - 1.2, -2.0, parting_z - 1.2])
                    cube([3.0, 4.0, 1.3]);
        }

        // 底壳内壁的 4 处卡扣锁止母槽 (Snap-fit Recesses)
        for (ang = [35, 145, 215, 325]) {
            rotate([0, 0, ang])
                translate([cavity_dia / 2 - 0.2, -snap_w / 2, parting_z - 1.8])
                    cube([snap_depth + 0.8, snap_w, 1.6]);
        }
    }
}

// 2. 上壳模型 (Top Bezel Shell & Integrated Antenna Hood)
module top_shell() {
    top_h = total_body_h - parting_z;

    difference() {
        union() {
            // 上壳主体轮廓拉伸
            translate([0, 0, parting_z])
                linear_extrude(height = top_h, convexity = 10)
                    case_outer_profile_2d();

            // 下凸的 4 组免螺丝卡扣弹性公扣 (Snap-fit male tabs)
            for (ang = [35, 145, 215, 325]) {
                rotate([0, 0, ang])
                    translate([cavity_dia / 2 - 0.7, -snap_w / 2 + 0.2, parting_z - 2.2])
                        snap_tab();
            }
        }

        // 正面圆形屏幕透光/触控视窗开孔
        translate([0, 0, parting_z + top_h - top_bezel_t - 0.01])
            cylinder(d = screen_view_dia, h = top_bezel_t + 0.1);

        // 屏幕玻璃内沉槽 (压紧玻璃边缘，让 AMOLED 与上框完全平齐)
        translate([0, 0, parting_z])
            cylinder(d = cavity_dia, h = screen_glass_h + 0.2);

        // 12 点钟外置天线仓顶部容纳槽 (保留 1.2mm 极薄高透射 RF 塑料顶盖)
        translate([0, ant_center_y, parting_z])
            rounded_box_center(ant_w, ant_l, top_h - 1.2, r = 1.5);

        // 6点钟方向 USB Type-C 接口上半部避空
        translate([-typec_w / 2, -case_dia / 2 - 2, parting_z - 0.1])
            cube([typec_w, 8.0, 2.5]);

        // 侧边双硅麦 (ES7210) 传音小孔
        translate([cavity_dia / 2 - 2, 8.0, parting_z + 1.2])
            rotate([0, 90, 0]) cylinder(d = 1.4, h = 10);
        translate([cavity_dia / 2 - 2, -8.0, parting_z + 1.2])
            rotate([0, 90, 0]) cylinder(d = 1.4, h = 10);
    }
}

// 辅助模块：卡扣卡齿
module snap_tab() {
    difference() {
        union() {
            // 弹性臂
            cube([0.9, snap_w - 0.4, 2.3]);
            // 锁止外凸齿 (45度导向面 + 75度锁止面)
            translate([0.9, 0, 0.4])
                rotate([0, -90, 0])
                    linear_extrude(height = 0.9)
                        polygon(points = [[0, 0], [1.2, 0], [0.6, snap_depth]]);
        }
    }
}

// 辅助模块：居中圆角方体
module rounded_box_center(w, l, h, r = 1.5) {
    translate([-w/2 + r, -l/2 + r, 0])
        minkowski() {
            cube([w - 2*r, l - 2*r, h / 2]);
            cylinder(r = r, h = h / 2);
        }
}

// ==============================================================================
// 视图控制 (默认同时显示底壳与上壳扣合装配态)
// 可单选: bottom_shell(); 或 top_shell();
// ==============================================================================
color("LightSteelBlue", 0.95) bottom_shell();
translate([0, 0, 0]) color("WhiteSmoke", 0.95) top_shell();
