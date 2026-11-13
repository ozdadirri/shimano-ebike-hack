// etube_case.scad: parametric 3D-printed head unit for Borghini's dashboard.
//
// VARIANT = "en100" (default): LCDwiki ES3C28P 2.8" ESP32-S3 board (portrait), a
//   1S LiPo, the board's speaker and a power switch. The board reads the bike over
//   Bluetooth from the EW-EN100, so no cable goes to the bike.
//   Board numbers come from LCDwiki's outline drawing (ES3C28P_Size.pdf, V1.0).
// VARIANT = "tap": the older wired-tap layout (generic screen + 60 x 40 mm tap board).
//
// Ready-made STLs for the en100 variant are in stl/ (see stl/README.md).
// 1. Check [Touchscreen module] against your board, and [Battery / speaker / switch].
// 2. Set PART, press F6 (Render), then File > Export > Export as STL.
//    PART = "bezel" | "shell" | "mount" | "garmin" | "garmin_test" | "gasket" | "assembly"
// Print bezel, shell, mount and garmin in PETG or ASA (not PLA: it softens in summer sun).
// Print the gasket in TPU 95A.
// "mount" straps to the bar; "garmin" replaces it with a Garmin quarter-turn tab.
// Print "garmin_test" first and twist it into your Garmin mount to check the fit.

VARIANT = "en100";  // "en100" | "tap"
PART = "assembly";
MOUNT = "bar";      // which mount the assembly view shows: "bar" | "garmin"

/* [Touchscreen module: ES3C28P defaults, from the LCDwiki drawing] */
scr_pcb     = VARIANT == "en100" ? [50.0, 86.0] : [86.0, 50.0];  // PCB outline, width x height (ES3C28P is portrait)
scr_pcb_t   = 1.6;           // PCB thickness
scr_glass_t = VARIANT == "en100" ? 4.3 : 4.0;   // touch glass + glue + LCD + glue on the front of the PCB (1.0+0.5+2.3+0.5)
view        = VARIANT == "en100" ? [43.6, 58.05] : [58.0, 44.0];  // window = the LCD's visible area
view_off    = VARIANT == "en100" ? [0.0, 3.0] : [0.0, 0.0];      // window centre minus PCB centre, seen from the front (+y = up)
scr_rear    = VARIANT == "en100" ? 4.7 : 11.0;  // tallest part on the back of the screen PCB
scr_holes   = [for (sx = [-1, 1], sy = [-1, 1]) [sx*21, sy*39]];  // ES3C28P: 4 x 3.2 mm holes on a 42 x 78 grid
press_pts   = [[-39.5, -21.5], [39.5, -21.5], [-39.5, 21.5], [39.5, 21.5]];  // tap variant only: posts press the screen PCB

/* [USB-C on the ES3C28P (bottom edge, back side)] */
usb_cut   = [17.0, 8.0];  // opening width x height: wide enough for either mirror reading of the drawing (+-1.9 mm) and a cable plug
usb_x_off = 0.0;          // opening centre from the PCB centre (the drawing puts the port about 1.9 mm off centre)
usb_dz    = 1.6;          // port centre below the back face of the PCB (a 3.2 mm tall receptacle)

/* [Battery / speaker / switch (en100 variant)] */
batt      = [34.0, 50.0, 10.0];  // 1S LiPo, e.g. 103450 (~2000 mAh): width x height x thickness
batt_pos  = [0.0, -21.0];        // battery centre on the floor (+y = top of the screen)
spk       = [20.0, 30.0];        // speaker footprint (a 2030 box speaker); set [0, 0] for none
spk_pos   = [16.0, 27.0];        // speaker centre: clear of the corner boss and the mount bosses; grille clear of the Garmin plate
sw_slot   = [7.0, 3.6];          // mini slide switch (e.g. SS12D00): actuator slot, length x height
sw_y      = -10.0;               // switch position along the left wall
rear_clr  = 1.5;                 // air gap between the battery (with foam) and the parts on the PCB back

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
screw_len = VARIANT == "en100" ? 20 : 30;  // length of the four M3 case screws (en100: M3 x 20)
insert_d  = 4.0;    // hole for M3 heat-set inserts (check your inserts' datasheet)
insert_l  = 5.5;
foam      = 1.5;    // foam pad between the press posts and the screen PCB
gasket_t  = 1.0;    // TPU gasket thickness
gland_d   = 12.6;   // PG7 cable gland thread
cover_t   = 1.0;    // clear polycarbonate window, set 0 for none
cover_pad = VARIANT == "en100" ? 4.0 : 5.0;  // window sheet extends this far past the opening (4 keeps it clear of the PCB pillars)

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
bezel_h = front + scr_glass_t + scr_pcb_t + 1.0;           // the PCB back sits 1.0 mm inside the bezel
// inside the shell: tap board stack, or battery (+ foam) + air + parts on the PCB back
depth   = VARIANT == "tap" ? tap_stand + tap_t + tap_parts + 2.0 + scr_rear
                           : batt.z + foam + rear_clr + scr_rear - gasket_t - 1.0;
shell_h = floor_t + depth;
boss_c  = wall + boss_d/2 - 1;                              // boss centre from the outer edge
corners = [for (sx = [-1, 1], sy = [-1, 1])
           [sx*(outer.x/2 - boss_c), sy*(outer.y/2 - boss_c)]];
tap_org = [-tap_pcb.x/2, -inner.y/2 + 1.0];                // tap PCB bottom-left corner
mount_bosses = [[0, mount_y - 6], [0, mount_y + 6]];
usb_zc  = shell_h + gasket_t + 1.0 - usb_dz;                // USB-C centre height in assembly coordinates
cb_depth = max(2, shell_h + gasket_t + insert_l - 1.0 - screw_len);

echo(str("Outer size ", outer.x, " x ", outer.y, " mm, total thickness ",
         shell_h + gasket_t + bezel_h, " mm, counterbore depth ", cb_depth, " mm"));

// USB-C opening through the bottom wall, in assembly coordinates (shell frame)
module usb_notch() {
    translate([usb_x_off, -outer.y/2 - 0.01, usb_zc])
        rotate([-90, 0, 0]) linear_extrude(wall + clr + margin + 0.02) rrect(usb_cut, 2.5);
}

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
            // en100: pillars the PCB is screwed to (M2.5 self-tapping, through its 4 holes)
            if (VARIANT == "en100")
                for (h = scr_holes) translate(h) cylinder(d = 5.4, h = front + scr_glass_t);
        }
        if (VARIANT == "en100") {
            for (h = scr_holes) translate([h.x, h.y, front]) cylinder(d = 2.2, h = scr_glass_t + 0.01);
            // the USB-C port straddles the bezel/shell joint: notch the bezel's back edge too
            translate([0, 0, bezel_h]) mirror([0, 0, 1]) translate([0, 0, -(shell_h + gasket_t)]) usb_notch();
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
            if (VARIANT == "tap") {
                for (h = tap_holes) translate([tap_org.x + h.x, tap_org.y + h.y, 0]) cylinder(d = 5.5, h = tap_z);
                // posts press the screen PCB (through a foam pad) into the bezel
                for (p = press_pts) translate(p) cylinder(d = 6, h = shell_h + gasket_t + 1.0 - foam);
            } else {
                // low ribs that keep the battery and the speaker in place (add foam tape on top)
                translate([batt_pos.x, batt_pos.y, floor_t - 0.01]) cradle([batt.x, batt.y], 3);
                if (spk.x > 0) translate([spk_pos.x, spk_pos.y, floor_t - 0.01]) cradle(spk, 2);
            }
            for (p = mount_bosses) translate(p) cylinder(d = 7, h = floor_t + 6);
        }
        // M3 screws from the back into the bezel's inserts
        for (p = corners) translate([p.x, p.y, -0.01]) {
            cylinder(d = screw_d, h = shell_h + 1);
            cylinder(d = head_d, h = cb_depth);
        }
        // pilot holes for M2.5 self-tapping screws
        if (VARIANT == "tap")
            for (h = tap_holes) translate([tap_org.x + h.x, tap_org.y + h.y, floor_t]) cylinder(d = 2.2, h = tap_z);
        // heat-set inserts for the mount, pressed in from the inside
        for (p = mount_bosses) translate([p.x, p.y, -0.01]) cylinder(d = insert_d, h = floor_t + 6.1);
        if (VARIANT == "tap") {
            // USB-C access in the bottom wall
            translate([tap_org.x + usb_x, -outer.y/2 - 0.01, tap_z + tap_t + usb_z])
                rotate([-90, 0, 0]) linear_extrude(wall + 0.02) rrect([12, 7], 2.5);
            // PG7 cable gland in the left wall (seen from the front)
            translate([-outer.x/2 - 0.01, 0, floor_t + depth/2])
                rotate([0, 90, 0]) cylinder(d = gland_d, h = wall + 0.02);
        } else {
            usb_notch();
            // power switch in the left wall (seen from the front), wired into the battery's + lead
            translate([-outer.x/2 - 0.01, sw_y, floor_t + depth/2])
                rotate([0, 90, 0]) linear_extrude(wall + 0.02) rrect([sw_slot.y, sw_slot.x], 1);
            // speaker grille: 2 mm holes under the speaker
            if (spk.x > 0)
                for (gx = [-spk.x/2 + 3 : 3.5 : spk.x/2 - 3], gy = [-spk.y/2 + 3 : 3.5 : spk.y/2 - 3])
                    translate([spk_pos.x + gx, spk_pos.y + gy, -0.01]) cylinder(d = 2, h = floor_t + 0.02, $fn = 16);
        }
    }
}

// Ribs around a footprint on the floor. 1.2 mm thick, with 0.6 mm play, gaps in the corners for wires.
module cradle(size, h) {
    linear_extrude(h) difference() {
        rrect(size + [2*(0.6 + 1.2), 2*(0.6 + 1.2)], 1.5);
        rrect(size + [1.2, 1.2], 0.8);
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx*size.x/2, sy*size.y/2]) square(6, center = true);
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
        if (VARIANT == "en100")
            translate([usb_x_off, -outer.y/2]) square([usb_cut.x, 2*(wall + clr + margin) + 0.02], center = true);
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
