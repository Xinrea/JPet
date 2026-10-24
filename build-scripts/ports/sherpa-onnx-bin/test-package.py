#!/usr/bin/env python3
"""Smoke-test an installed sherpa-onnx-bin package (no model/audio needed).

Run on the matching target machine after vcpkg install:
  python3 build-scripts/ports/sherpa-onnx-bin/test-package.py <installed/triplet>
Requires CMake, a C++ compiler, and on macOS the Xcode command-line tools.
Tests the actual package config/headers and loads only its relocated runtimes.
"""
import argparse
import ctypes
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile


def run(*args):
    return subprocess.check_output([str(arg) for arg in args], text=True)


def probe(directory):
    # This runs in a fresh process: no already-loaded ORT can hide a missing
    # runtime, bad rpath, or accidental dependency on the build machine.
    dll_directory = None
    if sys.platform == "win32":
        dll_directory = __import__("os").add_dll_directory(str(directory))
        library = ctypes.CDLL(str(directory / "sherpa-onnx-c-api.dll"))
    else:
        library = ctypes.CDLL(str(directory / "libsherpa-onnx-c-api.dylib"))
    capability = library.SherpaOnnxJpetSenseVoiceHotwordsVersion
    capability.restype = ctypes.c_int32
    assert capability() == 1, "Missing or incompatible JPet hotword decoder"
    for name, expected in [("SherpaOnnxGetVersionStr", b"1.13.8"),
                           ("SherpaOnnxGetOnnxruntimeVersionStr", b"1.28.2")]:
        function = getattr(library, name)
        function.restype = ctypes.c_char_p
        assert function() == expected, (name, function())
    if dll_directory:
        dll_directory.close()
    print("Relocated runtime: sherpa 1.13.8, ORT 1.28.2, JPet hotwords v1")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--probe", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    if args.probe:
        probe(prefix)
        return
    if platform.system() not in ("Darwin", "Windows"):
        parser.error("Run on macOS arm64 or Windows x64, matching the package")
    work = Path(__file__).resolve().parents[3] / "build"
    work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="jpet-sherpa-package-", dir=work) as temporary:
        root = Path(temporary)
        (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.15)
project(sherpa_package_smoke LANGUAGES CXX)
find_package(unofficial-sherpa-onnx-bin CONFIG REQUIRED)
if(NOT SHERPA_ONNX_BIN_JPET_HOTWORDS_VERSION EQUAL 1)
  message(FATAL_ERROR "Not the JPet hotword package")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/runtimes.txt" "${SHERPA_ONNX_BIN_RUNTIMES}")
add_executable(headers headers.cpp)
target_compile_features(headers PRIVATE cxx_std_17)
target_link_libraries(headers PRIVATE unofficial::sherpa-onnx-bin::headers)
''')
        (root / "headers.cpp").write_text('''#include <sherpa-onnx/c-api/c-api.h>
#include <onnxruntime_c_api.h>
#include <type_traits>
static_assert(std::is_same<decltype(SherpaOnnxJpetSenseVoiceHotwordsVersion()),
                           int32_t>::value, "Patched header missing");
int main() { return sizeof(SherpaOnnxOfflineRecognizerConfig) == 0; }
''')
        print(run("cmake", "-S", root, "-B", root / "build",
                  f"-DCMAKE_PREFIX_PATH={prefix}"))
        print(run("cmake", "--build", root / "build", "--config", "Release"))
        runtimes = [Path(path) for path in
                    (root / "build/runtimes.txt").read_text().split(";")]
        expected = ({"sherpa-onnx-c-api.dll", "onnxruntime.dll",
                     "onnxruntime_providers_shared.dll"} if sys.platform == "win32" else
                    {"libsherpa-onnx-c-api.dylib", "libonnxruntime.dylib"})
        assert {path.name for path in runtimes} == expected, runtimes
        relocated = root / "独立运行时"
        relocated.mkdir()
        for path in runtimes:
            assert path.is_file() and prefix in path.parents, path
            shutil.copy2(path, relocated / path.name)
            if sys.platform == "darwin":
                assert run("lipo", "-archs", path).strip() == "arm64", path
                commands = run("otool", "-l", path)
                assert re.search(r"\bminos 11\.0\b", commands), path
                rpaths = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", commands)
                assert rpaths == ["@loader_path"], (path, rpaths)
        assert (prefix / "share/sherpa-onnx-bin/copyright").is_file()
        print(run(sys.executable, Path(__file__).resolve(), relocated, "--probe"))
    print("Package config, consumer headers, runtime list and relocated loading passed")


if __name__ == "__main__":
    main()
