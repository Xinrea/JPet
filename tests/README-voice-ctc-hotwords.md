# Opt-in SenseVoice CTC hotword experiment

JPet now uses **weight 12, beam 8, and the bundled 轴伊 hotword** through the
patched sherpa-onnx v1.13.8 overlay port. These standalone probes remain useful
for comparing greedy/beam/other scores without changing the application's
configuration. They require the **custom patched runtime** implementing
`jpet_ctc_beam_search`; a stock runtime must not be treated as evidence of
working hotwords. Never overwrite JPet's installed dylibs to run a comparison.

## Reproducible experimental build (macOS arm64)

From the repository root, with Python 3.9+, CMake, Ninja, Git, curl and Xcode C++ tools:

```sh
python3 -B build-scripts/build_sensevoice_hotwords.py
OUT="$PWD/build/sensevoice-hotwords"
MODEL="$HOME/Library/Application Support/JPet/models/sensevoice-int8-2025-09-09"
"$OUT/voice_ctc_config_test" "$MODEL"
```

The helper verifies the v1.13.8 source archive SHA-256, applies the checked-in
`build-scripts/ports/sherpa-onnx-bin/sensevoice-hotwords.patch`, and builds
only the custom C API target with upstream's checksum-pinned dependencies
(including ONNX Runtime 1.28.2). Example-only symlinks in the upstream archive
are deliberately not extracted. It compiles both probes/tests and runs the
model-free decoder test (900 exhaustive path comparisons plus edge cases).
The config test is a separate opt-in command because it loads the local model.

Generated source, dependencies, dylibs, probes and `build-info.json` stay under
`build/sensevoice-hotwords/`; no application bundle, vcpkg port, production
settings or system installation is modified. The helper does not download ASR
model weights. First run needs Internet access for build dependencies, but
recognition and fixture synthesis are local.

Re-running with the same patch reuses the source/build. If the patch changes,
choose a fresh directory, e.g. `--build-dir build/sensevoice-hotwords-v2`;
the helper refuses to overwrite a source with a different patch stamp.
Do not hand-edit generated source; edit/regenerate the patch instead.
This is a local experiment, not a redistribution/package build. Windows and
other architectures have not been validated. There is no per-stream hotword
API or automatic text-to-token conversion; recreate the experimental recognizer
when its token file or score changes. Only SenseVoice's regular ONNX backend is
supported, not FunASR-Nano or accelerator-specific implementations.

See [recorded experiment results](../doc/sensevoice-hotword-experiment.md) for
measured effects and known limits. Do **not** copy these dylibs into JPet's app
bundle: normal app builds obtain the same patch through the vcpkg overlay port,
which also provides a capability marker checked before creating the recognizer.

## Build the comparison probe manually (macOS)

Dependencies: a C++17 compiler, the v1.13.8 `sherpa-onnx/c-api/c-api.h`, the
custom `libsherpa-onnx-c-api.dylib` and its ONNX Runtime dependencies. The Python
runner/tests require Python 3.9+ and only the standard library. No Qt, JPet
source dependencies, pip packages, or root CMake target are involved.

From the repository root, set the source/header root and **custom** library
folder. The library folder can be the custom build's `build/lib` or `install/lib`;
it must contain the patched C API library and have its runtime dependencies
resolvable. The following is the full compile command:

```sh
SHERPA_SRC="$PWD/build/sensevoice-hotwords/source"
SHERPA_LIB="$PWD/build/sensevoice-hotwords/install/lib"
OUT="$PWD/build/voice-ctc-probe"
mkdir -p "$OUT"
c++ -std=c++17 -O2 -Wall -Wextra -pedantic -mmacosx-version-min=11.0 \
  -I "$SHERPA_SRC" tests/voice_ctc_hotword_probe.cpp \
  "$SHERPA_LIB/libsherpa-onnx-c-api.dylib" \
  -Wl,-rpath,"$SHERPA_LIB" -o "$OUT/voice_ctc_hotword_probe"
```

The absolute linked library and rpath select this runtime, not the app's
runtime. Use `otool -L "$OUT/voice_ctc_hotword_probe"` to inspect linkage; don't
set an overriding `DYLD_LIBRARY_PATH` pointing to a stock runtime.

The probe always compares three separately loaded recognizers:

| Report config | Decoding method | Hotwords |
| --- | --- | --- |
| `greedy` | `greedy_search` | none |
| `beam` | `jpet_ctc_beam_search`, `max_active_paths=8` | none |
| `hotwords` | `jpet_ctc_beam_search`, `max_active_paths=8` | token file and supplied score |

All use the **same** `model.int8.onnx` and `tokens.txt`, SenseVoice language
`auto`, `use_itn=1`, two threads, CPU, 16 kHz features of dimension 80. Each is
warmed with the first recording once before any measurements. Warmup and load
times are separate records. Each measured WAV is read once and the **same float
sample buffer** is passed to all three recognizers. Decode order rotates between
recordings. Wall latency measures only `SherpaOnnxDecodeOfflineStream`, excluding
model loading, WAV I/O, waveform acceptance, result extraction, and Python work.
It is a small single-pass comparison, not a rigorous steady-state benchmark.

## Real-recording manifest

Use actual 16 kHz **mono, little-endian PCM16 RIFF/WAVE** recordings. The C++ reader
walks RIFF chunks (including metadata/padding), verifies lengths and the PCM
format, and rejects truncated, empty, stereo, compressed, floating-point, or
wrong-rate recordings. Each file is limited to 128 MiB. It never silently
resamples. Convert source recordings explicitly if needed, keeping the resulting
WAV fixed for every score/configuration.

Save this JSON structure next to your recordings, replacing the example paths
and expected text with verified human transcripts:

```json
{
  "hotwords": [
    {"text": "轴伊", "tokens": ["轴", "伊"]}
  ],
  "samples": [
    {"id": "speaker1-positive", "wav": "positive.wav", "expected": "轴伊你好呀", "label": "hotword", "source": "real"},
    {"id": "speaker1-near-sound", "wav": "negative.wav", "expected": "周一提醒我读书", "label": "negative", "source": "real"},
    {"id": "speaker1-control", "wav": "control.wav", "expected": "把音量调小一点", "label": "control", "source": "real"}
  ]
}
```

Paths are relative to the manifest (absolute paths also work). IDs and WAV paths
must be distinct. `hotword` means at least one target phrase appears in the
expected transcript. `negative` is a deliberately confusable utterance without
a target phrase; `control` is an ordinary non-target utterance. Empty expected
text is allowed for silence/control recordings. `source` is required and must
be `real` or `synthetic`; metrics never pool those sources.

`text` is the human-readable phrase for matching. `tokens` are the **actual model
vocabulary tokens**, not arbitrary text; the runner checks each against
`tokens.txt` and writes one UTF-8, whitespace-separated token sequence per line
to a temporary hotwords file, e.g. `轴 伊`. For non-Chinese phrases supply the
model's correct tokenization yourself (including SentencePiece markers when
applicable); this script is not a tokenizer. The C++ CLI alone expects you to
prepare/validate that file.

```sh
MODEL="$HOME/Library/Application Support/JPet/models/sensevoice-int8-2025-09-09"
python3 -B tests/voice_ctc_hotword_probe.py run \
  --probe "$OUT/voice_ctc_hotword_probe" --model-dir "$MODEL" \
  --manifest /absolute/path/to/recordings/manifest.json \
  --score 1.5 --output "$OUT/real-score-1.5.json"
```

The score is an adjustable finite float in `0..100`, interpreted as bonus per
hotword token. Zero disables the reward (but still includes hotword tokens in
candidate pruning); increasing the score can strongly bias unrelated speech.
Incomplete phrase rewards are refunded at the end of the utterance.
The example score `1.5` is a starting point, **not** a recommendation. Repeat the **same manifest and stored
WAVs** at other scores, using different output filenames. The runner creates
reports exclusively (never overwrites), passes argument arrays without a shell,
uses a private temporary directory for token files, enforces a configurable
`--timeout` (default 1800 seconds), and fails the run if any sample/configuration
is missing. Standard error from the runtime is preserved.

For raw JSONL without Python metrics, the exact positional CLI is:

```sh
printf '轴 伊\n' > "$OUT/hotwords.txt"
"$OUT/voice_ctc_hotword_probe" "$MODEL" "$OUT/hotwords.txt" 1.5 \
  /absolute/path/to/positive.wav /absolute/path/to/negative.wav
```

## Optional synthetic smoke test (not accuracy evidence)

On macOS with the selected system voices installed:

```sh
python3 -B tests/voice_ctc_hotword_probe.py synthetic \
  --output-dir "$OUT/synthetic" --voices Tingting Meijia
python3 -B tests/voice_ctc_hotword_probe.py run \
  --probe "$OUT/voice_ctc_hotword_probe" --model-dir "$MODEL" \
  --manifest "$OUT/synthetic/manifest.json" \
  --score 1.5 --output "$OUT/synthetic-score-1.5.json"
```

`--output-dir` must not already exist. There are 21 sentences per voice: seven
positive, seven ambiguous negative, seven control. Defaults run Tingting first,
then Meijia (42 WAVs); pass `--voices Tingting` for a smaller smoke test. `say`
and `afconvert` run once per sentence/voice, with temporary text/AIFF files and
persistent converted WAVs. The recognition comparison never resynthesizes a
sentence between recognizers. Missing voices or failed conversion abort rather
than silently omitting examples; choose a fresh output directory after failure.

All generated samples explicitly carry `source: synthetic` and the voice.
Listen to generated speech: TTS may pronounce a rare name differently than
people do. These examples can expose hotword effects and false positives, but
cannot establish real-user recognition accuracy or an optimal production score.

## Report interpretation

JSON reports include raw/normalized transcripts, reference text and labels,
per-sample CER and decode milliseconds, audio duration, expected hotwords,
hits/misses/false positives, separate warmup timings, settings and input hashes.
Model, vocabulary, probe binary and WAV SHA-256 hashes aid reproducibility;
preserve `build-info.json` next to each sweep's reports. It records the archive,
patch, custom runtime, ONNX Runtime and probe SHA-256 hashes; a probe binary hash
alone does not identify a dynamically linked library. Do not replace dylibs or
set overriding `DYLD_*` variables during a sweep.

Normalization is Unicode NFKC + casefold, removing all Unicode punctuation and
whitespace but retaining symbols and digits. CER uses Unicode code-point
Levenshtein edits divided by reference length, and can exceed 1. Empty-reference
CER is `null`; its insertion errors still count in corpus totals. Corpus CER
uses total edits / total reference characters, not mean per-utterance CER.

A hit is normalized target-phrase **substring presence**, once per phrase per
utterance, not a token occurrence count or word-boundary metric. False positives
are predicted target phrases absent from the reference (including unwanted
additional targets on positive samples). Summaries separate `real`/`synthetic`,
config, and `hotword`/`negative`/`control`, plus an all-label summary for each
source. Each summary reports hit opportunities/recall, false-positive sample
count/rate, CER, mean/max decode latency, and aggregate real-time factor. Inspect
negative/control rows rather than choosing a score solely for positive recall.

## Helper validation

```sh
# Build-source preparation + normalization, scoring, manifest/protocol and fixtures
python3 -B -m unittest discover -s tests -p 'voice_ctc_*test.py' -v

# Also exercise the actual compiled C++ WAV parser (no model loading/inference)
JPET_CTC_PROBE="$OUT/voice_ctc_hotword_probe" \
  python3 -B -m unittest discover -s tests -p 'voice_ctc_*test.py' -v

# Standalone WAV format check
"$OUT/voice_ctc_hotword_probe" --check-wav /absolute/path/to/recording.wav
```

The compiled parser tests include wrong sample rate/channels/width, metadata,
odd-sized padded chunks, data preceding format, duplicate chunks, truncated
payloads, RIFF length mismatches, missing padding, empty audio and odd PCM bytes.
Passing these tests validates the experiment plumbing, **not recognition
accuracy**. Do not infer accuracy until actual recordings have been decoded.
