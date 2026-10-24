#!/usr/bin/env python3
"""Draw placeholder Pupu frames with simple shapes so development does not wait for art.

Writes one horizontal strip per animation (48 px per frame) into assets/images/pupu/,
using only colours from assets/images/pupu/pupu.gpl. Replace any strip with real
pixel art later (for example from Piskel) and rerun tools/gen_pupu_assets.py.

Runs on the system Python 3.9 with Pillow: /usr/bin/python3 tools/make_placeholder_art.py
"""

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ART_DIR = ROOT / "assets" / "images" / "pupu"
W = 48

CLEAR, OUTLINE, FACE, FACE_HI, EYE, BLUSH, HORN, HORN_DARK = range(8)
WOOL_LIGHT, WOOL_MID, WOOL_DARK, SHELL, SHELL_DARK, INK = range(8, 14)


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
    def __init__(self):
        self.px = [[CLEAR] * W for _ in range(W)]

    def put(self, x, y, slot):
        if 0 <= x < W and 0 <= y < W:
            self.px[y][x] = slot

    def ellipse(self, cx, cy, rx, ry, fill, outline=None, shade=None):
        """Filled ellipse; optional 1 px outline and a lower shadow band."""
        for y in range(W):
            for x in range(W):
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


def draw_sheep(f, cx, body_y, rx, ry, head_y, head_r, blink=False, lying=False, big_horns=False):
    if not lying:
        for hx in (cx - rx + 3, cx - rx // 3, cx + rx // 3 - 1, cx + rx - 4):
            for y in range(body_y + ry - 3, body_y + ry + 2):
                f.put(hx, y, FACE)
                f.put(hx + 1, y, FACE)
    f.ellipse(cx, body_y, rx, ry, WOOL_MID, OUTLINE, wool_shade(rx, ry))
    # curled horns
    curl = 5 if big_horns else 4
    for i in range(curl):
        f.put(cx - head_r - 1 + i // 2, head_y - 6 + i, HORN)
        f.put(cx - head_r + i // 2, head_y - 6 + i, HORN_DARK)
        f.put(cx + head_r + 1 - i // 2, head_y - 6 + i, HORN)
        f.put(cx + head_r - i // 2, head_y - 6 + i, HORN_DARK)
    f.ellipse(cx, head_y, head_r, head_r - 1, FACE)
    f.put(cx - head_r + 3, head_y - head_r + 3, FACE_HI)
    for ex in (cx - 4, cx + 3):
        if blink:
            f.put(ex, head_y + 1, EYE)
            f.put(ex + 1, head_y + 1, EYE)
        else:
            f.put(ex, head_y, EYE)
            f.put(ex + 1, head_y, EYE)
            f.put(ex, head_y + 1, EYE)
    f.put(cx - 6, head_y + 3, BLUSH)
    f.put(cx + 6, head_y + 3, BLUSH)
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


def child(f, blink=False):
    draw_sheep(f, 24, 30, 15, 11, 19, 7, blink=blink)


def child_hungry(f, breath):
    draw_sheep(f, 24, 35 - breath, 16, 7 + breath, 28, 7, blink=True, lying=True)


def adult(f, blink=False):
    draw_sheep(f, 24, 29, 18, 13, 16, 8, blink=blink, big_horns=True)


ANIMS = [
    ("egg_idle", [lambda f: draw_egg(f, crack=0), lambda f: draw_egg(f, crack=0, shimmer=True)]),
    ("egg_wobble", [lambda f: draw_egg(f, tilt=-1, crack=1), lambda f: draw_egg(f, tilt=1, crack=1)]),
    ("egg_hatch", [lambda f: draw_hatch(f, 0), lambda f: draw_hatch(f, 1), lambda f: draw_hatch(f, 2)]),
    ("child_idle", [lambda f: child(f), lambda f: child(f, blink=True)]),
    ("child_hungry", [lambda f: child_hungry(f, 0), lambda f: child_hungry(f, 1)]),
    ("adult_idle", [lambda f: adult(f), lambda f: adult(f, blink=True)]),
]


def main():
    palette = read_palette(ART_DIR / "pupu.gpl")
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
