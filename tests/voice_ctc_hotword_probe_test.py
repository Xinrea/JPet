#!/usr/bin/env python3
"""Stdlib helper tests; optionally set JPET_CTC_PROBE to test compiled WAV reader."""
import importlib.util
import json
import os
import struct
import subprocess
import tempfile
import unittest
import wave
from pathlib import Path
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("probe", Path(__file__).with_name("voice_ctc_hotword_probe.py"))
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


class ReportTests(unittest.TestCase):
    def test_unicode_normalization(self):
        self.assertEqual(probe.normalize(" ＡＢＣ，轴伊！‘你好’。\n"), "abc轴伊你好")
        self.assertEqual(probe.normalize("Straße +１"), "strasse+1")

    def test_edit_distance(self):
        self.assertEqual(probe.edit_distance("轴伊你好", "周一你好"), 2)
        self.assertEqual(probe.edit_distance("", "轴伊"), 2)
        self.assertEqual(probe.edit_distance("abc", "ac"), 1)

    def test_hits_and_false_positives(self):
        hotwords = [{"text": "轴伊"}, {"text": "JPet"}]
        sample = {"expected": "轴伊你好", "label": "hotword"}
        row = probe.score_result(sample, {"text": "轴伊轴伊 JPET", "decode_ms": 8, "audio_seconds": 1}, hotwords)
        self.assertEqual(row["hotword_hits"], ["轴伊"])
        self.assertEqual(row["hotword_false_positives"], ["JPet"])
        self.assertEqual(row["hotword_misses"], [])
        summary = probe.aggregate([row])
        self.assertEqual(summary["hotword_recall"], 1)
        self.assertEqual(summary["false_positive_sample_rate"], 1)
        self.assertEqual(summary["rtf"], .008)

    def test_empty_reference_keeps_insertion_errors(self):
        row = probe.score_result({"expected": ""}, {"text": "轴伊"}, [{"text": "轴伊"}])
        self.assertIsNone(row["cer"])
        self.assertEqual(row["character_errors"], 2)
        self.assertEqual(row["hotword_false_positives"], ["轴伊"])

    def test_probe_output_requires_all_configs(self):
        samples = [{"id": "a", "wav": "/input.wav", "expected": "轴伊", "label": "hotword", "source": "real"}]
        records = [{"type": "warmup", "config": config} for config in probe.CONFIGS]
        records.extend({"type": "result", "config": config, "input_index": 0, "wav": "/input.wav",
                        "text": "轴伊", "decode_ms": 4, "audio_seconds": 1} for config in probe.CONFIGS)
        encode = lambda values: "\n".join(json.dumps(value) for value in values)
        rows, warmups = probe.parse_probe_output(encode(records), samples, [{"text": "轴伊"}])
        self.assertEqual(len(rows), 3)
        self.assertEqual(len(warmups), 3)
        self.assertTrue(all(row["cer"] == 0 for row in rows))
        self.assertEqual(len(probe.summarize(rows)), 6)
        with self.assertRaisesRegex(ValueError, "every sample"):
            probe.parse_probe_output(encode(records[:-1]), samples, [{"text": "轴伊"}])
        with self.assertRaisesRegex(ValueError, "duplicate"):
            probe.parse_probe_output(encode(records + [records[-1]]), samples, [{"text": "轴伊"}])

    def test_manifest_token_and_label_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "a.wav").write_bytes(b"placeholder")
            tokens = root / "tokens.txt"
            tokens.write_text("轴 1\n伊 2\n", encoding="utf-8")
            manifest_path = root / "manifest.json"
            manifest = {"hotwords": [{"text": "轴伊", "tokens": ["轴", "伊"]}], "samples": [
                {"id": "a", "wav": "a.wav", "expected": "轴伊你好", "label": "hotword", "source": "real"}]}
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self.assertEqual(probe.load_manifest(manifest_path, tokens)["samples"][0]["wav"], str((root / "a.wav").resolve()))
            manifest["samples"][0]["label"] = "negative"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "label disagrees"):
                probe.load_manifest(manifest_path, tokens)
            manifest["hotwords"][0]["tokens"] = ["不存在"]
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unknown hotword token"):
                probe.load_manifest(manifest_path, tokens)

    def test_synthetic_uses_safe_argument_arrays_and_persistent_distinct_wavs(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "fixtures"
            args = type("Args", (), {"output_dir": output, "voices": ["Tingting", "Meijia"]})()
            calls = []

            def run(command, **kwargs):
                self.assertIsInstance(command, list)
                self.assertNotIn("shell", kwargs)
                calls.append(command)
                if command[0] == "afconvert":
                    Path(command[-1]).write_bytes(b"fixture")

            with patch.object(probe.shutil, "which", return_value="/usr/bin/tool"), patch.object(probe.subprocess, "run", run):
                probe.synthetic(args)
            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(len(manifest["samples"]), 42)
            self.assertEqual(len(calls), 84)
            self.assertEqual(len({s["wav"] for s in manifest["samples"]}), 42)
            self.assertTrue(all(s["source"] == "synthetic" for s in manifest["samples"]))
            self.assertTrue(all((output / s["wav"]).exists() for s in manifest["samples"]))
            for label in probe.LABELS:
                self.assertEqual(sum(s["label"] == label for s in manifest["samples"]), 14)


@unittest.skipUnless(os.environ.get("JPET_CTC_PROBE"), "set JPET_CTC_PROBE for C++ WAV parser checks")
class WavReaderTests(unittest.TestCase):
    def check(self, payload, valid):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.wav"
            path.write_bytes(payload)
            completed = subprocess.run([os.environ["JPET_CTC_PROBE"], "--check-wav", str(path)], capture_output=True, text=True)
            if valid:
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertEqual(json.loads(completed.stdout)["samples"], 160)
            else:
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("voice_ctc_hotword_probe:", completed.stderr)

    @staticmethod
    def wav(rate=16000, channels=1, width=2):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.wav"
            with wave.open(str(path), "wb") as wav:
                wav.setparams((channels, width, rate, 0, "NONE", "not compressed"))
                wav.writeframes(b"\x00" * 160 * channels * width)
            return path.read_bytes()

    @staticmethod
    def riff(chunks):
        content = b"WAVE" + chunks
        return b"RIFF" + struct.pack("<I", len(content)) + content

    def test_valid(self):
        self.check(self.wav(), True)

    def test_metadata_odd_chunk_and_data_before_format(self):
        wav = self.wav()
        self.check(self.riff(b"JUNK\x03\x00\x00\x00abc\x00" + wav[36:] + wav[12:36]), True)

    def test_invalid_formats(self):
        for options in ({"rate": 44100}, {"channels": 2}, {"width": 1}):
            with self.subTest(options=options):
                self.check(self.wav(**options), False)

    def test_truncated_and_inconsistent_headers(self):
        wav = self.wav()
        for payload in (b"", b"data" * 20, wav[:-1], wav + b"junk", self.riff(wav[12:36]),
                        self.riff(wav[12:] + wav[36:]), self.riff(wav[12:] + wav[12:36]),
                        self.riff(wav[12:36] + b"data\x01\x00\x00\x00x\x00"),
                        self.riff(wav[12:] + b"JUNK\x01\x00\x00\x00x"),
                        self.riff(wav[12:36] + b"data\x00\x00\x00\x00")):
            with self.subTest(size=len(payload)):
                self.check(payload, False)


if __name__ == "__main__":
    unittest.main()
