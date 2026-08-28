#!/usr/bin/env python3
"""Generate the 72x72 app-tile icons for the Apps screen.

Draws each icon with Pillow at 4x and downsamples, rather than shipping SVGs and a
cairosvg dependency (convert_icon.py's path), because these eight shapes are all
rectangles, lines, circles and arcs -- primitives ImageDraw already has.

Output matches what GfxRenderer::drawIcon expects: square, 1bpp, MSB-first, and
bit == 0 is ink. drawIcon plots each bitmap pixel at (x + size-1-row, y + col),
which transposes and flips, so the stored bitmap is pre-rotated to compensate:

    bitmap[row][col] = screen(size - 1 - row, col)

Geometry is authored in the same 24-unit space as the design mockups so the two
stay comparable; SS is the supersample factor and U scales 24 units to pixels.

    python scripts/gen_app_icons.py [--preview contact-sheet.png]
"""

import sys
from PIL import Image, ImageDraw

SIZE = 72
SS = 4
CANVAS = SIZE * SS
U = CANVAS / 24.0          # 24-unit design space -> supersampled pixels
# Pinned to a whole 3px after downsampling (3 * SS), not 1.6 design units: a
# fractional final stroke (1.6 units lands on 3.75px) leaves every edge between
# pixels, which a 1-bit threshold turns into ragged, uneven outlines.
STROKE = 3 * SS
OUT_DIR = "src/components/icons"


def u(*vals):
    return tuple(v * U for v in vals)


def line(d, x1, y1, x2, y2, w=None):
    d.line([u(x1, y1)[0], u(x1, y1)[1], u(x2, y2)[0], u(x2, y2)[1]],
           fill=0, width=w or STROKE)


def rect(d, x1, y1, x2, y2, w=None):
    d.rectangle([u(x1, y1)[0], u(x1, y1)[1], u(x2, y2)[0], u(x2, y2)[1]],
                outline=0, width=w or STROKE)


def _round_box(cx, cy, r, w):
    # Pillow grows an arc/ellipse stroke INWARD from the bounding box, while line()
    # centres its stroke on the path. Left uncorrected the two disagree by w/2, which
    # shows up as a notch wherever a straight edge meets a knob. Inflate the box by
    # half a stroke so the arc's centreline lands on r, matching the lines.
    pad = w / 2.0
    return [u(cx, cy)[0] - r * U - pad, u(cx, cy)[1] - r * U - pad,
            u(cx, cy)[0] + r * U + pad, u(cx, cy)[1] + r * U + pad]


def circle(d, cx, cy, r, w=None, fill=False):
    if fill:
        d.ellipse([u(cx - r, cy - r)[0], u(cx - r, cy - r)[1],
                   u(cx + r, cy + r)[0], u(cx + r, cy + r)[1]], fill=0)
    else:
        w = w or STROKE
        d.ellipse(_round_box(cx, cy, r, w), outline=0, width=w)


def arc(d, cx, cy, r, start, end, w=None):
    w = w or STROKE
    d.arc(_round_box(cx, cy, r, w), start, end, fill=0, width=w)


# --- the eight icons, in the order AppsActivity lists them -------------------

def draw_files(d):
    # Folder: back panel with a tab, front panel across the bottom.
    line(d, 3.2, 6.0, 9.0, 6.0)
    line(d, 9.0, 6.0, 10.8, 8.0)
    rect(d, 3.2, 8.0, 20.8, 19.0)
    line(d, 3.2, 6.0, 3.2, 8.4)


def draw_quran(d):
    # Open book: two leaves meeting at a centre spine.
    line(d, 12, 6.6, 12, 19.2)
    for s in (-1, 1):
        x_out = 12 + s * 8.2
        line(d, 12, 6.6, x_out, 5.0)
        line(d, x_out, 5.0, x_out, 17.6)
        line(d, x_out, 17.6, 12, 19.2)


# Puzzle piece P1, centred on (12,12): a square with sockets cut into the top and
# left edges and knobs bulging out of the right and bottom ones. r is deliberately
# generous relative to the square -- at 56px a knob narrower than about 4x the
# stroke stops reading as a tab and turns the whole outline into a blob. At 56px
# four features never resolved at all, which is why the tile icon is 72px.
PUZZLE_A, PUZZLE_B, PUZZLE_R = 5.0, 17.0, 2.2
PUZZLE_M = (PUZZLE_A + PUZZLE_B) / 2.0


def _puzzle_solid(d, delta, color):
    """Fill the piece's silhouette, grown (delta > 0) or shrunk (delta < 0).

    Knobs grow with the body; sockets are cut with the OPPOSITE sign, since a
    smaller cut leaves a larger shape.
    """
    a, b, r, m = PUZZLE_A, PUZZLE_B, PUZZLE_R, PUZZLE_M
    box = lambda cx, cy, rad: [u(cx - rad, cy - rad)[0], u(cx - rad, cy - rad)[1],
                               u(cx + rad, cy + rad)[0], u(cx + rad, cy + rad)[1]]
    d.rectangle([u(a - delta, a - delta)[0], u(a - delta, a - delta)[1],
                 u(b + delta, b + delta)[0], u(b + delta, b + delta)[1]], fill=color)
    d.ellipse(box(b, m, r + delta), fill=color)   # right knob
    d.ellipse(box(m, b, r + delta), fill=color)   # bottom knob


def _puzzle_sockets(d, delta, color):
    a, b, r, m = PUZZLE_A, PUZZLE_B, PUZZLE_R, PUZZLE_M
    box = lambda cx, cy, rad: [u(cx - rad, cy - rad)[0], u(cx - rad, cy - rad)[1],
                               u(cx + rad, cy + rad)[0], u(cx + rad, cy + rad)[1]]
    d.ellipse(box(m, a, r - delta), fill=color)   # top socket
    d.ellipse(box(a, m, r - delta), fill=color)   # left socket


def draw_games(d):
    # Built as outer silhouette MINUS an inset copy, rather than twelve stroked
    # segments: stroking each edge and arc separately left nubs at the corners and
    # made the knob joins disagree by half a stroke.
    half = (STROKE / 2.0) / U      # half the stroke, in design units
    from PIL import Image as _Image, ImageDraw as _ImageDraw
    outer = _Image.new("L", (CANVAS, CANVAS), 255)
    od = _ImageDraw.Draw(outer)
    _puzzle_solid(od, half, 0)
    _puzzle_sockets(od, half, 255)
    inner = _Image.new("L", (CANVAS, CANVAS), 255)
    idr = _ImageDraw.Draw(inner)
    _puzzle_solid(idr, -half, 0)
    _puzzle_sockets(idr, -half, 255)
    # ink = outer AND NOT inner
    op, ip = outer.load(), inner.load()
    for y in range(CANVAS):
        for x in range(CANVAS):
            if op[x, y] == 0 and ip[x, y] != 0:
                d.point((x, y), fill=0)


def draw_tasbih(d):
    # Eight prayer beads on a ring.
    import math
    for i in range(8):
        ang = math.radians(i * 45 - 90)
        circle(d, 12 + 7.4 * math.cos(ang), 12 + 7.4 * math.sin(ang), 1.9)


def draw_stopwatch(d):
    circle(d, 12, 13.6, 7.4)
    line(d, 12, 13.6, 12, 9.4)        # hand
    line(d, 9.4, 2.8, 14.6, 2.8)      # crown bar
    line(d, 12, 2.8, 12, 6.2)         # crown stem


def draw_pomodoro(d):
    # Timer dial with a stem and two leaves.
    circle(d, 12, 13.8, 7.2)
    line(d, 12, 13.8, 12, 9.8)
    line(d, 12, 6.6, 12, 4.0)
    line(d, 12, 6.0, 8.6, 5.2)
    line(d, 12, 6.0, 15.4, 5.2)


def draw_gym(d):
    # Dumbbell: two end weights, two inner collars, one bar.
    line(d, 3.4, 9.0, 3.4, 15.0)
    line(d, 7.0, 6.6, 7.0, 17.4)
    line(d, 17.0, 6.6, 17.0, 17.4)
    line(d, 20.6, 9.0, 20.6, 15.0)
    line(d, 7.0, 12, 17.0, 12)


def draw_ble(d):
    # Bluetooth rune.
    line(d, 12, 3.6, 12, 20.4)
    line(d, 12, 3.6, 16.4, 7.8)
    line(d, 16.4, 7.8, 7.6, 16.2)
    line(d, 12, 20.4, 16.4, 16.2)
    line(d, 16.4, 16.2, 7.6, 7.8)


def draw_prayer(d):
    # Mosque: a dome on a base, one minaret to each side. Every edge is either
    # straight or a half-circle, which is what survives a 1-bit threshold at
    # tile size -- the crescent that first sat on the dome did not.
    arc(d, 12, 11.4, 4.4, 180, 360)   # dome
    line(d, 7.6, 11.4, 7.6, 19.2)     # dome sides down to the base
    line(d, 16.4, 11.4, 16.4, 19.2)
    line(d, 12, 4.6, 12, 6.8)         # finial
    line(d, 4.0, 9.6, 4.0, 19.2)      # left minaret
    line(d, 20.0, 9.6, 20.0, 19.2)    # right minaret
    line(d, 2.8, 19.2, 21.2, 19.2)    # base


ICONS = [
    ("AppFiles72Icon", "appfiles72", draw_files),
    ("AppQuran72Icon", "appquran72", draw_quran),
    ("AppGames72Icon", "appgames72", draw_games),
    ("AppTasbih72Icon", "apptasbih72", draw_tasbih),
    ("AppStopwatch72Icon", "appstopwatch72", draw_stopwatch),
    ("AppPomodoro72Icon", "apppomodoro72", draw_pomodoro),
    ("AppGym72Icon", "appgym72", draw_gym),
    ("AppBle72Icon", "appble72", draw_ble),
    ("AppPrayer72Icon", "appprayer72", draw_prayer),
]


def render(fn):
    """Draw at SS x and downsample to a 1-bit SIZE x SIZE image (0 = ink)."""
    img = Image.new("L", (CANVAS, CANVAS), 255)
    fn(ImageDraw.Draw(img))
    # BOX averages the supersampled block; LANCZOS rings and frays a 1-bit threshold.
    small = img.resize((SIZE, SIZE), Image.BOX)
    return small.point(lambda p: 0 if p < 128 else 255, mode="L")


def to_c_array(img, name):
    px = img.load()
    row_bytes = (SIZE + 7) // 8
    out = []
    for row in range(SIZE):
        for byte_i in range(row_bytes):
            byte = 0
            for bit in range(8):
                col = byte_i * 8 + bit
                ink = False
                if col < SIZE:
                    # drawIcon plots bitmap[row][col] at screen(size-1-row, col).
                    ink = px[SIZE - 1 - row, col] == 0
                if not ink:
                    byte |= 1 << (7 - bit)
            out.append(byte)

    body = ""
    # 19 bytes per line, not a rounder 12: every token is the same six
    # characters ("0xFF, "), so 4 + 19*6 fills the .clang-format ColumnLimit
    # of 120 exactly and a 20th would overflow it. Anything narrower just gets
    # repacked to this by the next bin/clang-format-fix run, and CI's format
    # check then fails on freshly generated output.
    for i in range(0, len(out), 19):
        body += "    " + ", ".join("0x%02X" % b for b in out[i:i + 19]) + ",\n"
    return (
        "#pragma once\n#include <cstdint>\n\n"
        "// size: %dx%d -- generated by scripts/gen_app_icons.py, do not edit by hand.\n"
        "// 1bpp, MSB-first, bit == 0 is ink, pre-rotated for GfxRenderer::drawIcon.\n"
        "static const uint8_t %s[] = {\n%s};\n" % (SIZE, SIZE, name, body.rstrip(",\n"))
    )


def main():
    preview = None
    if "--preview" in sys.argv:
        preview = sys.argv[sys.argv.index("--preview") + 1]

    sheet = Image.new("L", (SIZE * len(ICONS), SIZE), 255)
    for i, (name, stem, fn) in enumerate(ICONS):
        img = render(fn)
        sheet.paste(img, (i * SIZE, 0))
        path = "%s/%s.h" % (OUT_DIR, stem)
        with open(path, "w", newline="\n") as f:
            f.write(to_c_array(img, name))
        print("wrote %s (%s)" % (path, name))

    if preview:
        sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(preview)
        print("preview -> %s" % preview)


if __name__ == "__main__":
    main()
