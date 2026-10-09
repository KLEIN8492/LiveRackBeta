"""Draws the LiveRack app icon (Resources/AppIcon.png, 1024x1024).

Utilitarian: a flat dark tile holding a 2U rack front panel, rack ears with
screw holes, a square power LED and a segmented level meter. No gradients,
no glow. Run: python3 Resources/make_icon.py
"""
from pathlib import Path
from PIL import Image, ImageDraw

S = 4
W = 1024 * S

TILE = (27, 27, 27)
TILE_EDGE = (61, 61, 61)
PLATE = (52, 52, 52)
PLATE_EDGE = (78, 78, 78)
HOLE = (20, 20, 20)
INK = (220, 220, 220)
GREEN, AMBER, RED = (88, 194, 88), (224, 160, 48), (226, 74, 59)


def px(v):
    return int(round(v * S))


def rect(d, box, **kw):
    d.rectangle([px(c) for c in box], **kw)


def main():
    icon = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # macOS tile shape (kept so it sits right in the Dock), flat fill.
    d.rounded_rectangle([px(100), px(100), px(924), px(924)], radius=px(185), fill=TILE,
                        outline=TILE_EDGE, width=px(4))

    # 2U front panel.
    left, right, top, bottom = 170, 854, 352, 672
    rect(d, (left, top, right, bottom), fill=PLATE, outline=PLATE_EDGE, width=px(4))

    # Rack ears: slotted screw holes.
    for x in (left + 40, right - 40):
        for y in (top + 62, bottom - 62):
            rect(d, (x - 22, y - 11, x + 22, y + 11), fill=HOLE)
    for x in (left + 82, right - 82):
        d.line([px(x), px(top + 18), px(x), px(bottom - 18)], fill=PLATE_EDGE, width=px(4))

    # Power LED + label stub.
    lx, ly = left + 130, top + 70
    rect(d, (lx, ly, lx + 34, ly + 34), fill=GREEN, outline=HOLE, width=px(3))
    rect(d, (lx, ly + 58, lx + 110, ly + 70), fill=INK)

    # Segmented meter: 10 segments, 7 lit (green, green... amber), red dark.
    mx0, mx1, my0, my1 = left + 130, right - 130, bottom - 118, bottom - 64
    n, gap = 10, 10
    seg = (mx1 - mx0 - gap * (n - 1)) / n
    for i in range(n):
        c = RED if i >= 9 else AMBER if i >= 7 else GREEN
        lit = i < 8
        colour = c if lit else tuple(int(v * 0.3) for v in c)
        x0 = mx0 + i * (seg + gap)
        rect(d, (x0, my0, x0 + seg, my1), fill=colour)

    # Panel legend boxes, kept inside the ears.
    for k in range(3):
        x0 = right - 130 - 62 - k * 92
        rect(d, (x0, top + 70, x0 + 62, top + 104), outline=PLATE_EDGE, width=px(4))

    out = Path(__file__).with_name("AppIcon.png")
    icon.resize((1024, 1024), Image.LANCZOS).save(out)
    print(out)


if __name__ == "__main__":
    main()
