import tempfile
import unittest
from pathlib import Path

from tools.import_webvtt_pack import build_pack, convert_webvtt, to_srt_timestamp


class WebVttConversionTests(unittest.TestCase):
    def test_timestamp_without_hours(self):
        self.assertEqual(to_srt_timestamp("00:04.360"), "00:00:04,360")

    def test_timestamp_with_hours(self):
        self.assertEqual(to_srt_timestamp("01:02:03.004"), "01:02:03,004")

    def test_cue_id_entities_and_markup(self):
        source = """WEBVTT

cue-1
00:00.850 --> 00:04.360 align:start
<v Lain><i>Ola &amp; mundo!</i></v>
"""
        self.assertEqual(
            convert_webvtt(source),
            "1\n00:00:00,850 --> 00:00:04,360\nOla & mundo!\n",
        )

    def test_multiple_cues_keep_order_and_line_breaks(self):
        source = """WEBVTT

00:01.000 --> 00:02.000
Primeira linha
Segunda linha

00:03.000 --> 00:04.000
Fim
"""
        self.assertEqual(
            convert_webvtt(source),
            "1\n00:00:01,000 --> 00:00:02,000\nPrimeira linha\nSegunda linha\n\n"
            "2\n00:00:03,000 --> 00:00:04,000\nFim\n",
        )

    def test_pack_uses_original_node_names(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source"
            output = root / "data" / "lang" / "pt-BR"
            source.mkdir()
            (source / "Cou001.vtt").write_text(
                "WEBVTT\n\n00:00.000 --> 00:01.000\nTeste\n", encoding="utf-8"
            )
            (source / "Lda001.vtt").write_text(
                "WEBVTT\n\n00:00.000 --> 00:01.000\nDiario\n", encoding="utf-8"
            )

            count, skipped = build_pack(source, output, "Português brasileiro")
            self.assertEqual((count, skipped), (2, 0))
            self.assertEqual(
                (output / "subtitles" / "index.txt").read_text(encoding="utf-8"),
                "Cou001.srt\nLda001.srt\n",
            )
            self.assertTrue((output / "subtitles" / "Cou001.srt").is_file())
            self.assertIn("name = Português brasileiro", (output / "pack.txt").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
