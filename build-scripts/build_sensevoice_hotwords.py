#!/usr/bin/env python3
"""Build the opt-in SenseVoice hotword experiment; never changes JPet's runtime.

Requires macOS arm64, Python 3.9+, CMake, Ninja, Git, curl and Xcode C++ tools.
The source archive is pinned and verified. Upstream CMake verifies its downloaded
third-party dependencies. All generated files stay under the repository build/.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "1.13.8"
ARCHIVE_SHA256 = "b0374cc56dbc186d442ae73d5de743bb092470b640c4c50ce7b029044c0c4fa8"
PATCH = ROOT / "build-scripts/ports/sherpa-onnx-bin/sensevoice-hotwords.patch"


def run(*args, **kwargs):
    args = [str(arg) for arg in args]
    print("+", subprocess.list2cmdline(args), flush=True)
    subprocess.run(args, check=True, **kwargs)


def sha256(path):
    with path.open("rb") as file:
        digest = hashlib.sha256()
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
        return digest.hexdigest()


def prepare_source(directory):
    archive = directory / f"sherpa-onnx-v{VERSION}.tar.gz"
    if not archive.exists():
        partial = archive.with_suffix(".partial")
        run("curl", "--fail", "--location", "--retry", "2", "--output", partial,
            f"https://codeload.github.com/k2-fsa/sherpa-onnx/tar.gz/refs/tags/v{VERSION}")
        if sha256(partial) != ARCHIVE_SHA256:
            raise RuntimeError(f"Archive checksum mismatch: {partial}")
        partial.replace(archive)
    if sha256(archive) != ARCHIVE_SHA256:
        raise RuntimeError(f"Archive checksum mismatch: {archive}")

    stamp = {"version": VERSION, "archive_sha256": ARCHIVE_SHA256,
             "patch_sha256": sha256(PATCH)}
    source = directory / "source"
    if source.exists():
        marker = source / ".jpet-source.json"
        if not marker.exists() or json.loads(marker.read_text()) != stamp:
            raise RuntimeError("Source/patch version changed or source is unmanaged. "
                               "Choose a fresh --build-dir under build/; no files were removed.")
        return source, stamp

    # Extract to staging so interruption cannot leave a half-patched source.
    with tempfile.TemporaryDirectory(prefix="source-", dir=directory) as temporary:
        staging = Path(temporary)
        with tarfile.open(archive, "r:gz") as tar:
            # Upstream has example-only symlinks, including absolute links to
            # its author's machine. None are needed by this native C API build.
            members = [member for member in tar.getmembers() if member.isfile() or member.isdir()]
            for member in members:
                target = (staging / member.name).resolve()
                if staging.resolve() not in target.parents:
                    raise RuntimeError(f"Unsafe archive member: {member.name}")
            tar.extractall(staging, members=members)
        extracted = staging / f"sherpa-onnx-{VERSION}"
        run("git", "apply", "--check", PATCH, cwd=extracted)
        run("git", "apply", PATCH, cwd=extracted)
        (extracted / ".jpet-source.json").write_text(json.dumps(stamp, indent=2) + "\n")
        extracted.rename(source)
    return source, stamp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/sensevoice-hotwords")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 2))
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("This first experiment is macOS arm64 only, not a production dependency build.")
    directory = args.build_dir.resolve()
    if (ROOT / "build").resolve() not in directory.parents or args.jobs < 1:
        parser.error("Use a subdirectory of this repository's build/ and --jobs >= 1.")
    for command in ("cmake", "ninja", "git", "curl", "c++"):
        if not shutil.which(command):
            parser.error(f"Missing required tool: {command}")
    directory.mkdir(parents=True, exist_ok=True)
    source, stamp = prepare_source(directory)
    binary, install = directory / "build", directory / "install"
    # Tarball sources have no Git metadata: avoid accidentally reporting JPet's
    # parent-repository commit as the sherpa source version. The stamp is authority.
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(source.parent))
    run("cmake", "-S", source, "-B", binary, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0",
        "-DBUILD_SHARED_LIBS=ON", "-DSHERPA_ONNX_ENABLE_BINARY=OFF",
        "-DSHERPA_ONNX_BUILD_C_API_EXAMPLES=OFF", "-DSHERPA_ONNX_ENABLE_TTS=OFF",
        "-DSHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION=OFF", "-DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF",
        "-DSHERPA_ONNX_ENABLE_WEBSOCKET=OFF",
        "-DSHERPA_ONNX_USE_PRE_INSTALLED_ONNXRUNTIME_IF_AVAILABLE=OFF",
        f"-DCMAKE_INSTALL_PREFIX={install}", env=env)
    run("cmake", "--build", binary, "--target", "sherpa-onnx-c-api", "-j", args.jobs, env=env)
    # Install only runtime/header artifacts we use. Full upstream cmake --install
    # also expects unrelated targets; this experiment deliberately doesn't build them.
    lib = install / "lib"
    lib.mkdir(parents=True, exist_ok=True)
    ort = binary / "_deps/onnxruntime-src"
    for path in list((binary / "lib").glob("*.dylib")) + list((ort / "lib").glob("*.dylib")):
        shutil.copy2(path, lib / path.name)
    include = install / "include/sherpa-onnx/c-api"
    include.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / "sherpa-onnx/c-api/c-api.h", include / "c-api.h")
    shutil.copy2(source / "LICENSE", install / "SHERPA-LICENSE")
    # Upstream dependency source and license files remain in build/_deps.
    common = ["c++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-pedantic",
              "-mmacosx-version-min=11.0", "-I", source]
    probe_rpath = "-Wl,-rpath,@executable_path/install/lib"
    probe = directory / "voice_ctc_hotword_probe"
    run(*common, ROOT / "tests/voice_ctc_hotword_probe.cpp", lib / "libsherpa-onnx-c-api.dylib",
        probe_rpath, "-o", probe)
    # No model or googletest needed: enumerate toy CTC paths and compare with
    # the actual patched decoder (including context scoring and timestamps).
    decoder_test = directory / "voice_ctc_decoder_test"
    run(*common, "-I", ort / "include", ROOT / "tests/voice_ctc_decoder_test.cpp",
        binary / "lib/libsherpa-onnx-core.a", lib / "libonnxruntime.dylib",
        probe_rpath, "-o", decoder_test)
    run(decoder_test)
    config_test = directory / "voice_ctc_config_test"
    run(*common, ROOT / "tests/voice_ctc_config_test.cpp", lib / "libsherpa-onnx-c-api.dylib",
        probe_rpath, "-o", config_test)
    info = dict(stamp, source=str(source), library=str(lib / "libsherpa-onnx-c-api.dylib"),
                library_sha256=sha256(lib / "libsherpa-onnx-c-api.dylib"),
                ort_include=str(ort / "include"), ort_library=str(lib / "libonnxruntime.dylib"),
                ort_library_sha256=sha256(lib / "libonnxruntime.dylib"),
                probe=str(probe), probe_sha256=sha256(probe), platform=platform.platform())
    (directory / "build-info.json").write_text(json.dumps(info, indent=2) + "\n")
    print(f"\nExperiment built (JPet unchanged). Probe: {probe}")
    print(f"Source/build provenance: {directory / 'build-info.json'}")


if __name__ == "__main__":
    main()
