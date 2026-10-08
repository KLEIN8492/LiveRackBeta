"""Draws the LiveRack app icon (Resources/AppIcon.png, 1024x1024).

Flat and restrained, in the app's own palette: a dark macOS-shaped tile
holding three rack units with status LEDs, and a green signal trace
running through the middle unit. Run: python3 Resources/make_icon.py
"""
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

S = 4                      # supersampling factor
W = 1024 * S

BG_TOP, BG_BOTTOM = (36, 40, 47), (19, 21, 24)
UNIT, UNIT_EDGE = (42, 47, 55), (58, 64, 74)
SCREW = (24, 27, 31)
SLOT = (64, 71, 82)
GREEN = (62, 207, 142)
GREEN_DIM = (62, 207, 142, 70)


def px(v):
    return int(v * S)


def rounded(draw, box, r, **kw):
    draw.rounded_rectangle([px(c) for c in box], radius=px(r), **kw)


def tile_mask():
    m = Image.new("L", (W, W), 0)
    rounded(ImageDraw.Draw(m), (100, 100, 924, 924), 185, fill=255)
    return m


def main():
    icon = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    mask = tile_mask()

    # Soft drop shadow, as in Apple's macOS icon template.
    shadow = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    shadow.putalpha(mask.point(lambda a: a * 90 // 255))
    shadow = shadow.transform(shadow.size, Image.AFFINE, (1, 0, 0, 0, 1, -px(12)))
    shadow = shadow.filter(ImageFilter.GaussianBlur(px(18)))
    icon.alpha_composite(shadow)

    # Tile: vertical gradient clipped to the rounded square.
    grad = Image.new("RGBA", (W, W))
    gd = ImageDraw.Draw(grad)
    for y in range(W):
        t = min(1.0, max(0.0, (y - px(100)) / px(824)))
        c = tuple(int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t) for i in range(3))
        gd.line([(0, y), (W, y)], fill=c + (255,))
    icon.paste(grad, (0, 0), mask)

    d = ImageDraw.Draw(icon)
    rounded(d, (100, 100, 924, 924), 185, outline=(255, 255, 255, 18), width=px(3))

    # Three rack units.
    units = [(262, 382), (452, 572), (642, 762)]
    left, right = 214, 810
    for i, (top, bottom) in enumerate(units):
        rounded(d, (left, top, right, bottom), 22, fill=UNIT, outline=UNIT_EDGE, width=px(3))

        # Rack ears with screws.
        cy = (top + bottom) / 2
        for sx in (left + 34, right - 34):
            r = 13
            d.ellipse([px(sx - r), px(cy - r), px(sx + r), px(cy + r)], fill=SCREW)

        # Status LED with a soft glow.
        lx = left + 92
        glow = Image.new("RGBA", (W, W), (0, 0, 0, 0))
        ImageDraw.Draw(glow).ellipse([px(lx - 34), px(cy - 34), px(lx + 34), px(cy + 34)], fill=GREEN_DIM)
        icon.alpha_composite(glow.filter(ImageFilter.GaussianBlur(px(14))))
        d = ImageDraw.Draw(icon)
        d.ellipse([px(lx - 15), px(cy - 15), px(lx + 15), px(cy + 15)], fill=GREEN)

        if i != 1:
            # Plain units: a row of front-panel slots.
            start, end, n, gap = left + 150, right - 70, 4, 34
            slot_w = (end - start - gap * (n - 1)) / n
            for k in range(n):
                x0 = start + k * (slot_w + gap)
                rounded(d, (x0, cy - 9, x0 + slot_w, cy + 9), 9, fill=SLOT)

    # Middle unit: the live signal passing through.
    top, bottom = units[1]
    cy = (top + bottom) / 2
    x0, x1 = left + 140, right - 70
    pts = []
    for k in range(400):
        t = k / 399
        x = x0 + (x1 - x0) * t
        env = math.sin(math.pi * t) ** 1.5          # swell in the middle
        y = cy - 38 * env * math.sin(2 * math.pi * 4.5 * t)
        pts.append((px(x), px(y)))
    d.line(pts, fill=GREEN, width=px(11), joint="curve")
    for x, y in (pts[0], pts[-1]):
        r = px(5.5)
        d.ellipse([x - r, y - r, x + r, y + r], fill=GREEN)

    out = Path(__file__).with_name("AppIcon.png")
    icon.resize((1024, 1024), Image.LANCZOS).save(out)
    print(out)


if __name__ == "__main__":
    main()
