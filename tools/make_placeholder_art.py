#!/usr/bin/env python3
"""Draw placeholder Pupu frames with simple shapes so development does not wait for art.

Writes one horizontal strip per animation (48 px per frame) into assets/images/pupu/,
using only colours from assets/images/pupu/pupu.gpl. Icons (hand, sheet, wolf) are
plain RGBA images at their own size and may use any colour. Replace any file with
real pixel art later (for example from Piskel) and rerun tools/gen_pupu_assets.py.

Runs on the system Python 3.9 with Pillow: /usr/bin/python3 tools/make_placeholder_art.py
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ART_DIR = ROOT / "assets" / "images" / "pupu"
W = 48

CLEAR, OUTLINE, FACE, FACE_HI, EYE, BLUSH, HORN, HORN_DARK = range(8)
WOOL_LIGHT, WOOL_MID, WOOL_DARK, SHELL, SHELL_DARK, INK, STAR, CLOUD = range(8, 16)


def read_palette(path):
    colours = []
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) >= 3 and all(p.isdigit() for p in parts[:3]):
            colours.append(tuple(int(p) for p in parts[:3]))
    if len(colours) != 16:
        sys.exit("{}: expected 16 colours, found {}".format(path, len(colours)))
    return colours


class Frame:
    def __init__(self, size=W):
        self.size = size
        self.px = [[CLEAR] * size for _ in range(size)]

    def put(self, x, y, slot):
        if 0 <= x < self.size and 0 <= y < self.size:
            self.px[y][x] = slot

    def ellipse(self, cx, cy, rx, ry, fill, outline=None, shade=None):
        """Filled ellipse; optional 1 px outline and a lower shadow band."""
        for y in range(self.size):
            for x in range(self.size):
                dx, dy = x - cx, y - cy
                if dx * dx * ry * ry + dy * dy * rx * rx > rx * rx * ry * ry:
                    continue
                slot = fill
                if shade is not None:
                    slot = shade(dx, dy)
                inner = dx * dx * (ry - 1) * (ry - 1) + dy * dy * (rx - 1) * (rx - 1)
                if outline is not None and inner > (rx - 1) * (rx - 1) * (ry - 1) * (ry - 1):
                    slot = outline
                self.put(x, y, slot)


def wool_shade(rx, ry):
    def shade(dx, dy):
        if dy > ry // 2:
            return WOOL_DARK
        if dx + dy < -(rx + ry) // 3:
            return WOOL_LIGHT
        return WOOL_MID
    return shade


def draw_eyes(f, cx, head_y, eyes):
    for side, ex in ((-1, cx - 4), (1, cx + 3)):
        if eyes == "blink":
            f.put(ex, head_y + 1, EYE)
            f.put(ex + 1, head_y + 1, EYE)
        elif eyes == "happy":  # ^ ^
            f.put(ex - 1, head_y + 1, EYE)
            f.put(ex, head_y, EYE)
            f.put(ex + 1, head_y, EYE)
            f.put(ex + 2, head_y + 1, EYE)
        elif eyes == "squint":  # > <
            tip, back = (ex + 1, ex) if side < 0 else (ex, ex + 1)
            f.put(back, head_y - 1, EYE)
            f.put(tip, head_y, EYE)
            f.put(back, head_y + 1, EYE)
        elif eyes == "frown":  # dot eyes under flat brows (annoyed)
            f.put(ex, head_y, EYE)
            f.put(ex + 1, head_y, EYE)
            f.put(ex, head_y + 1, EYE)
            for bx in range(ex - 1, ex + 3):
                f.put(bx, head_y - 2, EYE)
        elif eyes == "angry":  # brows slanting down toward the nose
            f.put(ex, head_y, EYE)
            f.put(ex + 1, head_y, EYE)
            inner = ex + 1 if side < 0 else ex
            outer = ex - 1 if side < 0 else ex + 2
            f.put(outer, head_y - 3, EYE)
            f.put((outer + inner) // 2, head_y - 2, EYE)
            f.put(inner, head_y - 1, EYE)
        elif eyes == "dizzy":  # x x
            for dx, dy in ((-1, -1), (1, -1), (0, 0), (-1, 1), (1, 1)):
                f.put(ex + dx, head_y + dy, EYE)
        elif eyes == "sad":  # half-closed lines, lower than a blink
            f.put(ex - 1, head_y + 1, EYE)
            f.put(ex, head_y + 1, EYE)
            f.put(ex + 1, head_y + 1, EYE)
            f.put(ex + 2, head_y + 2, EYE) if side > 0 else f.put(ex - 2, head_y + 2, EYE)
        else:
            f.put(ex, head_y, EYE)
            f.put(ex + 1, head_y, EYE)
            f.put(ex, head_y + 1, EYE)


def draw_sheet(f, cx, body_y):
    """A small spreadsheet held in front of the body: white paper, grey grid, dark border."""
    x0, x1, y0, y1 = cx - 8, cx + 8, body_y - 4, body_y + 8
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            edge = x in (x0, x1) or y in (y0, y1)
            grid = (x - x0) % 4 == 0 or (y - y0) % 4 == 0
            f.put(x, y, OUTLINE if edge else CLOUD if grid else EYE)


def draw_extras(f, cx, head_y, head_r, body_y, extras):
    top = head_y - head_r - 4  # top of the eggshell cap
    for extra in extras:
        if extra == "sweat":  # a passport-ink drop beside the head
            f.put(cx + head_r + 2, head_y - 3, INK)
            f.put(cx + head_r + 2, head_y - 2, INK)
            f.put(cx + head_r + 3, head_y - 2, INK)
        elif extra == "sweat2":
            f.put(cx + head_r + 3, head_y, INK)
            f.put(cx + head_r + 3, head_y + 1, INK)
            f.put(cx + head_r + 4, head_y + 1, INK)
        elif extra in ("cloud", "cloud2"):  # a little storm cloud above the head, to the right
            ox = 1 if extra == "cloud2" else 0
            ccx, ccy = cx + head_r + 3 + ox, head_y - head_r - 1
            f.ellipse(ccx, ccy, 5, 3, CLOUD, OUTLINE)
            f.ellipse(ccx - 1, ccy - 2, 3, 2, CLOUD)
            if extra == "cloud2":  # rain
                for dx in (-3, 0, 3):
                    f.put(ccx + dx, ccy + 5, INK)
        elif extra == "sheet":
            draw_sheet(f, cx, body_y)
        elif extra == "anger":  # a pink cross mark by the temple
            ax, ay = cx + head_r + 3, head_y - head_r
            for dx, dy in ((0, 0), (2, 0), (1, 1), (0, 2), (2, 2)):
                f.put(ax + dx, ay + dy, BLUSH)
        elif extra in ("stars", "stars2"):  # little gold stars circling above the head
            ring = [(-6, -1), (0, -4), (6, -1)] if extra == "stars" else [(-4, -3), (3, -4), (6, 0)]
            for dx, dy in ring:
                f.put(cx + dx, top + dy, STAR)
                f.put(cx + dx + 1, top + dy, STAR)
                f.put(cx + dx, top + dy + 1, STAR)
                f.put(cx + dx + 1, top + dy + 1, STAR)


def draw_sheep(f, cx, body_y, rx, ry, head_y, head_r, blink=False, lying=False, big_horns=False,
               head_dx=0, head_dy=0, eyes=None, mouth=None, puff=False, wave=None, extras=()):
    """head_dx/head_dy move only the head; eyes is open/blink/happy/squint/frown/angry/dizzy/sad;
    mouth is open/chew/sad. wave=0/1 lifts the front leg beside the body (two positions of a wave).
    extras adds sweat, cloud, sheet, anger or stars (see draw_extras)."""
    if eyes is None:
        eyes = "blink" if blink else "open"
    if not lying:
        legs = [cx - rx + 3, cx - rx // 3, cx + rx // 3 - 1, cx + rx - 4]
        if wave is not None:
            legs.pop()  # the front leg is up in the air
        for hx in legs:
            for y in range(body_y + ry - 3, body_y + ry + 2):
                f.put(hx, y, FACE)
                f.put(hx + 1, y, FACE)
    f.ellipse(cx, body_y, rx, ry, WOOL_MID, OUTLINE, wool_shade(rx, ry))
    if wave is not None:
        # a 2 px wide leg from the side of the body: out at 45 degrees (0), or steeper (1)
        x0, y0 = cx + rx - 1, body_y - 1
        for i in range(6):
            x = x0 + (i if wave == 0 else 2 + i // 3)
            f.put(x, y0 - i, FACE)
            f.put(x + 1, y0 - i, FACE)
    if "sheet" in extras:
        draw_sheet(f, cx, body_y)
    cx += head_dx  # everything from here on is the head
    head_y += head_dy
    # curled horns
    curl = 5 if big_horns else 4
    for i in range(curl):
        f.put(cx - head_r - 1 + i // 2, head_y - 6 + i, HORN)
        f.put(cx - head_r + i // 2, head_y - 6 + i, HORN_DARK)
        f.put(cx + head_r + 1 - i // 2, head_y - 6 + i, HORN)
        f.put(cx + head_r - i // 2, head_y - 6 + i, HORN_DARK)
    f.ellipse(cx, head_y, head_r, head_r - 1, FACE)
    f.put(cx - head_r + 3, head_y - head_r + 3, FACE_HI)
    draw_eyes(f, cx, head_y, eyes)
    f.put(cx - 6, head_y + 3, BLUSH)
    f.put(cx + 6, head_y + 3, BLUSH)
    if mouth == "open":
        for x in (cx - 1, cx):
            f.put(x, head_y + 3, BLUSH)
            f.put(x, head_y + 4, BLUSH)
    elif mouth == "chew":  # closed mouth and a couple of rice crumbs
        f.put(cx - 1, head_y + 4, FACE_HI)
        f.put(cx, head_y + 4, FACE_HI)
        f.put(cx + 3, head_y + head_r, EYE)
        f.put(cx - 3, head_y + head_r + 1, EYE)
    elif mouth == "sad":  # corners down
        f.put(cx - 2, head_y + 5, BLUSH)
        for x in (cx - 1, cx, cx + 1):
            f.put(x, head_y + 4, BLUSH)
        f.put(cx + 2, head_y + 5, BLUSH)
    if puff:  # the little "pu" snort cloud beside the nose
        f.ellipse(cx + head_r + 5, head_y + 3, 3, 2, EYE, OUTLINE)
        f.put(cx + head_r + 9, head_y, EYE)
    # eggshell cap on the head, zigzag lower edge and an ink crack
    top = head_y - head_r - 4
    for x in range(cx - 5, cx + 6):
        bottom = top + 4 - (0 if x % 2 else 1)
        for y in range(top, bottom + 1):
            edge = y == top or x in (cx - 5, cx + 5)
            f.put(x, y, SHELL_DARK if edge else SHELL)
    f.put(cx - 1, top + 2, INK)
    f.put(cx, top + 3, INK)
    f.put(cx + 1, top + 2, INK)
    draw_extras(f, cx, head_y, head_r, body_y, [e for e in extras if e != "sheet"])


def egg_shade(rx, ry):
    def shade(dx, dy):
        if dx > rx // 3 or dy > ry // 2:
            return SHELL_DARK
        return SHELL
    return shade


def draw_egg(f, tilt=0, crack=1, shimmer=False):
    cx, cy, rx, ry = 24, 27, 13, 17
    base = Frame()
    base.ellipse(cx, cy, rx, ry, SHELL, OUTLINE, egg_shade(rx, ry))
    # passport-ink crack, longer as the egg gets ready to hatch
    path = [(-4, -6), (-3, -5), (-2, -6), (-1, -5), (0, -6), (1, -5), (2, -6), (3, -5), (4, -6)]
    n = {0: 3, 1: 5, 2: 9}[crack]
    for dx, dy in path[:n]:
        base.put(cx + dx, cy + dy, INK)
    if shimmer:
        base.put(cx - 6, cy - 9, EYE)
        base.put(cx - 7, cy - 8, EYE)
    # tilt by shifting each row sideways around the bottom of the egg
    for y in range(W):
        shift = 0
        if tilt:
            shift = int(round((cy + ry - y) / 10.0)) * tilt
        for x in range(W):
            slot = base.px[y][x]
            if slot != CLEAR:
                f.put(x + shift, y, slot)


def draw_hatch(f, step):
    if step == 0:
        draw_egg(f, crack=2)
        return
    cx, cy, rx, ry = 24, 27, 13, 17
    lower = Frame()
    lower.ellipse(cx, cy, rx, ry, SHELL, OUTLINE, egg_shade(rx, ry))
    split = cy - 2 if step == 1 else cy + 4
    if step == 2:
        draw_sheep(f, 24, 32, 12, 9, 22, 6)
    for y in range(W):
        for x in range(W):
            slot = lower.px[y][x]
            if slot == CLEAR:
                continue
            if y >= split:
                f.put(x, y, slot)
            elif step == 1:
                f.put(x, y - 3, slot)  # the top half lifts off
    for x in range(cx - rx + 1, cx + rx):
        f.put(x, split + (0 if x % 2 else -1), INK)


def child(f, blink=False, dx=0, dy=0, **face):
    draw_sheep(f, 24 + dx, 30 + dy, 15, 11, 19 + dy, 7, blink=blink, **face)


def child_hungry(f, breath):
    draw_sheep(f, 24, 35 - breath, 16, 7 + breath, 28, 7, blink=True, lying=True)


def child_lying(f, **face):
    draw_sheep(f, 24, 35, 16, 7, 28, 7, lying=True, **face)


def adult(f, blink=False, dx=0, dy=0, **face):
    draw_sheep(f, 24 + dx, 29 + dy, 18, 13, 16 + dy, 8, blink=blink, big_horns=True, **face)


def adult_hungry(f, breath):
    draw_sheep(f, 24, 34 - breath, 19, 8 + breath, 26, 8, blink=True, lying=True, big_horns=True)


def adult_lying(f, **face):
    draw_sheep(f, 24, 34, 19, 8, 26, 8, lying=True, big_horns=True, **face)


# Reactions shared by the child and the adult: eat (head bobs while chewing), happy
# (hop, ^ ^ eyes and a snort cloud), dodge (leans away squinting), shake (head left/right).
def reaction_frames(body):
    return [
        ("eat", [lambda f: body(f, dy=1, mouth="open"), lambda f: body(f, mouth="chew")]),
        ("happy", [lambda f: body(f, dy=-2, eyes="happy"), lambda f: body(f, eyes="happy", puff=True)]),
        ("dodge", [lambda f: body(f, dx=-3, head_dx=-1, eyes="squint"),
                   lambda f: body(f, dx=-5, head_dx=-2, eyes="squint")]),
        ("shake", [lambda f: body(f, head_dx=-2, eyes="blink"), lambda f: body(f, head_dx=2, eyes="blink")]),
    ]


# v2 reactions (M7b): annoyed (frown, head tilted), angry (hops away with an anger mark),
# work (holds a spreadsheet and sweats), dizzy (knocked flat, stars circling), emo (droops
# under a little storm cloud).
def v2_frames(body, lying):
    return [
        ("annoyed", [lambda f: body(f, head_dx=2, eyes="frown"),
                     lambda f: body(f, head_dx=3, eyes="frown")]),
        ("angry", [lambda f: body(f, dx=3, dy=-3, head_dx=1, eyes="angry", extras=("anger",)),
                   lambda f: body(f, dx=5, head_dx=2, eyes="angry", extras=("anger",))]),
        ("work", [lambda f: body(f, eyes="open", extras=("sheet", "sweat")),
                  lambda f: body(f, eyes="blink", extras=("sheet", "sweat2"))]),
        ("dizzy", [lambda f: lying(f, eyes="dizzy", extras=("stars",)),
                   lambda f: lying(f, eyes="dizzy", extras=("stars2",))]),
        ("emo", [lambda f: body(f, head_dy=2, eyes="sad", mouth="sad", extras=("cloud",)),
                 lambda f: body(f, head_dy=3, eyes="sad", mouth="sad", extras=("cloud2",))]),
    ]


ANIMS = [
    ("egg_idle", [lambda f: draw_egg(f, crack=0), lambda f: draw_egg(f, crack=0, shimmer=True)]),
    ("egg_wobble", [lambda f: draw_egg(f, tilt=-1, crack=1), lambda f: draw_egg(f, tilt=1, crack=1)]),
    ("egg_hatch", [lambda f: draw_hatch(f, 0), lambda f: draw_hatch(f, 1), lambda f: draw_hatch(f, 2)]),
    ("child_idle", [lambda f: child(f), lambda f: child(f, blink=True)]),
    ("child_hungry", [lambda f: child_hungry(f, 0), lambda f: child_hungry(f, 1)]),
    ("adult_idle", [lambda f: adult(f), lambda f: adult(f, blink=True)]),
]
ANIMS += [("child_" + name, frames) for name, frames in reaction_frames(child)]
ANIMS += [("adult_hungry", [lambda f: adult_hungry(f, 0), lambda f: adult_hungry(f, 1)])]
ANIMS += [("adult_" + name, frames) for name, frames in reaction_frames(adult)]
# Sync (M6): wave a front leg with ^ ^ eyes. Appended last so earlier frames keep their bytes.
ANIMS += [
    ("child_wave", [lambda f: child(f, eyes="happy", wave=0), lambda f: child(f, eyes="happy", wave=1)]),
    ("adult_wave", [lambda f: adult(f, eyes="happy", wave=0), lambda f: adult(f, eyes="happy", wave=1)]),
]
# v2 (M7b): appended after everything above for the same reason.
ANIMS += [("child_" + name, frames) for name, frames in v2_frames(child, child_lying)]
ANIMS += [("adult_" + name, frames) for name, frames in v2_frames(adult, adult_lying)]


# ---- Icons: plain RGBA at their own size, any colour (the palette has no skin tone, green
# or wolf grey). Shown 1:1 as RGB565A8 and never tinted.

def outline_image(image, colour):
    """1 px outline around every opaque pixel that touches transparency (aliased, no blending)."""
    size = image.size[0]
    px = image.load()
    edge = []
    for y in range(size):
        for x in range(size):
            if px[x, y][3] == 0:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if not (0 <= nx < size and 0 <= ny < size) or px[nx, ny][3] == 0:
                    edge.append((x, y))
                    break
    for x, y in edge:
        px[x, y] = colour + (255,)


def paint_hand(size, palette):
    """A cartoon hand seen from above, fingers pointing down: it drops onto Pupu's head."""
    skin, dark, light = (246, 211, 179), palette[OUTLINE], (255, 241, 227)
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    d.rounded_rectangle((10, 4, 38, 26), radius=9, fill=skin + (255,))  # palm
    for x0 in (8, 16, 24, 32):  # four fingers, a 1 px slit between them
        d.rounded_rectangle((x0, 18, x0 + 6, 42), radius=3, fill=skin + (255,))
    d.rounded_rectangle((36, 10, 46, 30), radius=4, fill=skin + (255,))  # thumb to the right
    outline_image(image, dark)
    px = image.load()
    for x0 in (8, 16, 24, 32):  # knuckle creases and a palm highlight
        for x in range(x0 + 2, x0 + 5):
            px[x, 30] = dark + (255,)
    for x in range(14, 20):
        px[x, 9] = light + (255,)
    return image


def paint_sheet(size, palette):
    """A spreadsheet: green header row, white cells, light grid, dark border, folded corner."""
    green, white, grid, dark = (29, 111, 66), (255, 255, 255), (200, 207, 200), palette[OUTLINE]
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    d.rectangle((5, 3, 42, 44), fill=white + (255,))
    d.rectangle((5, 3, 42, 11), fill=green + (255,))
    px = image.load()
    for x in range(5, 43):
        for y in range(12, 45):
            if (x - 5) % 9 == 0 or (y - 12) % 8 == 0:
                px[x, y] = grid + (255,)
    for x in range(5, 43):  # header cell dividers
        if (x - 5) % 9 == 0:
            for y in range(3, 12):
                px[x, y] = white + (255,)
    for i in range(6):  # folded corner, bottom right
        for j in range(6 - i):
            px[42 - i, 44 - j] = (0, 0, 0, 0)
    outline_image(image, dark)
    # a little "x" like a spreadsheet logo in the header
    for i in range(5):
        px[12 + i, 5 + i] = white + (255,)
        px[16 - i, 5 + i] = white + (255,)
    return image


def paint_wolf(size, palette):
    """A grey wolf head facing left: pointy ears, long snout, yellow eye, white teeth."""
    grey, dark_grey, dark = (112, 118, 140), (84, 88, 104), palette[OUTLINE]
    eye, teeth, nose = (255, 196, 46), (255, 255, 255), (30, 27, 26)
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    d.polygon([(12, 4), (20, 18), (6, 18)], fill=grey + (255,))    # left ear
    d.polygon([(34, 4), (42, 18), (26, 18)], fill=grey + (255,))   # right ear
    d.ellipse((6, 12, 42, 40), fill=grey + (255,))                 # head
    d.polygon([(4, 30), (24, 26), (24, 42), (8, 44)], fill=dark_grey + (255,))  # snout, to the left
    outline_image(image, dark)
    px = image.load()
    for x in range(5, 9):  # nose tip
        for y in range(30, 34):
            px[x, y] = nose + (255,)
    for x in range(10, 22, 4):  # teeth along the jaw
        px[x, 40] = teeth + (255,)
        px[x, 41] = teeth + (255,)
        px[x + 1, 40] = teeth + (255,)
    d2 = ImageDraw.Draw(image)
    d2.ellipse((27, 20, 33, 26), fill=eye + (255,))  # eye
    px[30, 23] = dark + (255,)
    px[31, 23] = dark + (255,)
    return image


ICONS = [
    ("icon_hand", 48, paint_hand),
    ("icon_sheet", 48, paint_sheet),
    ("icon_wolf", 48, paint_wolf),
]


def main():
    palette = read_palette(ART_DIR / "pupu.gpl")
    for name, size, paint in ICONS:
        image = paint(size, palette)
        out = ART_DIR / "{}.png".format(name)
        image.save(out, optimize=True)
        print("wrote {} ({} x {})".format(out.relative_to(ROOT), size, size))
    for name, painters in ANIMS:
        strip = Image.new("RGBA", (W * len(painters), W), (0, 0, 0, 0))
        pixels = strip.load()
        for i, paint in enumerate(painters):
            frame = Frame()
            paint(frame)
            for y in range(W):
                for x in range(W):
                    slot = frame.px[y][x]
                    if slot != CLEAR:
                        r, g, b = palette[slot]
                        pixels[i * W + x, y] = (r, g, b, 255)
        out = ART_DIR / "{}.png".format(name)
        strip.save(out, optimize=True)
        print("wrote {} ({} frames)".format(out.relative_to(ROOT), len(painters)))


if __name__ == "__main__":
    main()
