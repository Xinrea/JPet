# Production SenseVoice wrapper regression tests

These tests compile the actual `src/VoiceSpeech.cpp` and load tiny fake sherpa
shared libraries through the production dynamic loader. They require only C++17
and the sherpa C API header: no model weights, ONNX Runtime, audio hardware, or
network access. The test executable stubs `Voice::LoadOnnxRuntime` and
`Voice::OnnxRuntimeLibrary`; do **not** link `VoiceOnnx.cpp` into this target.

## CMake integration

After `find_package(unofficial-sherpa-onnx-bin CONFIG REQUIRED)`, inside the
existing `JPET_BUILD_VOICE_TESTS` block (already wired in the root CMake), include:

```cmake
include(tests/voice_speech_tests.cmake)
```

Then build and run:

```sh
cmake --build <build-directory> --target voice_speech_test
ctest --test-dir <build-directory> -R '^voice_speech_' --output-on-failure
```

The include builds the executable and four mock shared libraries. Each test is a
separate process because the production loader caches both success and failure.
Tests can run in parallel; generated invalid-resource fixtures have separate
per-scenario directories. No installed runtime is overwritten.

| Scenario | What it verifies |
| --- | --- |
| `success` | Required capability version 1; decoder `jpet_ctc_beam_search`, beam 8, score 12; bundled `轴 伊` token file (LF or CRLF); unchanged SenseVoice configuration; signed PCM conversion; repeated real `Transcribe` calls; stream/result/recognizer cleanup; null stream/result/text; successful cached loader |
| `missing_hotwords` | Absent resource fails before runtime creation, then valid resource recovers |
| `empty_hotwords` | Empty resource fails before runtime creation, then valid resource recovers |
| `blank_hotwords` | Whitespace-only resource fails before runtime creation, then valid resource recovers |
| `create_failure` | Failed runtime recognizer creation returns an error and never destroys a null recognizer |
| `stock` | No capability symbol: both loads fail without ever entering recognizer creation |
| `wrong_version` | Capability version 2: both loads fail before recognizer creation |
| `incomplete` | Missing final `SherpaOnnxDestroyOfflineRecognizerResult` symbol: both loads fail, even though create was already bound |
| `missing_library` | Absent runtime returns a reinstall error on both attempts |

Checks use exceptions rather than `assert`, so Release builds retain them.
Mock lifetime counters observe cleanup in the real wrapper; they are not a
replacement for a real decoder/model test.

## Manual build (macOS)

From the repository root, point `SHERPA_INCLUDE` at a directory containing
`sherpa-onnx/c-api/c-api.h` (stock headers also suffice). Choose a fresh output
directory inside the worktree, not a symlink to another checkout:

```sh
SHERPA_INCLUDE="$PWD/build/sensevoice-hotwords/source"
OUT="$PWD/build/voice-speech-tests"
mkdir -p "$OUT"
for variant in compatible stock wrong_version incomplete; do
  defines=
  case "$variant" in
    stock) defines=-DJPET_MOCK_STOCK ;;
    wrong_version) defines=-DJPET_MOCK_WRONG_VERSION ;;
    incomplete) defines=-DJPET_MOCK_INCOMPLETE ;;
  esac
  c++ -std=c++17 -Wall -Wextra -pedantic -dynamiclib $defines \
    -I "$SHERPA_INCLUDE" tests/voice_speech_mock.cpp \
    -o "$OUT/libmock-$variant.dylib" || exit
 done
c++ -std=c++17 -Wall -Wextra -pedantic -I src -I "$SHERPA_INCLUDE" \
  tests/voice_speech_test.cpp src/VoiceSpeech.cpp -o "$OUT/voice_speech_test" || exit
for scenario in success missing_hotwords empty_hotwords blank_hotwords create_failure \
                stock wrong_version incomplete missing_library; do
  variant=compatible
  case "$scenario" in
    stock|wrong_version|incomplete) variant="$scenario" ;;
    missing_library) variant=missing ;;
  esac
  "$OUT/voice_speech_test" "$scenario" "$OUT/libmock-$variant.dylib" \
    "$PWD/resources/models/sensevoice-hotwords.txt" "$OUT/fixtures/$scenario" || exit
 done
```

For actual audio evaluation, use `voice_speech_probe` against the installed patched
runtime and the same final audio fixtures as the CTC comparison probe. Mock tests
prove application wiring and safe loader failure, not recognition quality or
packaging correctness. The CMake targets include Windows export/path support;
macOS is the platform on which these new tests were validated.
