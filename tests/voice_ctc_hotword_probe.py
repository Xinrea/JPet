#!/usr/bin/env python3
"""Opt-in SenseVoice CTC experiment; stdlib only. See README-voice-ctc-hotwords.md."""
import argparse
import hashlib
import json
import math
import shutil
import subprocess
import sys
import tempfile
import unicodedata
from pathlib import Path

CONFIGS = ("greedy", "beam", "hotwords")
LABELS = ("hotword", "negative", "control")


def normalize(text):
    """NFKC + casefold, remove Unicode punctuation and whitespace; keep symbols."""
    return "".join(c for c in unicodedata.normalize("NFKC", text).casefold()
                   if not c.isspace() and not unicodedata.category(c).startswith("P"))


def edit_distance(reference, hypothesis):
    previous = list(range(len(hypothesis) + 1))
    for i, a in enumerate(reference, 1):
        current = [i]
        for j, b in enumerate(hypothesis, 1):
            current.append(min(previous[j] + 1, current[-1] + 1,
                               previous[j - 1] + (a != b)))
        previous = current
    return previous[-1]


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_manifest(path, tokens_path):
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict):
        raise ValueError("manifest must be an object")
    hotwords, samples = manifest.get("hotwords"), manifest.get("samples")
    if not isinstance(hotwords, list) or not hotwords or not isinstance(samples, list) or not samples:
        raise ValueError("manifest requires nonempty hotwords and samples arrays")
    # Validate against the actual model vocabulary, not a guessed Chinese split.
    vocabulary = set()
    for line in tokens_path.read_text(encoding="utf-8").splitlines():
        fields = line.rsplit(maxsplit=1)
        if len(fields) == 2:
            vocabulary.add(fields[0])
    seen_words = set()
    for word in hotwords:
        if not isinstance(word, dict) or not isinstance(word.get("text"), str) or not normalize(word["text"]):
            raise ValueError("each hotword requires nonempty display text")
        tokens = word.get("tokens")
        if not isinstance(tokens, list) or not tokens:
            raise ValueError("each hotword requires a nonempty tokens array")
        for token in tokens:
            if not isinstance(token, str) or not token or any(c.isspace() for c in token) or token not in vocabulary:
                raise ValueError(f"invalid/unknown hotword token: {token!r}")
        normalized = normalize(word["text"])
        if normalized in seen_words:
            raise ValueError("duplicate normalized hotword")
        seen_words.add(normalized)
    seen_ids, seen_wavs = set(), set()
    for sample in samples:
        if not isinstance(sample, dict):
            raise ValueError("each sample must be an object")
        if not isinstance(sample.get("id"), str) or not sample["id"] or sample["id"] in seen_ids:
            raise ValueError("sample ids must be unique nonempty strings")
        seen_ids.add(sample["id"])
        if sample.get("label") not in LABELS or sample.get("source") not in ("real", "synthetic"):
            raise ValueError("samples require label=hotword|negative|control and source=real|synthetic")
        if not isinstance(sample.get("expected"), str) or not isinstance(sample.get("wav"), str):
            raise ValueError("sample expected and wav must be strings")
        wav = (path.parent / sample["wav"]).resolve(strict=True)
        if not wav.is_file() or wav in seen_wavs:
            raise ValueError("each sample must name a distinct WAV file")
        seen_wavs.add(wav)
        sample["wav"] = str(wav)
        contains_hotword = any(word in normalize(sample["expected"]) for word in seen_words)
        if (sample["label"] == "hotword") != contains_hotword:
            raise ValueError(f"{sample['id']}: label disagrees with expected hotword text")
    return manifest


def score_result(sample, result, hotwords):
    reference, hypothesis = normalize(sample["expected"]), normalize(result["text"])
    expected = {word["text"] for word in hotwords if normalize(word["text"]) in reference}
    predicted = {word["text"] for word in hotwords if normalize(word["text"]) in hypothesis}
    errors = edit_distance(reference, hypothesis)
    return {
        **sample, **result,
        "normalized_expected": reference, "normalized_text": hypothesis,
        "character_errors": errors, "reference_characters": len(reference),
        "cer": errors / len(reference) if reference else None,
        "expected_hotwords": sorted(expected), "hotword_hits": sorted(expected & predicted),
        "hotword_misses": sorted(expected - predicted),
        "hotword_false_positives": sorted(predicted - expected),
    }


def aggregate(rows):
    errors = sum(row["character_errors"] for row in rows)
    characters = sum(row["reference_characters"] for row in rows)
    expected = sum(len(row["expected_hotwords"]) for row in rows)
    hits = sum(len(row["hotword_hits"]) for row in rows)
    false_positive_samples = sum(bool(row["hotword_false_positives"]) for row in rows)
    audio = sum(row["audio_seconds"] for row in rows)
    decode = sum(row["decode_ms"] for row in rows)
    return {
        "samples": len(rows), "character_errors": errors, "reference_characters": characters,
        "cer": errors / characters if characters else None,
        "hotword_opportunities": expected, "hotword_hits": hits,
        "hotword_recall": hits / expected if expected else None,
        "hotword_false_positives": sum(len(row["hotword_false_positives"]) for row in rows),
        "false_positive_samples": false_positive_samples,
        "false_positive_sample_rate": false_positive_samples / len(rows),
        "mean_decode_ms": decode / len(rows), "max_decode_ms": max(row["decode_ms"] for row in rows),
        "audio_seconds": audio, "decode_ms": decode, "rtf": decode / 1000 / audio,
    }


def summarize(rows):
    # Never pool synthetic and real recordings into one accuracy number.
    summaries = []
    for source in ("real", "synthetic"):
        for label in ("all", *LABELS):
            for config in CONFIGS:
                selected = [row for row in rows if row["source"] == source and row["config"] == config
                            and (label == "all" or row["label"] == label)]
                if selected:
                    summaries.append({"source": source, "label": label, "config": config, **aggregate(selected)})
    return summaries


def parse_probe_output(stdout, samples, hotwords):
    rows, warmups, seen = [], {}, set()
    for line in stdout.splitlines():
        result = json.loads(line)
        config = result.get("config")
        if config not in CONFIGS:
            raise ValueError(f"unexpected probe config: {config!r}")
        if result.get("type") == "warmup":
            if config in warmups or rows:
                raise ValueError("duplicate or late warmup record")
            warmups[config] = result
            continue
        if result.get("type") != "result" or len(warmups) != 3:
            raise ValueError("expected result after all three warmups")
        index = result.get("input_index")
        if type(index) is not int or not 0 <= index < len(samples) or (index, config) in seen:
            raise ValueError("duplicate or invalid probe input index")
        sample = samples[index]
        if result.get("wav") != sample["wav"] or not isinstance(result.get("text"), str):
            raise ValueError("probe returned wrong WAV or invalid transcript")
        for field in ("audio_seconds", "decode_ms"):
            value = result.get(field)
            if not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
                raise ValueError(f"invalid {field}")
        if result["audio_seconds"] == 0:
            raise ValueError("empty audio result")
        seen.add((index, config))
        rows.append(score_result(sample, result, hotwords))
    if len(seen) != len(samples) * 3:
        raise ValueError("probe did not return every sample/config pair")
    return sorted(rows, key=lambda row: (row["input_index"], CONFIGS.index(row["config"]))), list(warmups.values())


def run(args):
    if not math.isfinite(args.score):
        raise ValueError("score must be finite")
    model_dir = args.model_dir.resolve(strict=True)
    probe = args.probe.resolve(strict=True)
    manifest = load_manifest(args.manifest.resolve(strict=True), model_dir / "tokens.txt")
    samples, hotwords = manifest["samples"], manifest["hotwords"]
    for sample in samples:
        sample["wav_sha256"] = sha256(Path(sample["wav"]))
    hotwords_text = "".join(" ".join(word["tokens"]) + "\n" for word in hotwords)
    with tempfile.TemporaryDirectory(prefix="jpet-ctc-") as temp:
        hotwords_path = Path(temp) / "hotwords.txt"
        hotwords_path.write_text(hotwords_text, encoding="utf-8")
        command = [str(probe), str(model_dir), str(hotwords_path), str(args.score),
                   *(sample["wav"] for sample in samples)]
        completed = subprocess.run(command, check=True, text=True, encoding="utf-8", stdout=subprocess.PIPE,
                                   timeout=args.timeout)
    # Reject recordings changed during the run so the report fingerprints the input.
    for sample in samples:
        if sha256(Path(sample["wav"])) != sample["wav_sha256"]:
            raise ValueError(f"WAV changed during run: {sample['wav']}")
    rows, warmups = parse_probe_output(completed.stdout, samples, hotwords)
    report = {
        "experiment": "opt-in JPet SenseVoice CTC hotwords; not a production accuracy claim",
        "settings": {"beam_method": "jpet_ctc_beam_search", "max_active_paths": 8,
                     "language": "auto", "use_itn": 1, "threads": 2, "provider": "cpu",
                     "hotwords_score": args.score, "hotwords_file_contents": hotwords_text},
        "normalization": "Unicode NFKC + casefold; remove punctuation and whitespace; preserve symbols",
        "metrics": "CER is character edits/reference length (null for empty reference); hotword presence uses normalized substrings, once per phrase/sample; latency times only SherpaOnnxDecodeOfflineStream",
        "probe": str(probe), "probe_sha256": sha256(probe),
        "model_dir": str(model_dir), "model_sha256": sha256(model_dir / "model.int8.onnx"),
        "tokens_sha256": sha256(model_dir / "tokens.txt"),
        "warmups": warmups, "results": rows, "summary": summarize(rows),
    }
    # Exclusive create: don't silently replace an earlier score's report.
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")
    print(f"Wrote {args.output}: {len(samples)} samples, three recognizers, score={args.score}", file=sys.stderr)


def synthetic(args):
    # Persistent WAVs, temporary synthesis inputs. Pass arguments as arrays, never
    # through a shell; text goes in a file, so even leading '-' is not an option.
    if not shutil.which("say") or not shutil.which("afconvert"):
        raise ValueError("synthetic fixtures require macOS say and afconvert")
    directory = args.output_dir.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    examples = [
        ("positive-greeting", "hotword", "轴伊你好呀"),
        ("positive-request", "hotword", "轴伊帮我看看现在有多少经验"),
        ("positive-task", "hotword", "轴伊把读书加入任务队列"),
        ("positive-news", "hotword", "帮我搜一下今天的新闻轴伊"),
        ("positive-volume", "hotword", "轴伊把音量调小一点"),
        ("positive-weather", "hotword", "轴伊今天天气怎么样"),
        ("positive-thanks", "hotword", "谢谢你轴伊"),
        ("ambiguous-monday", "negative", "周一提醒我读书"),
        ("ambiguous-attention", "negative", "注意今天的天气"),
        ("ambiguous-week", "negative", "下周一有什么任务"),
        ("ambiguous-one-by-one", "negative", "请逐一检查任务列表"),
        ("ambiguous-monday-morning", "negative", "周一早上八点叫我"),
        ("ambiguous-notice", "negative", "请注意音量不要太大"),
        ("ambiguous-fold", "negative", "这件衣服起皱，衣领也不平"),
        ("control-weather", "control", "今天天气怎么样"),
        ("control-volume", "control", "把音量调小一点"),
        ("control-task", "control", "取消当前的任务"),
        ("control-browser", "control", "用浏览器打开哔哩哔哩"),
        ("control-joke", "control", "给我讲个笑话吧"),
        ("control-wait", "control", "等一下我还没说完"),
        ("control-outfit", "control", "换成粉色的衣服"),
    ]
    samples = []
    with tempfile.TemporaryDirectory(prefix="jpet-ctc-say-") as temp:
        text, aiff = Path(temp) / "text.txt", Path(temp) / "speech.aiff"
        for voice_index, voice in enumerate(args.voices):
            for ident, label, expected in examples:
                text.write_text(expected, encoding="utf-8")
                aiff.unlink(missing_ok=True)
                subprocess.run(["say", "-v", voice, "-f", str(text), "-o", str(aiff)], check=True, timeout=120)
                sample_id = f"voice{voice_index}-{ident}"
                wav = directory / f"{sample_id}.wav"
                subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16@16000", "-c", "1", str(aiff), str(wav)],
                               check=True, timeout=120)
                samples.append({"id": sample_id, "wav": wav.name, "expected": expected, "label": label,
                                "source": "synthetic", "voice": voice})
    manifest = {"hotwords": [{"text": "轴伊", "tokens": ["轴", "伊"]}], "samples": samples}
    (directory / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"SYNTHETIC fixtures only: {directory / 'manifest.json'}; not real-recording accuracy evidence")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    evaluate = commands.add_parser("run", help="evaluate a manifest and write a new JSON report")
    evaluate.add_argument("--probe", required=True, type=Path)
    evaluate.add_argument("--model-dir", required=True, type=Path)
    evaluate.add_argument("--manifest", required=True, type=Path)
    evaluate.add_argument("--score", required=True, type=float)
    evaluate.add_argument("--output", required=True, type=Path)
    evaluate.add_argument("--timeout", type=float, default=1800, help="total probe timeout in seconds")
    evaluate.set_defaults(action=run)
    fixtures = commands.add_parser("synthetic", help="create a small, clearly labeled macOS TTS fixture set")
    fixtures.add_argument("--output-dir", type=Path, required=True, help="new directory (must not already exist)")
    fixtures.add_argument("--voices", nargs="+", default=["Tingting", "Meijia"],
                          help="macOS voices, in order (default: Tingting Meijia)")
    fixtures.set_defaults(action=synthetic)
    args = parser.parse_args()
    try:
        args.action(args)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
