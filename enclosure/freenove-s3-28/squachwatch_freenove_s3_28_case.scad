// SquachWatch case for the Freenove ESP32-S3 Display 2.8" (FNK0104A/B)
//
// Two printed parts, press-fit, no screws:
//   back  - tub; printed pins on its four posts drop into the board's 3.2 mm
//           holes. Prints floor-down
//   lid   - front frame; crush ribs inside its skirt grip the tub's top step,
//           and four bosses press the board's ends down onto the posts
//
// RESET and BOOT sit either side of the USB-C, facing the back of the board,
// ~10 mm above the floor. Each gets a paperclip hole in the floor with a guide
// tube that steers the clip onto the switch.
//
// Numbers are from the board's outline drawing (LCDWIKI ES3C28P spec, fig 5.1,
// in Freenove's repo under Datasheet/ES3C28P_ES2N28P_Specification_V1.0.pdf).
//
// Coordinates: looking at the screen, USB-C end on the left, the edge with the
// SPEAKER / I2C / IO connectors at the bottom. Origin = PCB bottom-left corner.
//
// The default part, "print", lays the tub and the lid side by side as two
// separate bodies, ready to slice. Or render one part:
//   openscad -D 'part="back"' -o squachwatch_freenove_s3_28_back.stl squachwatch_freenove_s3_28_case.scad

part = "print"; // [print, back, bezel, assembly]

/* [Measure these on your board] */
// Floor top -> PCB back. Speaker (10) + tallest part on the PCB back (4.7)
back_clear = 15;
// RESET/BOOT switch height off the PCB back, actuator included
btn_h = 2.0;

/* [Fit] */
fit = 0.3;          // side gap (the glass is as wide as the PCB)
end_fit = 0.6;      // end gap (USB-C pokes out ~0.3)
bezel_fit = 0.15;   // gap between lid skirt and tub step
rib_interf = 0.2;   // crush-rib bite into the tub step (raise if loose)
pin_d = 3.0;        // locating pins into the 3.2 mm holes
pin_h = 1.3;
wall = 2.2;
floor_t = 2.0;

/* [Bezel] */
bezel_t = 1.2;
skirt_h = 3;
skirt_t = 1.0;
window_margin = 0.25; // beyond the visible window, each side

/* [Openings] */
io_ports = true;    // I2C, IO and UART connectors
sd_slot = true;
sd_slot_w = 19;     // TF slot opening width (the card is 11)
mic_hole = true;
speaker = true;     // pocket + grille

/* [Lettering] */
logo = "SQUACHWATCH";   // engraved on the back; "" for none
logo_size = 4.6;
logo_len = 36;          // engraving length
logo_center = [61, 25]; // beside the speaker grille, over the battery space

/* [Speaker] */
spk_l = 39;   // measured: 1 mm over the listed 38 for a snug fit
spk_w = 27;
spk_d = 10;
spk_across = true;       // long side across the case, leaving more battery room
spk_center = [23, 25];   // clear of the RESET/BOOT guide tubes and the posts
// speaker footprint along the case's x (length) and y (width)
spk_x = spk_across ? spk_w : spk_l;
spk_y = spk_across ? spk_l : spk_w;

/* [Hidden] */
$fn = 48;
eps = 0.01;

// ---- board (outline drawing) ----
pcb_l = 86;  pcb_w = 50;  pcb_t = 1.6;  pcb_r = 3.5;
hole_d = 3.2;
holes = [[4, 4], [82, 4], [4, 46], [82, 46]];      // 78 x 42
// glass stack above the PCB: glue 0.5 + LCD 2.3 + glue 0.5 + touch 1.0
stack = 4.3;
glass_x0 = 8.58;  glass_l = 69.2;                   // 8.22 from the right end
va_x0 = pcb_l - 10.96 - 58.05;  va_l = 58.05;       // visible window
va_y0 = 3.2;  va_w = 43.6;

// edge features (from the drawing's back view, mirrored top-to-bottom)
usb_y = 25;
btns = [[3.26, 13.44], [3.26, 36.57]];             // RESET, BOOT
i2c_x = 49.9;   io_x = 67.75;                        // bottom edge (y = 0)
uart_x = 35.0;  sd_x = 50.97;                        // top edge (y = 50)
mic = [82, 10];

// ---- derived ----
cav_x0 = -end_fit;  cav_y0 = -fit;
cav_l = pcb_l + 2 * end_fit;  cav_w = pcb_w + 2 * fit;
cav_y1 = cav_y0 + cav_w;
out_x0 = cav_x0 - wall;  out_y0 = cav_y0 - wall;
out_l = cav_l + 2 * wall; out_w = cav_w + 2 * wall;
out_y1 = out_y0 + out_w;

z_pcb_bot = floor_t + back_clear;
z_pcb_top = z_pcb_bot + pcb_t;
z_glass = z_pcb_top + stack;
tub_h = z_glass;
step = skirt_t + bezel_fit;

z_usb = z_pcb_bot - 1.6;
z_io = z_pcb_bot - 1.6;
z_sd = z_pcb_bot - 1.0;

// paperclip guide: hole through the floor, tube up to just under the switch
clip_d = 1.6;                         // a standard paperclip is ~1 mm
tube_d = 4.2;
tube_top = z_pcb_bot - btn_h - 0.6;
funnel_d = 4.0;                       // countersink on the outside

echo(str("Case: ", out_l, " x ", out_w, " x ", z_glass + bezel_t, " mm"));

module rr2d(w, h, r) offset(r = r) offset(delta = -r) square([w, h]);
module outer2d(inset = 0)
    translate([out_x0 + inset, out_y0 + inset])
        rr2d(out_l - 2 * inset, out_w - 2 * inset, max(0.5, pcb_r + fit + wall - inset));
module cav2d() translate([cav_x0, cav_y0]) rr2d(cav_l, cav_w, pcb_r + fit);
module win2d(grow = 0) {
    m = window_margin + grow;
    translate([va_x0 - m, va_y0 - m]) square([va_l + 2 * m, va_w + 2 * m]);
}
module rbox(w, h, r) translate([-w / 2, -h / 2]) rr2d(w, h, min(r, h / 2 - 0.01));
// port through a wall: along x (left/right walls) or y (front/back walls)
module port_x(cy, cz, w, h, x0, x1, r = 1)
    translate([x0, cy, cz]) rotate([0, 90, 0]) rotate([0, 0, 90]) linear_extrude(x1 - x0) rbox(w, h, r);
module port_y(cx, cz, w, h, y0, y1, r = 1)
    translate([cx, y1, cz]) rotate([90, 0, 0]) linear_extrude(y1 - y0) rbox(w, h, r);

// ---------------- back ----------------
module back() {
    difference() {
        union() {
            difference() {
                union() {
                    linear_extrude(tub_h - skirt_h) outer2d();
                    linear_extrude(tub_h) outer2d(step);
                }
                translate([0, 0, floor_t]) linear_extrude(tub_h) cav2d();
            }
            for (h = holes) translate([h[0], h[1], floor_t - eps]) {
                cylinder(d = 6.5, h = z_pcb_bot - floor_t + eps);
                // pin into the board's hole, chamfered tip
                translate([0, 0, z_pcb_bot - floor_t]) {
                    cylinder(d = pin_d, h = pin_h - 0.4);
                    translate([0, 0, pin_h - 0.4]) cylinder(d1 = pin_d, d2 = pin_d - 0.6, h = 0.4);
                }
            }
            for (b = btns) translate([b[0], b[1], floor_t - eps])
                cylinder(d = tube_d, h = tube_top - floor_t + eps);
            if (speaker) speaker_frame();
        }

        for (b = btns) translate([b[0], b[1], 0]) {
            translate([0, 0, -1]) cylinder(d = clip_d, h = tube_top + 2, $fn = 20);
            translate([0, 0, -eps]) cylinder(d1 = funnel_d, d2 = clip_d, h = (funnel_d - clip_d) / 2);
        }
        // labels next to the paperclip holes
        for (i = [0, 1]) translate([btns[i][0] + 6.5, btns[i][1], -eps])
            linear_extrude(0.5) mirror([1, 0]) rotate(90)
                text(["RST", "BOOT"][i], size = 2.6, halign = "center", valign = "center",
                     font = "Liberation Sans:style=Bold");

        // USB-C on the left end, with a shallow recess for the overmold
        port_x(usb_y, z_usb, 9.6, 3.8, out_x0 - 1, 0, 1.6);
        port_x(usb_y, z_usb - 0.2, 12.8, 6.8, out_x0 - 1, out_x0 + 1.2, 2.5);

        if (io_ports) {
            for (x = [i2c_x, io_x]) port_y(x, z_io, 8.0, 4.5, out_y0 - 1, cav_y0 + 1);
            port_y(uart_x, z_io, 8.0, 4.5, cav_y1 - 1, out_y1 + 1);
        }

        if (sd_slot) {
            // 19 mm, not 13: on the first print the card sat 2-3 mm off the
            // slot's centre (2026-10-17), and the drawing's position is a
            // scaled reading. 3 mm of slack each side, scoop to match.
            translate([sd_x - sd_slot_w / 2, cav_y1 - 1, z_sd - 1.5]) cube([sd_slot_w, wall + 2, 3]);
            translate([sd_x, out_y1 + 3.4, z_sd]) scale([2.4, 1, 0.8]) sphere(r = 4.6);
        }

        // name on the back, reading right way round with the case face-down
        if (logo != "")
            translate([logo_center[0], logo_center[1], -eps]) linear_extrude(0.6) mirror([1, 0])
                resize([logo_len, 0], auto = true)
                    text(logo, size = logo_size, halign = "center", valign = "center",
                         font = "Liberation Sans:style=Bold");

        if (speaker) grille();
    }
}

module speaker_frame() {
    c = 0.4; r = 1.2; h = 2.5;
    translate([spk_center[0], spk_center[1], floor_t - eps])
        linear_extrude(h) difference() {
            offset(r = r) square([spk_x + 2 * c, spk_y + 2 * c], center = true);
            square([spk_x + 2 * c, spk_y + 2 * c], center = true);
            square([6, spk_y + 10], center = true);
            square([spk_x + 10, 6], center = true);
        }
}

module grille() {
    p = 2.6; d = 1.6;
    gx = spk_x - 8; gy = spk_y - 7;
    for (i = [0 : floor(gx / p)], j = [0 : floor(gy / (p * 0.866))]) {
        x = -gx / 2 + i * p + (j % 2) * p / 2;
        y = -gy / 2 + j * p * 0.866;
        if (abs(x) <= gx / 2)
            translate([spk_center[0] + x, spk_center[1] + y, -1]) cylinder(d = d, h = floor_t + 2, $fn = 16);
    }
}

// ---------------- bezel ----------------
module bezel() {
    difference() {
        union() {
            translate([0, 0, z_glass]) linear_extrude(bezel_t) outer2d();
            translate([0, 0, z_glass - skirt_h]) linear_extrude(skirt_h + eps)
                difference() { outer2d(); outer2d(skirt_t); }
            // bosses clamp the PCB ends, outside the glass
            for (h = holes) translate([h[0], h[1], z_pcb_top + 0.1])
                cylinder(d = 5.6, h = z_glass - z_pcb_top);
            crush_ribs();
        }
        // pry notch on the plain right end, for getting the lid off
        translate([out_x0 + out_l - skirt_t - 1, out_y0 + out_w / 2 - 5, z_glass - skirt_h - 1])
            cube([skirt_t + 2, 10, 2.2]);
        hull() {
            translate([0, 0, z_glass - eps]) linear_extrude(eps) win2d(0);
            translate([0, 0, z_glass + bezel_t]) linear_extrude(eps * 2) win2d(bezel_t * 0.8);
        }
        if (mic_hole) translate([mic[0], mic[1], z_glass - 1]) cylinder(d = 1.6, h = bezel_t + 2);
    }
}

// vertical ribs inside the lid's skirt; they bite into the tub's step
module crush_ribs() {
    rh = bezel_fit + rib_interf;
    z0 = z_glass - skirt_h + 0.8;            // lead-in below the ribs
    ix0 = out_x0 + skirt_t;  ix1 = out_x0 + out_l - skirt_t;
    iy0 = out_y0 + skirt_t;  iy1 = out_y1 - skirt_t;
    for (f = [0.25, 0.5, 0.75]) {            // long sides
        x = ix0 + (ix1 - ix0) * f;
        translate([x - 0.5, iy0 - eps, z0]) cube([1, rh + eps, z_glass - z0 + eps]);
        translate([x - 0.5, iy1 - rh, z0]) cube([1, rh + eps, z_glass - z0 + eps]);
    }
    for (f = [1 / 3, 2 / 3]) {               // short ends
        y = iy0 + (iy1 - iy0) * f;
        translate([ix0 - eps, y - 0.5, z0]) cube([rh + eps, 1, z_glass - z0 + eps]);
        translate([ix1 - rh, y - 0.5, z0]) cube([rh + eps, 1, z_glass - z0 + eps]);
    }
}

// ---------------- mock board for the assembly view ----------------
module board() {
    color("#1a1a1a") translate([0, 0, z_pcb_bot]) linear_extrude(pcb_t) difference() {
        rr2d(pcb_l, pcb_w, pcb_r);
        for (h = holes) translate(h) circle(d = hole_d);
    }
    color("#222") translate([glass_x0, 0, z_pcb_top]) cube([glass_l, pcb_w, stack]);
    color("#3a6ea5") translate([0, 0, z_glass]) linear_extrude(0.05) win2d(-window_margin);
    color("silver") port_x(usb_y, z_usb, 8.9, 3.2, -0.3, 7);
    if (speaker) color("#666") translate([spk_center[0] - spk_x / 2, spk_center[1] - spk_y / 2, floor_t])
        cube([spk_x, spk_y, spk_d]);
}

module bezel_print() translate([0, 0, z_glass + bezel_t]) rotate([180, 0, 0]) bezel();

if (part == "back") back();
else if (part == "bezel") bezel_print();
else if (part == "print") {        // tub + lid as two separate pieces
    back();
    translate([0, 2 * out_y1 + 8, 0]) bezel_print();
}
else {
    color("#e8e2d0") back();
    board();
    color("#c9b98f") translate([0, 0, 10]) bezel();
}
