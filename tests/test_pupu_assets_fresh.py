#!/usr/bin/env python3
"""Fail when the Pupu art or palette changed but main/pupu_assets.{c,h} were not regenerated.

tools/gen_pupu_assets.py writes the SHA-256 of every input into main/pupu_assets.h.
This test needs only the Python standard library, so it runs without Pillow.
"""

import hashlib
import re
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "main" / "pupu_assets.h"
ART_DIR = ROOT / "assets" / "images" / "pupu"
HASH_RE = re.compile(r"^// sha256 ([0-9a-f]{64})  (\S+)$")


def recorded_hashes(header_text):
    out = {}
    for line in header_text.splitlines():
        match = HASH_RE.match(line)
        if match:
            out[match.group(2)] = match.group(1)
    return out


def problems(header_text, root):
    """Return a list of human-readable reasons the generated files are stale."""
    recorded = recorded_hashes(header_text)
    found = []
    expected = {"tools/gen_pupu_assets.py", "assets/images/pupu/pupu.gpl"}
    art = root / "assets" / "images" / "pupu"
    expected.update(p.relative_to(root).as_posix() for p in art.glob("*.png"))
    for path in sorted(expected - set(recorded)):
        found.append("{} is not in the generated header".format(path))
    for path, digest in sorted(recorded.items()):
        source = root / path
        if not source.exists():
            found.append("{} is listed but missing".format(path))
        elif hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            found.append("{} changed since the C files were generated".format(path))
    return found


class PupuAssetsFreshTest(unittest.TestCase):
    def test_generated_files_match_sources(self):
        reasons = problems(HEADER.read_text(encoding="utf-8"), ROOT)
        self.assertEqual(
            reasons, [],
            "rerun: /usr/bin/python3 tools/gen_pupu_assets.py\n" + "\n".join(reasons))

    def test_generated_c_file_exists(self):
        self.assertTrue((ROOT / "main" / "pupu_assets.c").is_file())

    def test_detects_changed_and_unlisted_art(self):
        # Negative cases, so the check above cannot pass unconditionally.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            art = root / "assets" / "images" / "pupu"
            art.mkdir(parents=True)
            (root / "tools").mkdir()
            files = {
                "tools/gen_pupu_assets.py": b"gen",
                "assets/images/pupu/pupu.gpl": b"palette",
                "assets/images/pupu/egg_idle.png": b"egg",
            }
            lines = []
            for rel, data in files.items():
                (root / rel).write_bytes(data)
                lines.append("// sha256 {}  {}".format(hashlib.sha256(data).hexdigest(), rel))
            header = "\n".join(lines)
            self.assertEqual(problems(header, root), [])

            (art / "egg_idle.png").write_bytes(b"egg, repainted")
            self.assertEqual(problems(header, root),
                             ["assets/images/pupu/egg_idle.png changed since the C files were generated"])

            (art / "egg_idle.png").write_bytes(b"egg")
            (art / "new_anim.png").write_bytes(b"new")
            self.assertEqual(problems(header, root),
                             ["assets/images/pupu/new_anim.png is not in the generated header"])


if __name__ == "__main__":
    unittest.main()
