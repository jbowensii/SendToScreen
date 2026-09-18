"""Render icon candidates (and the final .ico) for SendToScreen. Usage:
   python make_icons.py            -> candidates A-D as 256px PNGs + contact sheet
   python make_icons.py A          -> icons/icon.ico (16..256) from concept A
Drawn at 8x and downsampled so edges stay clean."""
import sys, math
from PIL import Image, ImageDraw

S = 256          # output size
SS = 8           # supersample factor
W = S * SS

def canvas():
    return Image.new("RGBA", (W, W), (0, 0, 0, 0))

def rr(d, box, r, fill, outline=None, width=0):
    d.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)

def arrow(d, x0, y, x1, color, t):
    """Thick right-pointing arrow from x0 to x1 at height y, thickness t."""
    head = t * 2.2
    d.line([(x0, y), (x1 - head * 0.6, y)], fill=color, width=t)
    d.polygon([(x1, y), (x1 - head, y - head * 0.8), (x1 - head, y + head * 0.8)], fill=color)

def monitor(d, x, y, w, h, body, screen, r=None, stand=True):
    r = r or w * 0.06
    rr(d, (x, y, x + w, y + h), r, body)
    inset = w * 0.06
    rr(d, (x + inset, y + inset, x + w - inset, y + h - inset), r * 0.6, screen)
    if stand:
        sw, sh = w * 0.28, h * 0.12
        d.rectangle((x + w / 2 - sw * 0.25, y + h, x + w / 2 + sw * 0.25, y + h + sh), fill=body)
        rr(d, (x + w / 2 - sw / 2, y + h + sh, x + w / 2 + sw / 2, y + h + sh * 1.6), sh * 0.3, body)

def bg_square(d, c1, c2):
    """Vertical gradient rounded square background."""
    img = Image.new("RGBA", (W, W), c1)
    top = Image.new("RGBA", (W, W), c2)
    mask = Image.linear_gradient("L").resize((W, W))
    img = Image.composite(top, img, mask)
    m = Image.new("L", (W, W), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, W - 1, W - 1), radius=W * 0.22, fill=255)
    img.putalpha(m)
    return img

BLUE, TEAL, NAVY, WHITE = (0, 120, 212, 255), (0, 178, 148, 255), (16, 34, 62, 255), (255, 255, 255, 255)
PURPLE, INDIGO, ORANGE = (110, 60, 200, 255), (40, 70, 190, 255), (255, 140, 0, 255)

def concept_a():
    """Two monitors, window hopping from left to right on a blue gradient tile."""
    img = bg_square(None, (0, 90, 180, 255), (0, 140, 230, 255))
    d = ImageDraw.Draw(img)
    u = W / 256
    monitor(d, 26 * u, 84 * u, 92 * u, 66 * u, WHITE, (0, 60, 130, 255))
    monitor(d, 138 * u, 84 * u, 92 * u, 66 * u, WHITE, (0, 60, 130, 255))
    rr(d, (44 * u, 100 * u, 84 * u, 132 * u), 3 * u, ORANGE)          # window on left screen
    arrow(d, 96 * u, 60 * u, 188 * u, WHITE, int(10 * u))            # arc-ish hop over the top
    return img

def concept_b():
    """Dark navy circle, one big monitor with a window flying out to a small target screen."""
    img = canvas(); d = ImageDraw.Draw(img); u = W / 256
    d.ellipse((4 * u, 4 * u, 252 * u, 252 * u), fill=NAVY)
    monitor(d, 34 * u, 70 * u, 120 * u, 84 * u, (200, 215, 235, 255), (30, 60, 110, 255))
    rr(d, (54 * u, 88 * u, 104 * u, 126 * u), 4 * u, TEAL)
    arrow(d, 112 * u, 107 * u, 196 * u, TEAL, int(12 * u))
    monitor(d, 176 * u, 84 * u, 54 * u, 40 * u, (200, 215, 235, 255), (30, 60, 110, 255), stand=False)
    rr(d, (186 * u, 92 * u, 220 * u, 116 * u), 2 * u, TEAL)
    return img

def concept_c():
    """Purple-to-indigo tile, white window glyph with a bold chevron: minimal Fluent look."""
    img = bg_square(None, PURPLE, INDIGO)
    d = ImageDraw.Draw(img); u = W / 256
    rr(d, (40 * u, 62 * u, 150 * u, 150 * u), 8 * u, WHITE)
    d.rectangle((40 * u, 62 * u, 150 * u, 82 * u), fill=(225, 225, 240, 255))
    rr(d, (40 * u, 62 * u, 150 * u, 82 * u), 8 * u, (225, 225, 240, 255))
    d.rectangle((40 * u, 74 * u, 150 * u, 82 * u), fill=(225, 225, 240, 255))
    t = int(16 * u)
    d.line([(150 * u, 106 * u), (200 * u, 106 * u)], fill=WHITE, width=t)
    d.line([(178 * u, 78 * u), (212 * u, 106 * u), (178 * u, 134 * u)], fill=WHITE, width=t, joint="curve")
    return img

def concept_d():
    """Three monitors in a row (multi-screen desk), middle lit, curved arrow landing on it."""
    img = bg_square(None, (20, 40, 70, 255), (35, 70, 120, 255))
    d = ImageDraw.Draw(img); u = W / 256
    for i, x in enumerate((14, 90, 166)):
        lit = i == 1
        monitor(d, x * u, 96 * u, 76 * u, 56 * u, WHITE if lit else (150, 170, 200, 255), TEAL if lit else (40, 60, 95, 255))
    # curved arrow from left screen up and over into the middle
    pts = []
    for k in range(41):
        a = math.pi * (1 - k / 40)
        pts.append((52 * u + 76 * u * (1 - math.cos(a)) / 2, 90 * u - 46 * u * math.sin(a)))
    d.line(pts, fill=ORANGE, width=int(10 * u), joint="curve")
    d.polygon([(128 * u, 96 * u), (110 * u, 76 * u), (146 * u, 80 * u)], fill=ORANGE)
    return img

CONCEPTS = {"A": concept_a, "B": concept_b, "C": concept_c, "D": concept_d}

def render(name):
    return CONCEPTS[name]().resize((S, S), Image.LANCZOS)

if __name__ == "__main__":
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    if len(sys.argv) > 1:
        big = CONCEPTS[sys.argv[1].upper()]()
        sizes = [256, 128, 64, 48, 32, 24, 16]
        frames = [big.resize((s, s), Image.LANCZOS) for s in sizes]
        frames[0].save(os.path.join(here, "icon.ico"), format="ICO", sizes=[(s, s) for s in sizes], append_images=frames[1:])
        frames[0].save(os.path.join(here, "icon256.png"))
        print("wrote icon.ico with sizes", sizes)
    else:
        sheet = Image.new("RGBA", (S * 4 + 100, S + 120), (245, 245, 245, 255))
        sd = ImageDraw.Draw(sheet)
        for i, n in enumerate("ABCD"):
            im = render(n)
            im.save(os.path.join(here, f"concept_{n}.png"))
            x = 20 + i * (S + 20)
            sheet.paste(im, (x, 20), im)
            small = im.resize((32, 32), Image.LANCZOS)
            sheet.paste(small, (x + S // 2 - 16, S + 40), small)          # tray-size preview
            sd.text((x + S // 2 - 4, S + 84), n, fill=(0, 0, 0, 255))
        sheet.save(os.path.join(here, "contact_sheet.png"))
        print("wrote concept_A-D.png and contact_sheet.png")
