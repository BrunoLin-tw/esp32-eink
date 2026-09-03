import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock

from tools import gen_fonts

EXPECTED_FONT_SHA = "faa5f3656a78b2e2d450d27fe8382c778bc2b6bb5ea29c986664a6a435056ceb"
PINNED_FONT = pathlib.Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc")

class FontContractTest(unittest.TestCase):
    def test_manifest_contains_v2_glyphs(self):
        self.assertTrue(set("部分/").issubset(set(gen_fonts.G16)))
        self.assertTrue(set("中興華金鋼").issubset(set(gen_fonts.G20)))

    def test_validate_environment_accepts_exact_inputs(self):
        self.assertEqual(
            gen_fonts.validate_environment(EXPECTED_FONT_SHA, "12.3.0", "2.37.1"),
            None,
        )

    def test_rejects_wrong_inputs(self):
        with self.assertRaises(SystemExit):
            gen_fonts.validate_environment("bad", "12.3.0", "2.37.1")
        with self.assertRaises(SystemExit):
            gen_fonts.validate_environment(EXPECTED_FONT_SHA, "12.2.0", "2.37.1")
        with self.assertRaises(SystemExit):
            gen_fonts.validate_environment(EXPECTED_FONT_SHA, "12.3.0", "abc123")
        with self.assertRaises(SystemExit):
            gen_fonts.validate_environment(EXPECTED_FONT_SHA, "12.3.0", "2.37.1-dirty")

    def test_banner_is_path_independent(self):
        a = gen_fonts.output_banner(EXPECTED_FONT_SHA)
        b = gen_fonts.output_banner(EXPECTED_FONT_SHA)
        self.assertEqual(a, b)
        self.assertIn("font: NotoSansCJK-Bold.ttc", a)
        self.assertIn(EXPECTED_FONT_SHA, a)
        self.assertNotIn("/usr/share", a)

    @mock.patch("tools.gen_fonts.subprocess.run")
    def test_detect_u8g2_uses_exact_tag_and_dirty_check(self, run):
        run.return_value = subprocess.CompletedProcess([], 0, "2.37.1\n", "")
        self.assertEqual(gen_fonts.detect_u8g2_version(), "2.37.1")
        run.assert_called_once_with(
            ["git", "-C", gen_fonts.U8G2_DIR, "describe", "--tags",
             "--exact-match", "--dirty"],
            capture_output=True, text=True,
        )

    def _assert_generate_rejected_without_overwrite(self, font_path, pillow, rev):
        with tempfile.TemporaryDirectory() as tmp:
            out_c = pathlib.Path(tmp) / "fonts_quote.c"
            out_h = pathlib.Path(tmp) / "fonts_quote.h"
            out_c.write_bytes(b"sentinel-c")
            out_h.write_bytes(b"sentinel-h")
            with mock.patch.object(gen_fonts.PIL, "__version__", pillow), \
                 mock.patch.object(gen_fonts, "detect_u8g2_version", return_value=rev):
                with self.assertRaises(SystemExit):
                    gen_fonts.generate(str(font_path), str(out_c), str(out_h))
            self.assertEqual(out_c.read_bytes(), b"sentinel-c")
            self.assertEqual(out_h.read_bytes(), b"sentinel-h")

    def test_generate_rejects_wrong_sha_without_overwrite(self):
        with tempfile.NamedTemporaryFile() as bad_font:
            bad_font.write(b"not-the-pinned-font")
            bad_font.flush()
            self._assert_generate_rejected_without_overwrite(
                bad_font.name, "12.3.0", "2.37.1")

    def test_generate_rejects_wrong_pillow_without_overwrite(self):
        self._assert_generate_rejected_without_overwrite(
            PINNED_FONT, "12.2.0", "2.37.1")

    def test_generate_rejects_non_exact_tag_without_overwrite(self):
        self._assert_generate_rejected_without_overwrite(
            PINNED_FONT, "12.3.0", "abc123")

    def test_generate_rejects_dirty_tag_without_overwrite(self):
        self._assert_generate_rejected_without_overwrite(
            PINNED_FONT, "12.3.0", "2.37.1-dirty")
