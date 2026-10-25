<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pupu Chinese Font Subset

The Pupu application shows a small amount of Chinese text. It uses a 16 px subset of
Noto Sans SC Regular that contains only printable ASCII and the characters listed in
`charset.txt`.

| Item | Value |
| --- | --- |
| Source font | Noto Sans SC Regular, static OTF (not the variable font) |
| Download | `https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf` (downloaded 2026-10-24, 8,331,336 bytes) |
| Source SHA-256 | `faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9` |
| License | SIL Open Font License 1.1, copied in `OFL.txt` |
| Converter | `lv_font_conv` 1.5.3 through `npx`, Node.js 22 |
| Output | `main/fonts/pupu_font_16.c` (committed), 16 px, 4 bpp, uncompressed |

The downloaded OTF goes in `src/`, which is ignored by Git; only the generated C file,
`charset.txt`, and `OFL.txt` are committed.

## Regenerating

Add any new UI character to `charset.txt` (UTF-8, one line, no spaces), then run from
the repository root:

```bash
npm_config_cache="$TMPDIR/npm-cache" npx --yes lv_font_conv@1.5.3 --font assets/fonts/pupu/src/NotoSansSC-Regular.otf --size 16 --bpp 4 --no-compress --format lvgl --range 0x20-0x7E --symbols "$(cat assets/fonts/pupu/charset.txt)" --lv-font-name pupu_font_16 --lv-include lvgl.h -o main/fonts/pupu_font_16.c
```

- Use Node.js 22. Node.js 24 has been reported to produce corrupted glyphs.
- `npm_config_cache` keeps the npm cache in a writable temporary directory. Without it,
  npm may suggest running `sudo chown`; do not use `sudo`.
- `tests/test_pupu_charset.py` checks that every non-ASCII character in the Pupu
  sources is in `charset.txt` and in the generated font.

The firmware gives this font a Montserrat 14 fallback so LVGL symbols such as the
battery icon still render. It was verified on the device on 2026-10-24.
