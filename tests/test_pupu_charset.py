#!/usr/bin/env python3
"""Every non-ASCII character in a Pupu string literal must be in charset.txt and in the font.

Scans the string literals (not comments, and not ESP_LOGx(...) arguments, which only go to
the serial log) of main/pupu_*.c and main/pupu_*.h, then checks them against
assets/fonts/pupu/charset.txt and the glyph list that lv_font_conv writes into
main/fonts/pupu_font_16.c. Standard library only.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHARSET = ROOT / "assets" / "fonts" / "pupu" / "charset.txt"
FONT = ROOT / "main" / "fonts" / "pupu_font_16.c"
GLYPH_RE = re.compile(r"/\* U\+([0-9A-F]{4,6}) ")


def skip_quoted(source, i):
    """i points at a quote; return the index just past the matching closing quote."""
    quote, j, n = source[i], i + 1, len(source)
    while j < n and source[j] != quote:
        j += 2 if source[j] == "\\" else 1
    return j + 1


def skip_call(source, i):
    """i points at a macro name; return the index just past its balanced (...) arguments."""
    j = source.find("(", i)
    if j < 0:
        return len(source)
    depth = 0
    while j < len(source):
        c = source[j]
        if c == '"' or c == "'":
            j = skip_quoted(source, j)
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return j + 1
        j += 1
    return j


def starts_word(source, i, word):
    before = source[i - 1] if i > 0 else " "
    return source.startswith(word, i) and not (before.isalnum() or before == "_")


def string_literals(source):
    """Return the contents of C string literals, skipping comments, char literals and logs."""
    out = []
    i, n = 0, len(source)
    while i < n:
        c = source[i]
        if starts_word(source, i, "ESP_LOG"):
            i = skip_call(source, i)
        elif source.startswith("//", i):
            end = source.find("\n", i)
            i = n if end < 0 else end
        elif source.startswith("/*", i):
            end = source.find("*/", i + 2)
            i = n if end < 0 else end + 2
        elif c == '"' or c == "'":
            j = skip_quoted(source, i)
            if c == '"':
                out.append(source[i + 1:j - 1])
            i = j
        else:
            i += 1
    return out


def used_characters(sources):
    used = {}
    for path, text in sources:
        for literal in string_literals(text):
            for ch in literal:
                if ord(ch) > 0x7E:
                    used.setdefault(ch, path)
    return used


def font_codepoints(font_text):
    return {int(m.group(1), 16) for m in GLYPH_RE.finditer(font_text)}


def missing(sources, charset_text, font_text):
    """Return (char, where, problem) for each character the font chain cannot show."""
    charset = {ch for ch in charset_text if not ch.isspace()}
    glyphs = font_codepoints(font_text)
    problems = []
    for ch, where in sorted(used_characters(sources).items()):
        if ch not in charset:
            problems.append((ch, where, "not in charset.txt"))
        if ord(ch) not in glyphs:
            problems.append((ch, where, "not in pupu_font_16.c"))
    return problems


def pupu_sources():
    paths = sorted((ROOT / "main").glob("pupu_*.[ch]"))
    return [(p.relative_to(ROOT).as_posix(), p.read_text(encoding="utf-8")) for p in paths]


class PupuCharsetTest(unittest.TestCase):
    def test_every_ui_character_is_covered(self):
        problems = missing(pupu_sources(), CHARSET.read_text(encoding="utf-8"),
                           FONT.read_text(encoding="utf-8"))
        self.assertEqual(problems, [], "\n".join(
            "U+{:04X} {} ({}): {}".format(ord(ch), ch, where, why) for ch, where, why in problems))

    def test_font_has_ascii_and_the_whole_charset(self):
        glyphs = font_codepoints(FONT.read_text(encoding="utf-8"))
        for code in range(0x20, 0x7F):
            self.assertIn(code, glyphs, "U+{:04X}".format(code))
        for ch in CHARSET.read_text(encoding="utf-8"):
            if not ch.isspace():
                self.assertIn(ord(ch), glyphs, "U+{:04X} {} from charset.txt".format(ord(ch), ch))

    def test_ui_text_is_actually_scanned(self):
        # The UI strings live in main/pupu_text.h; make sure the scan reaches them.
        used = used_characters(pupu_sources())
        self.assertIn("孵", used)  # the first character of "hatch"

    def test_reports_a_missing_character(self):
        # Negative case: U+9F98 is deliberately absent, so the check cannot pass unconditionally.
        source = [("main/pupu_fake.c", 'const char *s = "孵龘"; // 龜 in a comment')]
        charset = "孵"
        font = '/* U+5B75 "孵" */'
        self.assertEqual(missing(source, charset, font), [
            ("龘", "main/pupu_fake.c", "not in charset.txt"),
            ("龘", "main/pupu_fake.c", "not in pupu_font_16.c"),
        ])

    def test_comments_and_escapes_are_handled(self):
        text = '/* "一" */ a = "x\\"二"; b = \'"\'; // "三"\nc = "四";'
        self.assertEqual(string_literals(text), ['x\\"二', "四"])

    def test_log_messages_are_not_ui_text(self):
        # Serial-log text never reaches the screen, so it does not need glyphs.
        text = 'ESP_LOGI(TAG, "一 %s (%d)", f("二"), (1)); x = "三"; MY_ESP_LOG("四");'
        self.assertEqual(string_literals(text), ["三", "四"])


if __name__ == "__main__":
    unittest.main()
