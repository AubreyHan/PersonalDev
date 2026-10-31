// ==============================================================================
// Columbus P-1 Mark II (P-1 M2) GNSS Data Logger - Mounting Base (Open Front Box)
// Model: Straight Vertical Walls (Option B: Flat interior for friction / foam fit)
// Mounting: Completely Flat Bottom Exterior for Double-Sided Tape (3M VHB)
// Author: Aubrey Han & Antigravity
// ==============================================================================

$fn = 64; // High resolution curve rendering

// ------------------------------------------------------------------------------
// 1. Device Dimensions & Clearances
// ------------------------------------------------------------------------------
dev_w = 55.0;            // Device width (mm)
dev_h = 85.0;            // Device height (mm)
dev_d = 17.7;            // Device thickness (mm)

fit_tol_xy = 0.30;       // Single side clearance for smooth drop-in
fit_tol_z  = 0.30;       // Depth clearance so front is flush or slightly recessed

cav_w = dev_w + 2 * fit_tol_xy;  // 55.6 mm
cav_h = dev_h + 2 * fit_tol_xy;  // 85.6 mm
cav_d = dev_d + fit_tol_z;        // 18.0 mm
r_inner = 4.5;                   // Inner corner radius (matching device corners)

// ------------------------------------------------------------------------------
// 2. Base & Wall Dimensions
// ------------------------------------------------------------------------------
wall_thick = 2.4;        // Wall thickness (6 perimeters with 0.4mm nozzle)
floor_thick = 2.4;       // Base plate thickness (solid and flat for tape)

out_w = cav_w + 2 * wall_thick;  // 60.4 mm
out_h = cav_h + 2 * wall_thick;  // 90.4 mm
out_d = floor_thick + cav_d;     // 20.4 mm
r_outer = r_inner + wall_thick;  // 6.9 mm

// ------------------------------------------------------------------------------
// 3. Cutout Specifications
// ------------------------------------------------------------------------------
// Top Power Button Cutout
top_cut_w = 22.0;

// Right Function Button (FN) Cutout (Measured from top inner edge)
fn_from_top_start = 20.0;
fn_from_top_end   = 37.0;
fn_cut_len = fn_from_top_end - fn_from_top_start; // 17.0 mm

// Bottom Port Cutout (MicroUSB & Rubber Cap)
bot_cut_w = 38.0;

// Left Finger Grip Notch (For easy device removal)
finger_notch_w = 20.0;
finger_notch_depth = 11.4; // Cuts from top rim (Z=20.4) down to Z=9.0

// ------------------------------------------------------------------------------
// Modules
// ------------------------------------------------------------------------------
module rounded_rect(w, h, r, z_height) {
    x_half = w / 2 - r;
    y_half = h / 2 - r;
    linear_extrude(height = z_height) {
        hull() {
            translate([ x_half,  y_half, 0]) circle(r = r);
            translate([-x_half,  y_half, 0]) circle(r = r);
            translate([-x_half, -y_half, 0]) circle(r = r);
            translate([ x_half, -y_half, 0]) circle(r = r);
        }
    }
}

module p1m2_mounting_dock() {
    difference() {
        // Outer Solid Box with flat bottom at Z=0
        rounded_rect(out_w, out_h, r_outer, out_d);

        // 1. Inner Cavity (straight vertical walls)
        translate([0, 0, floor_thick])
            rounded_rect(cav_w, cav_h, r_inner, cav_d + 1.0);

        // 2. Top Cutout (Power Button)
        translate([-top_cut_w / 2, cav_h / 2 - 1.0, floor_thick])
            cube([top_cut_w, wall_thick + 2.0, cav_d + 1.0]);

        // 3. Right Cutout (Function Button FN)
        // Cavity top is at Y = cav_h / 2
        // Cutout span: Y = cav_h/2 - fn_from_top_end to cav_h/2 - fn_from_top_start
        translate([cav_w / 2 - 1.0, (cav_h / 2) - fn_from_top_end, floor_thick])
            cube([wall_thick + 2.0, fn_cut_len, cav_d + 1.0]);

        // 4. Bottom Cutout (MicroUSB / Cover)
        translate([-bot_cut_w / 2, -cav_h / 2 - wall_thick - 1.0, floor_thick])
            cube([bot_cut_w, wall_thick + 2.0, cav_d + 1.0]);

        // 5. Left Finger Grip Notch (Rounded dip on left wall)
        translate([-cav_w / 2 - wall_thick - 1.0, -finger_notch_w / 2, out_d - finger_notch_depth])
            cube([wall_thick + 2.0, finger_notch_w, finger_notch_depth + 1.0]);
    }
}

// Render the model
p1m2_mounting_dock();
