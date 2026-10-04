#!/usr/bin/env python3
"""Create a self-contained, ad-hoc signed ARM bundle (no developer paths)."""
import pathlib
import subprocess
import sys


def run(*arguments):
    return subprocess.check_output(arguments, text=True)


app = pathlib.Path(sys.argv[1]).resolve()
archive = pathlib.Path(sys.argv[2]).resolve()
executable = app / "Contents/MacOS/JPet"
if run("lipo", "-archs", str(executable)).strip() != "arm64":
    raise SystemExit("The macOS release must contain arm64 only")
# arm64-osx links third-party libraries statically. Catch any new unbundled dylib.
for line in run("otool", "-L", str(executable)).splitlines()[1:]:
    dependency = line.strip().split(" (", 1)[0]
    if not dependency.startswith(("/System/Library/", "/usr/lib/")):
        raise SystemExit(f"Unbundled runtime dependency: {dependency}")
subprocess.check_call(["codesign", "--force", "--deep", "--sign", "-", str(app)])
subprocess.check_call(["codesign", "--verify", "--deep", "--strict", str(app)])
archive.parent.mkdir(parents=True, exist_ok=True)
subprocess.check_call(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(archive)])
print(archive.name)
