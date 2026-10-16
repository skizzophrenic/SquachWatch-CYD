// SquachWatch case for the Waveshare ESP32-C5-Touch-LCD-2.8
//
// Two printed parts, press-fit, no screws:
//   back  - tub; printed pins on its four posts drop into the board's M2.5
//           holes. Prints floor-down
//   lid   - front frame over the glass border; crush ribs inside its skirt
//           grip the tub's top step and it clamps the board by the glass
//
// Board numbers are from Waveshare's dimension drawing
// (docs.waveshare.com/ESP32-C5-Touch-LCD-2.8). Port positions along the edges
// were scaled off that drawing, so print a test and nudge the *_pos values.
//
// Coordinates: looking at the screen, USB-C edge at the bottom.
// Origin = PCB bottom-left corner, x right, y up, z out of the screen.
//
// The default part, "print", lays the tub and the lid (bezel) side by side as
// two separate bodies, ready to slice. Or render one part:
//   openscad -D 'part="back"' -o squachwatch_waveshare_c5_back.stl squachwatch_waveshare_c5_case.scad

part = "print"; // [print, back, bezel, assembly]

/* [Measure these on your board] */
// PCB front face -> glass front face (display module + glass)
lcd_stack = 4.6;
// Floor top -> PCB back face. Needs room for speaker (7) + parts on the PCB back (~3.5)
back_clear = 11.5;
// The board's own brass posts on its back corners; the case's posts stop this
// far short of the PCB so the brass posts sit on them (from two test prints)
board_post_h = 6.0;

/* [Fit] */
fit = 0.3;          // gap around the glass
bezel_fit = 0.15;   // gap between lid skirt and tub step
rib_interf = 0.2;   // crush-rib bite into the tub step (raise if loose)
pin_d = 2.0;        // locating pins into the M2.5 holes
pin_h = 1.3;
wall = 2.2;
floor_t = 2.0;
corner_r = 4;

/* [Bezel] */
use_bezel = true;
bezel_t = 1.2;      // thickness over the glass
skirt_h = 3;        // how far the bezel wraps down the tub
skirt_t = 1.0;
window_margin = 0.5;// extra window beyond the active area, each side

/* [Openings] */
io_ports = true;    // I2C + UART SH1.0 connectors on the USB edge
sd_slot = true;
buttons = true;     // flex tabs for BOOT / RST / BAT_PWR
led_holes = true;   // PWR + charge LEDs on the PCB back (approximate)
speaker = true;     // pocket + grille for the 30 x 19 x 7 speaker

/* [Lettering] */
button_labels = true;   // BOOT / RST / PWR engraved under the flex tabs
logo = "SQUACHWATCH";   // engraved on the back; "" for none
logo_size = 4.6;

/* [Speaker] */
spk_l = 30;   // measured: the speaker is ~2 mm longer than its listed 28
spk_w = 19;
spk_d = 7;
spk_center = [25, 54];

/* [Hidden] */
$fn = 48;
eps = 0.01;

// ---- board (Waveshare drawing) ----
pcb_w = 49.9;  pcb_h = 69;  pcb_t = 1.6;
glass_w = 50.54; glass_h = 73.06; glass_r = 2;
glass_x0 = -(glass_w - pcb_w) / 2;
glass_y0 = -(glass_h - pcb_h - 1.92);       // glass overhangs PCB 1.92 at the top
act_w = 43.2;  act_h = 57.6;
act_top = 4.7;                               // glass top edge -> active area
holes = [[4.5, 4.5], [45.5, 4.5], [4.5, 64.5], [45.5, 64.5]];

// edge features, front view
usb_x = 25.3;
uart_x = 12.5;                               // UART header (GND 3V3 TXD RXD)
i2c_x = 38.0;                                // I2C header  (GND 3V3 SDA SCL)
btn_y = [57.0, 49.0, 41.2];                  // BOOT, RST, BAT_PWR (right edge)
btn_names = ["BOOT", "RST", "PWR"];
sd_y = 30.0;                                 // TF slot centre (right edge)
leds = [[32.6, 5.1], [17.6, 5.1]];           // PWR, charge

// ---- derived ----
cav_x0 = glass_x0 - fit;  cav_y0 = glass_y0 - fit;
cav_w = glass_w + 2 * fit; cav_h = glass_h + 2 * fit;
cav_x1 = cav_x0 + cav_w;
out_x0 = cav_x0 - wall;   out_y0 = cav_y0 - wall;
out_w = cav_w + 2 * wall; out_h = cav_h + 2 * wall;
out_x1 = out_x0 + out_w;

z_pcb_bot = floor_t + back_clear;
z_pcb_top = z_pcb_bot + pcb_t;
z_glass = z_pcb_top + lcd_stack;
z_post_top = z_pcb_bot - board_post_h;       // top of the case's posts
tub_h = use_bezel ? z_glass : z_glass + 0.6;
step = skirt_t + bezel_fit;

z_usb = z_pcb_bot - 1.65;
z_io = z_pcb_bot - 1.6;
z_sd = z_pcb_bot - 1.0;
z_btn = z_pcb_bot - 1.0;

tab_l = 8;      // flex tab length (hinge at the bottom)
tab_t = 1.2;    // flex tab thickness
logo_x = 25;    // centre of the back engraving, clear of the grille
logo_y = 22;
logo_len = 40;  // engraving length along the back

echo(str("Case: ", out_w, " x ", out_h, " x ", tub_h + (use_bezel ? bezel_t : 0), " mm"));

module rr2d(w, h, r) offset(r = r) offset(delta = -r) square([w, h]);
module outer2d(inset = 0)
    translate([out_x0 + inset, out_y0 + inset])
        rr2d(out_w - 2 * inset, out_h - 2 * inset, max(0.5, corner_r - inset));
module cav2d()
    translate([cav_x0, cav_y0]) rr2d(cav_w, cav_h, glass_r + fit);
module win2d(grow = 0) {
    m = window_margin + grow;
    translate([glass_x0 + (glass_w - act_w) / 2 - m,
               glass_y0 + glass_h - act_top - act_h - m])
        square([act_w + 2 * m, act_h + 2 * m]);
}
// rounded box centred in x/z, spanning y0..y1
module port_y(cx, cz, w, h, y0, y1, r = 1)
    translate([cx, y1, cz]) rotate([90, 0, 0])
        linear_extrude(y1 - y0) translate([-w / 2, -h / 2]) rr2d(w, h, min(r, h / 2 - 0.01));

// ---------------- back ----------------
module back() {
    difference() {
        union() {
            difference() {
                union() {
                    linear_extrude(use_bezel ? tub_h - skirt_h : tub_h) outer2d();
                    if (use_bezel) linear_extrude(tub_h) outer2d(step);
                }
                translate([0, 0, floor_t]) linear_extrude(tub_h) cav2d();
            }
            // standoffs
            for (h = holes) translate([h[0], h[1], floor_t - eps]) {
                cylinder(d = 6, h = z_post_top - floor_t + eps);
                // pin up into the board's brass post, chamfered tip
                translate([0, 0, z_post_top - floor_t]) {
                    cylinder(d = pin_d, h = pin_h - 0.4);
                    translate([0, 0, pin_h - 0.4]) cylinder(d1 = pin_d, d2 = pin_d - 0.6, h = 0.4);
                }
            }
            // fill the gap under the glass overhang on the USB edge so the
            // ports sit close to the connectors
            translate([3, cav_y0 - eps, floor_t - eps])
                cube([pcb_w - 6, -0.6 - cav_y0, z_pcb_top - 0.1 - floor_t]);
            if (buttons) for (y = btn_y)   // nubs on the flex tabs
                translate([cav_x1 - 0.4, y - 1.2, z_btn - 1.2]) cube([0.4 + eps, 2.4, 2.4]);
            if (speaker) speaker_frame();
        }

        // USB-C: through hole + recess for the plug's overmold
        port_y(usb_x, z_usb, 9.6, 3.8, out_y0 - 1, 0, 1.6);
        port_y(usb_x, z_usb - 0.2, 12.8, 6.8, out_y0 - 1, cav_y0 + 0.6, 2.5);

        if (io_ports) for (x = [uart_x, i2c_x])
            port_y(x, z_io, 8.5, 4.5, out_y0 - 1, 0, 1);

        if (sd_slot) {
            translate([cav_x1 - 1, sd_y - 6.5, z_sd - 1.5]) cube([wall + 2, 13, 3]);
            // finger scoop
            translate([out_x1 + 3.4, sd_y, z_sd]) scale([1, 1.8, 0.8]) sphere(r = 4.6);
        }

        if (buttons) for (y = btn_y) flex_tab_cut(y);

        // names engraved in the wall under each tab, reading along the side
        // with the screen up
        if (buttons && button_labels) for (i = [0 : len(btn_y) - 1])
            multmatrix([[0, 0, 1, out_x1 - 0.5], [1, 0, 0, btn_y[i]], [0, 1, 0, z_btn - tab_l + 2 - 2.2], [0, 0, 0, 1]])
                linear_extrude(1) text(btn_names[i], size = 1.8, halign = "center", valign = "center",
                                       font = "Liberation Sans:style=Bold");

        // name on the back, reading right way round with the case face-down
        if (logo != "")
            translate([logo_x, logo_y, -eps]) linear_extrude(0.6) mirror([1, 0]) rotate(90)
                resize([logo_len, 0], auto = true)
                    text(logo, size = logo_size, halign = "center", valign = "center",
                         font = "Liberation Sans:style=Bold");

        if (led_holes) for (l = leds) translate([l[0], l[1], -1]) cylinder(d = 2, h = floor_t + 2);

        if (speaker) grille();
    }
    // round pads on the flex tabs, 0.6 proud of the wall so you can find them
    if (buttons) for (y = btn_y)
        translate([cav_x1 + tab_t - eps, y, z_btn]) rotate([0, 90, 0])
            cylinder(d1 = 4.6, d2 = 3.8, h = wall - tab_t + 0.6 + eps);
}

// U-slot through the right wall, hinge at the bottom, tab thinned outside
module flex_tab_cut(y) {
    tw = 5; tl = tab_l; s = 0.7; tt = tab_t;
    z1 = z_btn + 2; z0 = z1 - tl;
    translate([cav_x1 - 1, 0, 0]) difference() {
        translate([0, y - tw / 2 - s, z0]) cube([wall + 2, tw + 2 * s, tl + s]);
        translate([-1, y - tw / 2, z0 - 1]) cube([wall + 4, tw, tl + 1]);
    }
    translate([cav_x1 + tt, y - tw / 2 - eps, z0]) cube([wall, tw + 2 * eps, tl]);
}

module speaker_frame() {
    c = 0.4; r = 1.2; h = 2.5;
    translate([spk_center[0], spk_center[1], floor_t - eps])
        linear_extrude(h) difference() {
            offset(r = r) square([spk_l + 2 * c, spk_w + 2 * c], center = true);
            square([spk_l + 2 * c, spk_w + 2 * c], center = true);
            // gaps for the lead wires and for prying it out
            square([6, spk_w + 10], center = true);
            square([spk_l + 10, 6], center = true);
        }
}

module grille() {
    p = 2.6; d = 1.6;
    gx = spk_l - 8; gy = spk_w - 7;
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
            crush_ribs();
        }
        // pry notch on the plain left side, for getting the lid off
        translate([out_x0 - 1, out_y0 + out_h / 2 - 5, z_glass - skirt_h - 1]) cube([skirt_t + 2, 10, 2.2]);
        hull() {
            translate([0, 0, z_glass - eps]) linear_extrude(eps) win2d(0);
            translate([0, 0, z_glass + bezel_t]) linear_extrude(eps * 2) win2d(bezel_t * 0.8);
        }
    }
}

// vertical ribs inside the lid's skirt; they bite into the tub's step
module crush_ribs() {
    rh = bezel_fit + rib_interf;
    z0 = z_glass - skirt_h + 0.8;            // lead-in below the ribs
    ix0 = out_x0 + skirt_t;  ix1 = out_x1 - skirt_t;
    iy0 = out_y0 + skirt_t;  iy1 = out_y0 + out_h - skirt_t;
    for (f = [0.25, 0.5, 0.75]) {            // long sides
        y = iy0 + (iy1 - iy0) * f;
        translate([ix0 - eps, y - 0.5, z0]) cube([rh + eps, 1, z_glass - z0 + eps]);
        translate([ix1 - rh, y - 0.5, z0]) cube([rh + eps, 1, z_glass - z0 + eps]);
    }
    for (f = [1 / 3, 2 / 3]) {               // short sides
        x = ix0 + (ix1 - ix0) * f;
        translate([x - 0.5, iy0 - eps, z0]) cube([1, rh + eps, z_glass - z0 + eps]);
        translate([x - 0.5, iy1 - rh, z0]) cube([1, rh + eps, z_glass - z0 + eps]);
    }
}

// ---------------- mock board for the assembly view ----------------
module board() {
    color("#1a1a1a") translate([0, 0, z_pcb_bot]) difference() {
        cube([pcb_w, pcb_h, pcb_t]);
        for (h = holes) translate([h[0], h[1], -1]) cylinder(d = 2.7, h = 4);
    }
    color("#b08d57") for (h = holes) translate([h[0], h[1], z_post_top]) difference() {
        cylinder(d = 4.5, h = board_post_h, $fn = 6);
        translate([0, 0, -1]) cylinder(d = 2.5, h = board_post_h + 2);
    }
    color("#222") translate([glass_x0, glass_y0, z_pcb_top + 0.2])
        linear_extrude(lcd_stack - 0.2) rr2d(glass_w, glass_h, glass_r);
    color("#3a6ea5") translate([0, 0, z_glass]) linear_extrude(0.05) win2d(-window_margin);
    color("silver") port_y(usb_x, z_usb, 8.9, 3.2, -0.3, 7);
    if (speaker) color("#666") translate([spk_center[0] - spk_l / 2, spk_center[1] - spk_w / 2, floor_t])
        cube([spk_l, spk_w, spk_d]);
}

module bezel_print() translate([0, 0, z_glass + bezel_t]) rotate([180, 0, 0]) bezel();

if (part == "back") back();
else if (part == "bezel") bezel_print();
else if (part == "print") {
    back();
    translate([out_w + 8, out_y0 + out_h + out_y0, 0]) bezel_print();
}
else {
    color("#e8e2d0") back();
    board();
    if (use_bezel) color("#c9b98f") translate([0, 0, 8]) bezel();
}
