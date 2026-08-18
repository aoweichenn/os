import hashlib
from pathlib import Path
import re
import unittest


OS_TEST_VGA_FONT_SOURCE = (
    Path(__file__).resolve().parents[2]
    / "source"
    / "firmware"
    / "include"
    / "font8x8_basic.inc"
)
OS_TEST_VGA_FONT_GLYPH_COUNT = 96
OS_TEST_VGA_FONT_ROWS_PER_GLYPH = 8
OS_TEST_VGA_FONT_SHA256 = (
    "3083fc9dacd57f0ed5dbe1efc984cdbd39ae61fdf9e58bda4f317b370f4f04f6"
)


class VgaFontToolTests(unittest.TestCase):
    def testFontHasPinnedBasicLatinPayload(self) -> None:
        source = OS_TEST_VGA_FONT_SOURCE.read_text(encoding="utf-8")
        values = bytes(
            int(match, 16)
            for match in re.findall(r"\b0x([0-9A-Fa-f]{2})\b", source)
        )
        self.assertEqual(
            len(values),
            OS_TEST_VGA_FONT_GLYPH_COUNT * OS_TEST_VGA_FONT_ROWS_PER_GLYPH,
        )
        self.assertEqual(hashlib.sha256(values).hexdigest(), OS_TEST_VGA_FONT_SHA256)


if __name__ == "__main__":
    unittest.main()
