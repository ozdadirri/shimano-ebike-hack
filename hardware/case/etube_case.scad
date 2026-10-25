// etube_case.scad: parametric 3D-printed head unit for the ESP32-S3 touchscreen
// and the 60 x 40 mm E-TUBE tap board from the build guide.
//
// 1. Measure your touchscreen module with calipers and edit [Touchscreen module].
// 2. Set PART, press F6 (Render), then File > Export > Export as STL.
//    PART = "bezel" | "shell" | "mount" | "garmin" | "garmin_test" | "gasket" | "assembly"
// Print bezel, shell, mount and garmin in PETG or ASA (not PLA: it softens in summer sun).
// Print the gasket in TPU 95A.
// "mount" straps to the bar; "garmin" replaces it with a Garmin quarter-turn tab.
// Print "garmin_test" first and twist it into your Garmin mount to check the fit.

PART = "assembly";
MOUNT = "bar";      // which mount the assembly view shows: "bar" | "garmin"

/* [Touchscreen module: measure yours] */
scr_pcb     = [86.0, 50.0];  // module PCB outline, width x height
scr_pcb_t   = 1.6;           // PCB thickness
scr_glass_t = 4.0;           // glass + backlight stack on the front of the PCB
view        = [58.0, 44.0];  // window opening: about 1 mm inside the active area on each side
view_off    = [0.0, 0.0];    // window centre minus PCB centre, as seen from the front
scr_rear    = 11.0;          // height of headers and parts on the back of the screen PCB
press_pts   = [[-39.5, -21.5], [39.5, -21.5], [-39.5, 21.5], [39.5, 21.5]];  // where posts press the screen PCB (avoid parts)

/* [Tap board (PCB chapter)] */
tap_pcb   = [60.0, 40.0];
tap_t     = 1.6;
tap_holes = [[2, 2], [58, 2], [2, 38], [58, 38]];  // from the PCB's bottom-left corner
tap_stand = 5.0;    // standoff height under the tap board
tap_parts = 13.0;   // tallest part on the tap board (Pololu module on pins)
usb_x     = 50.2;   // USB-C centre from the tap PCB's left edge, on its bottom edge
usb_z     = 2.8;    // USB-C centre above the tap PCB's top surface

/* [Case] */
wall      = 3.0;    // side walls, also the gasket flange
front     = 2.4;    // front plate
floor_t   = 2.4;    // back plate
clr       = 0.4;    // clearance around each PCB
margin    = 6.0;    // side room so the corner bosses clear the screen PCB corners
r_out     = 5.0;    // outer corner radius
boss_d    = 8.0;
screw_d   = 3.4;    // M3 clearance hole
head_d    = 6.4;    // M3 socket-head counterbore
screw_len = 30;     // length of the four M3 case screws
insert_d  = 4.0;    // hole for M3 heat-set inserts (check your inserts' datasheet)
insert_l  = 5.5;
foam      = 1.5;    // foam pad between the press posts and the screen PCB
gasket_t  = 1.0;    // TPU gasket thickness
gland_d   = 12.6;   // PG7 cable gland thread
cover_t   = 1.0;    // clear polycarbonate window, set 0 for none
cover_pad = 5.0;    // window sheet extends this far past the opening on each side

/* [Handlebar mount] */
bar_d   = 31.8;         // handlebar diameter at the clamp spot
shim    = 1.0;          // rubber shim thickness
strap   = [14.0, 3.5];  // strap slot width x thickness
mount_y = 20.0;         // mount centre on the case back (+y = top of the screen)

/* [Garmin quarter-turn adapter: fit-test first] */
// Starting values from community measurements of the Garmin quarter-turn tab,
// not from a Garmin drawing. Print PART = "garmin_test", try it in your mount,
// then adjust and print the full adapter.
g_tab_d    = 27.5;   // tab tip-to-tip diameter
g_tab_w    = 21.5;   // tab width across the flats
g_neck_d   = 20.0;   // round neck between the plate and the tabs
g_neck_h   = 2.6;    // gap the mount's lips slide into: raise if it won't twist in
g_tab_t    = 2.4;    // tab thickness
g_angle    = 0;      // turn the tabs if the unit sits sideways when locked (try 90)
g_printed  = true;   // false = flat 32 mm pad for sticking on a bought Garmin tab adapter

$fn = 64;

// ---------- derived ----------
inner   = [scr_pcb.x + 2*clr + 2*margin, scr_pcb.y + 2*clr + 2*margin];
outer   = inner + [2*wall, 2*wall];
bezel_h = front + scr_glass_t + scr_pcb_t + 1.0;
depth   = tap_stand + tap_t + tap_parts + 2.0 + scr_rear;   // inside the shell
shell_h = floor_t + depth;
boss_c  = wall + boss_d/2 - 1;                              // boss centre from the outer edge
corners = [for (sx = [-1, 1], sy = [-1, 1])
           [sx*(outer.x/2 - boss_c), sy*(outer.y/2 - boss_c)]];
tap_org = [-tap_pcb.x/2, -inner.y/2 + 1.0];                // tap PCB bottom-left corner
mount_bosses = [[0, mount_y - 6], [0, mount_y + 6]];
cb_depth = max(2, shell_h + gasket_t + insert_l - 1.0 - screw_len);

echo(str("Outer size ", outer.x, " x ", outer.y, " mm, total thickness ",
         shell_h + gasket_t + bezel_h, " mm, counterbore depth ", cb_depth, " mm"));

module rrect(size, r) { offset(r) square([size.x - 2*r, size.y - 2*r], center = true); }
module rbox(size, h, r) { linear_extrude(h) rrect(size, r); }

// Printed front face down. Local z = 0 is the face the rider sees.
module bezel() {
    difference() {
        union() {
            difference() {
                rbox(outer, bezel_h, r_out);
                translate([0, 0, front]) rbox(inner, bezel_h, r_out - wall);
            }
            for (p = corners) translate(p) cylinder(d = boss_d, h = bezel_h);
        }
        // window, chamfered 45 degrees towards the rider
        translate([view_off.x, view_off.y, 0])
            hull() {
                translate([0, 0, front - cover_t - 0.01]) linear_extrude(0.02) square(view, center = true);
                translate([0, 0, -0.01]) linear_extrude(0.02)
                    square(view + 2*[front - cover_t, front - cover_t], center = true);
            }
        // pocket for the clear window sheet, open to the inside (prints without supports)
        if (cover_t > 0)
            translate([view_off.x, view_off.y, front - cover_t])
                linear_extrude(cover_t + 0.01) rrect(view + 2*[cover_pad, cover_pad], 2);
        // M3 heat-set inserts, pressed in from the back face
        for (p = corners) translate([p.x, p.y, bezel_h - insert_l]) cylinder(d = insert_d, h = insert_l + 0.01);
    }
}

// Printed floor down. Local z = 0 is the back of the case.
module shell() {
    tap_z = floor_t + tap_stand;   // underside of the tap PCB
    difference() {
        union() {
            difference() {
                rbox(outer, shell_h, r_out);
                translate([0, 0, floor_t]) rbox(inner, shell_h, r_out - wall);
            }
            for (p = corners) translate(p) cylinder(d = boss_d, h = shell_h);
            for (h = tap_holes) translate([tap_org.x + h.x, tap_org.y + h.y, 0]) cylinder(d = 5.5, h = tap_z);
            // posts press the screen PCB (through a foam pad) into the bezel
            for (p = press_pts) translate(p) cylinder(d = 6, h = shell_h + gasket_t + 1.0 - foam);
            for (p = mount_bosses) translate(p) cylinder(d = 7, h = floor_t + 6);
        }
        // M3 screws from the back into the bezel's inserts
        for (p = corners) translate([p.x, p.y, -0.01]) {
            cylinder(d = screw_d, h = shell_h + 1);
            cylinder(d = head_d, h = cb_depth);
        }
        // pilot holes for M2.5 self-tapping screws
        for (h = tap_holes) translate([tap_org.x + h.x, tap_org.y + h.y, floor_t]) cylinder(d = 2.2, h = tap_z);
        // heat-set inserts for the mount, pressed in from the inside
        for (p = mount_bosses) translate([p.x, p.y, -0.01]) cylinder(d = insert_d, h = floor_t + 6.1);
        // USB-C access in the bottom wall
        translate([tap_org.x + usb_x, -outer.y/2 - 0.01, tap_z + tap_t + usb_z])
            rotate([-90, 0, 0]) linear_extrude(wall + 0.02) rrect([12, 7], 2.5);
        // PG7 cable gland in the left wall (seen from the front)
        translate([-outer.x/2 - 0.01, 0, floor_t + depth/2])
            rotate([0, 90, 0]) cylinder(d = gland_d, h = wall + 0.02);
    }
}

// Printed plate down. Local z = 0 is the face against the case back. The bar runs along x.
module mount() {
    mw = 44; ml = 26; mt = 4; blk = 11;
    r  = bar_d/2 + shim;
    difference() {
        union() {
            rbox([mw, ml], mt, 3);
            translate([0, 0, mt - 0.01]) linear_extrude(blk) square([mw - 6, ml], center = true);
        }
        translate([0, 0, mt + blk + r - 4]) rotate([0, 90, 0]) cylinder(r = r, h = mw + 2, center = true);
        for (sx = [-1, 1]) translate([sx*11, 0, mt + 2 + strap.y/2]) cube([strap.x, ml + 2, strap.y], center = true);
        for (sy = [-1, 1]) translate([0, sy*6, -0.01]) {
            cylinder(d = screw_d, h = mt + blk + 1);
            translate([0, 0, mt]) cylinder(d = head_d, h = blk + 1);
        }
    }
}

module gasket() {
    linear_extrude(gasket_t) difference() {
        union() {
            difference() { rrect(outer, r_out); rrect(inner, r_out - wall); }
            for (p = corners) translate(p) circle(d = boss_d);
        }
        for (p = corners) translate(p) circle(d = screw_d);
    }
}

// Garmin quarter-turn tab: round neck, then two tabs that lock under the mount's lips.
module garmin_tab() {
    cylinder(d = g_neck_d, h = g_neck_h + 0.01);
    translate([0, 0, g_neck_h]) linear_extrude(g_tab_t)
        rotate(g_angle) intersection() {
            circle(d = g_tab_d);
            square([g_tab_d + 1, g_tab_w], center = true);
        }
}

// Printed plate down. Screws onto the case back with the bar mount's two M3 inserts.
// Local origin = the bar mount's centre; the tab sits 4 mm below the case centre,
// far enough from the screw heads that they stay reachable at any g_angle.
module garmin() {
    pt = 6;                       // plate thickness: room for recessed M3 heads
    ty = -(mount_y + 4);          // tab centre, relative to the mount screws
    difference() {
        union() {
            linear_extrude(pt) hull() {
                for (sy = [-1, 1]) translate([0, sy*6]) circle(d = 14);
                translate([0, ty]) circle(d = 34);
            }
            translate([0, ty, pt - 0.01])
                if (g_printed) garmin_tab();
                else cylinder(d = 32, h = 1.0);   // flat pad for a bought adapter (VHB)
        }
        for (sy = [-1, 1]) translate([0, sy*6, -0.01]) {
            cylinder(d = screw_d, h = pt + 1);
            translate([0, 0, pt - 3.4]) cylinder(d = head_d, h = 4);
        }
    }
}

// 10-minute fit test: the tab on a small disc, nothing else.
module garmin_test() {
    cylinder(d = 32, h = 2);
    translate([0, 0, 2 - 0.01]) garmin_tab();
}

module assembly(explode = 14) {
    color("#2b3a35") shell();
    color("#d9b45a", 0.7) translate([0, 0, shell_h + explode*0.5]) gasket();
    color("#3a4a44") translate([0, 0, shell_h + gasket_t + explode + bezel_h]) mirror([0, 0, 1]) bezel();
    color("#55625d") translate([0, mount_y, -explode]) mirror([0, 0, 1])
        if (MOUNT == "garmin") garmin(); else mount();
}

if (PART == "bezel")       bezel();
if (PART == "shell")       shell();
if (PART == "mount")       mount();
if (PART == "garmin")      garmin();
if (PART == "garmin_test") garmin_test();
if (PART == "gasket")      gasket();
if (PART == "assembly")    assembly();
