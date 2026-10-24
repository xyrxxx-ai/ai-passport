#!/usr/bin/env python3
"""Convert the Pupu pixel-art strips into C arrays for the firmware.

Inputs (assets/images/pupu/):
  pupu.gpl        16-colour palette; slot 0 is transparent
  <anim>.png      one horizontal strip per animation, 48 px per frame

Outputs:
  main/pupu_assets.h, main/pupu_assets.c   packed 4-bit frames, palette, animation table
  build/pupu-preview.png                    3x contact sheet with every tint step (not committed)

Every opaque pixel must exactly match one of palette slots 1-15 and every other pixel
must be fully transparent; otherwise the script stops and names the file and x,y.
The generated header records the SHA-256 of every input, so
tests/test_pupu_assets_fresh.py can tell when the art changed but the C files did not.

Runs on the system Python 3.9 with Pillow: /usr/bin/python3 tools/gen_pupu_assets.py
"""

import hashlib
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ART_DIR = ROOT / "assets" / "images" / "pupu"
OUT_H = ROOT / "main" / "pupu_assets.h"
OUT_C = ROOT / "main" / "pupu_assets.c"
PREVIEW = ROOT / "build" / "pupu-preview.png"
W = 48

# Animation order is the enum order. Each entry lists how long every frame stays on
# screen (ms); the strip must have exactly that many frames.
ANIMS = [
    ("egg_idle", [1500, 300]),
    ("egg_wobble", [120, 120]),
    ("egg_hatch", [400, 400, 700]),
    ("child_idle", [2800, 160]),
    ("child_hungry", [900, 900]),
    ("adult_idle", [3000, 160]),
]

# Must match the wool grey in main/pupu_sprite_math.c; used only for the preview.
WOOL_GREY = [(0xC4, 0xC2, 0xBE), (0xA3, 0xA1, 0x9D), (0x7F, 0x7D, 0x79)]
TINT_STEPS = 8


def fail(message):
    sys.exit("gen_pupu_assets: " + message)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_palette(path):
    colours = []
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) >= 3 and all(p.isdigit() for p in parts[:3]):
            rgb = tuple(int(p) for p in parts[:3])
            if any(c > 255 for c in rgb):
                fail("{}: colour out of range: {}".format(path.name, line.strip()))
            colours.append(rgb)
    if len(colours) != 16:
        fail("{}: expected 16 colours, found {}".format(path.name, len(colours)))
    if len(set(colours[1:])) != 15:
        fail("{}: slots 1-15 must all be different colours".format(path.name))
    return colours


def read_strip(path, palette, frame_count):
    lookup = {rgb: slot for slot, rgb in enumerate(palette) if slot != 0}
    image = Image.open(str(path)).convert("RGBA")
    width, height = image.size
    if height != W or width % W != 0:
        fail("{}: must be {} px high and a multiple of {} px wide, got {}x{}".format(
            path.name, W, W, width, height))
    if width // W != frame_count:
        fail("{}: has {} frames but ANIMS in tools/gen_pupu_assets.py lists {}; "
             "update the frame durations there".format(path.name, width // W, frame_count))
    pixels = image.load()
    frames = []
    for i in range(frame_count):
        slots = []
        for y in range(W):
            for x in range(W):
                r, g, b, a = pixels[i * W + x, y]
                if a == 0:
                    slots.append(0)
                elif a != 255:
                    fail("{}:{},{}: half-transparent pixel (alpha {}); use fully "
                         "transparent or fully opaque".format(path.name, i * W + x, y, a))
                elif (r, g, b) in lookup:
                    slots.append(lookup[(r, g, b)])
                else:
                    fail("{}:{},{}: colour #{:02X}{:02X}{:02X} is not in pupu.gpl".format(
                        path.name, i * W + x, y, r, g, b))
        frames.append(slots)
    return frames


def pack(slots):
    out = bytearray()
    for i in range(0, len(slots), 2):
        out.append((slots[i] << 4) | slots[i + 1])  # even x in the high nibble (LVGL I4)
    return bytes(out)


def tint(cream, grey, step):
    last = TINT_STEPS - 1
    return tuple((c * (last - step) + g * step + last // 2) // last for c, g in zip(cream, grey))


def write_preview(palette, anims):
    scale = 3
    cell = W * scale + 4
    max_frames = max(len(frames) for _, frames in anims)
    rows = len(anims) + TINT_STEPS
    sheet = Image.new("RGB", (cell * max(max_frames, 1), cell * rows), (255, 255, 255))
    px = sheet.load()
    for yy in range(sheet.size[1]):
        for xx in range(sheet.size[0]):
            if ((xx // 6) + (yy // 6)) % 2:
                px[xx, yy] = (225, 225, 225)  # checkerboard shows transparency

    def draw(frame, colours, ox, oy):
        for y in range(W):
            for x in range(W):
                slot = frame[y * W + x]
                if slot == 0:
                    continue
                for dy in range(scale):
                    for dx in range(scale):
                        px[ox + x * scale + dx, oy + y * scale + dy] = colours[slot]

    for row, (_, frames) in enumerate(anims):
        for col, frame in enumerate(frames):
            draw(frame, palette, col * cell + 2, row * cell + 2)
    # tint steps 0..7 using the first frame of the first non-egg animation
    sample = next(frames[0] for name, frames in anims if not name.startswith("egg"))
    for step in range(TINT_STEPS):
        colours = list(palette)
        for shade in range(3):
            colours[8 + shade] = tint(palette[8 + shade], WOOL_GREY[shade], step)
        draw(sample, colours, 2, (len(anims) + step) * cell + 2)
    PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(str(PREVIEW))


def c_name(name):
    return name.upper()


def main():
    palette_path = ART_DIR / "pupu.gpl"
    palette = read_palette(palette_path)
    sources = [palette_path, Path(__file__).resolve()]
    anims = []
    for name, durations in ANIMS:
        path = ART_DIR / "{}.png".format(name)
        if not path.exists():
            fail("missing {}".format(path.relative_to(ROOT)))
        anims.append((name, read_strip(path, palette, len(durations))))
        sources.append(path)
    listed = {"{}.png".format(name) for name, _ in ANIMS}
    for extra in sorted(ART_DIR.glob("*.png")):
        if extra.name not in listed:
            fail("{} is not listed in ANIMS; add it or remove the file".format(extra.name))

    hashes = ["// sha256 {}  {}".format(sha256(p), p.relative_to(ROOT).as_posix()) for p in sources]
    total = sum(len(frames) for _, frames in anims)

    h = []
    h.append("// main/pupu_assets.h —— generated by tools/gen_pupu_assets.py. Do not edit by hand.")
    h.append("//")
    h.append("// Rerun /usr/bin/python3 tools/gen_pupu_assets.py after changing anything below;")
    h.append("// tests/test_pupu_assets_fresh.py checks these hashes.")
    h.extend(hashes)
    h.append("#pragma once")
    h.append("")
    h.append("#include <stdint.h>")
    h.append("")
    h.append('#include "pupu_sprite_math.h"')
    h.append("")
    h.append("typedef enum {")
    for name, _ in anims:
        h.append("    PUPU_ANIM_{},".format(c_name(name)))
    h.append("    PUPU_ANIM_COUNT,")
    h.append("} pupu_anim_t;")
    h.append("")
    h.append("typedef enum {")
    for name, frames in anims:
        for i in range(len(frames)):
            h.append("    PUPU_FRAME_{}_{},".format(c_name(name), i))
    h.append("    PUPU_FRAME_COUNT,")
    h.append("} pupu_frame_t;")
    h.append("")
    h.append("typedef struct {")
    h.append("    uint16_t first;  // pupu_frame_t of the first frame")
    h.append("    uint8_t count;")
    h.append("} pupu_anim_info_t;")
    h.append("")
    h.append("extern const uint32_t pupu_asset_palette[PUPU_PALETTE_SIZE];  // 0xAARRGGBB")
    h.append("extern const pupu_anim_info_t pupu_anims[PUPU_ANIM_COUNT];")
    h.append("extern const uint16_t pupu_frame_ms[PUPU_FRAME_COUNT];")
    h.append("extern const uint8_t pupu_frames[PUPU_FRAME_COUNT][PUPU_SPRITE_FRAME_BYTES];")
    h.append("")

    c = []
    c.append("// main/pupu_assets.c —— generated by tools/gen_pupu_assets.py. Do not edit by hand.")
    c.append('#include "pupu_assets.h"')
    c.append("")
    c.append("const uint32_t pupu_asset_palette[PUPU_PALETTE_SIZE] = {")
    for slot, (r, g, b) in enumerate(palette):
        alpha = 0 if slot == 0 else 0xFF
        c.append("    0x{:02X}{:02X}{:02X}{:02X}u,".format(alpha, r, g, b))
    c.append("};")
    c.append("")
    c.append("const pupu_anim_info_t pupu_anims[PUPU_ANIM_COUNT] = {")
    for name, frames in anims:
        c.append("    [PUPU_ANIM_{}] = {{ PUPU_FRAME_{}_0, {} }},".format(c_name(name), c_name(name), len(frames)))
    c.append("};")
    c.append("")
    c.append("const uint16_t pupu_frame_ms[PUPU_FRAME_COUNT] = {")
    for name, durations in ANIMS:
        c.append("    {},".format(", ".join(str(d) for d in durations)))
    c.append("};")
    c.append("")
    c.append("const uint8_t pupu_frames[PUPU_FRAME_COUNT][PUPU_SPRITE_FRAME_BYTES] = {")
    for name, frames in anims:
        for i, frame in enumerate(frames):
            data = pack(frame)
            c.append("    [PUPU_FRAME_{}_{}] = {{".format(c_name(name), i))
            for row in range(0, len(data), 24):
                c.append("        " + " ".join("0x{:02x},".format(v) for v in data[row:row + 24]))
            c.append("    },")
    c.append("};")
    c.append("")

    OUT_H.write_text("\n".join(h), encoding="utf-8")
    OUT_C.write_text("\n".join(c), encoding="utf-8")
    write_preview(palette, anims)
    print("wrote {} and {}: {} animations, {} frames ({} bytes of pixels)".format(
        OUT_H.relative_to(ROOT), OUT_C.relative_to(ROOT), len(anims), total,
        total * W * W // 2))
    print("preview: {}".format(PREVIEW.relative_to(ROOT)))


if __name__ == "__main__":
    main()
