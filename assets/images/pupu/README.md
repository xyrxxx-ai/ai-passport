<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pupu Pixel Art

Source art for the Pupu pet application. The firmware does not read these files
directly; `tools/gen_pupu_assets.py` converts them into `main/pupu_assets.c` and
`main/pupu_assets.h`, which are committed so a build does not need Pillow.

## Files

| File | Format | Use |
| --- | --- | --- |
| `pupu.gpl` | GIMP palette, 16 colours | The only colours any frame may use. Each slot has a fixed job (see below). |
| `egg_idle.png` | 96 × 48 PNG RGBA, 2 frames | Egg resting |
| `egg_wobble.png` | 96 × 48 PNG RGBA, 2 frames | Egg wobbling while the player warms it |
| `egg_hatch.png` | 144 × 48 PNG RGBA, 3 frames | Hatching |
| `child_idle.png` | 96 × 48 PNG RGBA, 2 frames | Young Pupu standing and blinking |
| `child_hungry.png` | 96 × 48 PNG RGBA, 2 frames | Young Pupu lying down when very hungry |
| `adult_idle.png` | 96 × 48 PNG RGBA, 2 frames | Adult Pupu standing and blinking |

Each strip is 48 px high with one 48 × 48 frame after another from left to right.
The current strips are placeholders drawn by `tools/make_placeholder_art.py`.
Source: generated for this repository; no third-party material.

## Palette slots

| Slot | Job | Slot | Job |
| --- | --- | --- | --- |
| 0 | transparent | 8 | wool light |
| 1 | outline | 9 | wool mid |
| 2 | face and hooves | 10 | wool shadow |
| 3 | face highlight | 11 | eggshell |
| 4 | eye white | 12 | eggshell shade |
| 5 | blush | 13 | passport ink |
| 6 | horn | 14 | spare |
| 7 | horn shadow | 15 | spare |

The firmware blends slots 8–10 toward grey in 8 steps as the lower of fullness and
happiness drops, so paint all wool with these three slots and nothing else with them.
Slot numbers are also defined in `main/pupu_sprite_math.h`.

## Replacing the art

1. In Piskel (free, runs in the browser), import `pupu.gpl` as the palette and draw
   on a 48 × 48 canvas. Keep the eggshell cap, the curled horns, and the round body.
2. Export each animation as a horizontal sprite-sheet PNG with the same file name.
   Use fully transparent pixels for the background; half-transparent pixels are rejected.
3. If an animation gets a different number of frames, update its frame durations in
   `ANIMS` at the top of `tools/gen_pupu_assets.py`.
4. Regenerate and check:

   ```bash
   /usr/bin/python3 tools/gen_pupu_assets.py
   ```

   The script stops with the file name and x,y of any colour that is not in the palette.
   It also writes `build/pupu-preview.png` (3× scale, every wool tint step) to review
   before flashing. `tests/test_pupu_assets_fresh.py` fails if the art changed but
   the C files were not regenerated.

On the device each frame is enlarged 3× into a 144 × 144 indexed (I4) canvas and drawn
at 1:1. Pixel data stays in Flash: 1,152 bytes per frame.
