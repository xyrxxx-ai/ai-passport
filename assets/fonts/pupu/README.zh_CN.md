<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 噗噗中文字库子集

噗噗应用只显示少量中文。它用 Noto Sans SC Regular 生成了一个 16 px 的子集，里面只有可打印的
ASCII 字符和 `charset.txt` 里列出的字。

| 项目 | 值 |
| --- | --- |
| 原始字体 | Noto Sans SC Regular，静态 OTF（不是可变字体） |
| 下载地址 | `https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf`（2026-10-24 下载，8,331,336 字节） |
| 原始文件 SHA-256 | `faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9` |
| 许可证 | SIL Open Font License 1.1，原文见 `OFL.txt` |
| 转换工具 | `lv_font_conv` 1.5.3，用 `npx` 运行，Node.js 22 |
| 输出 | `main/fonts/pupu_font_16.c`（提交），16 px，4 bpp，不压缩 |

下载的 OTF 放在 `src/` 里，这个目录被 Git 忽略；只提交生成的 C 文件、`charset.txt` 和 `OFL.txt`。

## 重新生成

把界面新用到的字加进 `charset.txt`（UTF-8，一行，不要空格），然后在仓库根目录运行：

```bash
npm_config_cache="$TMPDIR/npm-cache" npx --yes lv_font_conv@1.5.3 --font assets/fonts/pupu/src/NotoSansSC-Regular.otf --size 16 --bpp 4 --no-compress --format lvgl --range 0x20-0x7E --symbols "$(cat assets/fonts/pupu/charset.txt)" --lv-font-name pupu_font_16 --lv-include lvgl.h -o main/fonts/pupu_font_16.c
```

- 用 Node.js 22。社区记录过 Node.js 24 会生成损坏的字形。
- `npm_config_cache` 让 npm 把缓存放到可写的临时目录。不加的话，npm 可能提示运行 `sudo chown`；
  不要用 `sudo`。
- `tests/test_pupu_charset.py` 检查噗噗源码里的每个非 ASCII 字符都在 `charset.txt` 和生成的字库里。

固件给这个字库设了 Montserrat 14 作为后备字体，所以电池图标这类 LVGL 符号也能显示。
2026-10-24 已在真机上确认显示正常。
