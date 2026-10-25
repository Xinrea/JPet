# Windows beta.4 model-loading crash

## Evidence

The supplied `minidump_3.0.0-beta.4.dmp` records an access violation
(`0xC0000005`, read of `0x8`) at `JPet.exe + 0x210356`. The matching beta.4
release executable has `mov rbx, [rax+8]` there, with `rax == 0`. Its surrounding
instructions match `CubismModelSettingJson::GetModelFileName()` from an older
local build with symbols (allowing for relocated addresses). The old PDB is
**not** a matching symbol file for beta.4; its offsets must not be used to
symbolicate the dump directly.

The stack also contains:

- `[CSM][E][CubismJsonHolder] Invalid Json document.`
- `resources//joi/zmcw.model3.json`

A failed JSON parse leaves Cubism's cached node vector empty. The original
`LAppModel::LoadAssets()` called its getters without checking the read or parse
result. A headless Windows reproducer using the same unchecked sequence with a
missing file also raises `0xC0000005`.

The beta.4 ZIP contains valid `resources/joi/ZMCW.model3.json`. The leading
trigger hypothesis is a different launch working directory: Windows startup
previously assigned `execPath` but left relative resource reads dependent on the
launcher's directory. The dump does not establish the original working
directory or rule out a missing/damaged installed file. The affected user's
`Documents/jpet/jpet.log` and launch method would distinguish these cases.

## Fix

- Establish the executable directory as the Windows working directory before
  application initialization, matching the resource-root convention on macOS.
- Reset file lengths on failure, check complete reads, and use UTF-8 filesystem
  paths without leaking failed-read buffers.
- Validate model settings before any Cubism getter and reject a missing Moc
  reference. Report startup errors instead of dereferencing failed settings.
- Keep partially loaded models under RAII ownership and permit their destruction
  when initialization fails.

## Validation

Windows MSVC Release compilation passed for all changed C++ translation units.
`model_settings_test` passed under CTest, using the real file loader and Cubism
parser. It covers missing/empty/malformed settings, missing Moc references,
relative paths before/after establishing the resource root, and Unicode paths.
The test is enabled by `JPET_BUILD_MODEL_TESTS` and by the Windows release job:

```powershell
cmake --build build/release --config Release --target model_settings_test
ctest --test-dir build/release -C Release -R '^model_settings$' --output-on-failure
```

For this investigation the tests were built in an isolated temporary source
export against existing Windows SDK/dependency libraries, not a full application
build. Full configuration was blocked by the remote machine's old sherpa-onnx
runtime failing the project's patched-runtime check. The actual beta.4 GUI was
also attempted over SSH, but exited with `No display is available` before model
loading. Full packaged startup from a foreign working directory remains to be
verified in an interactive desktop session. macOS syntax checking passed for the
shared file-loader change; no macOS application build was run.

Follow-up: retain matching release PDBs as a separate CI artifact. The current
release workflow strips them from the package and does not archive them, which
makes field dumps unnecessarily difficult to diagnose. Do not commit user dumps
or logs; they can contain private process data.
