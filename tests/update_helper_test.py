#!/usr/bin/env python3
"""Exercise the shipped helper's replacement and rollback in isolated folders."""
import os
import pathlib
import plistlib
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "darwin", "macOS helper")
class MacUpdateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="jpet-update-test-")
        self.root = pathlib.Path(self.temp.name) / "目录 with 'quotes'"
        self.root.mkdir()
        self.target = self.app("installed/JPet.app", "old")
        self.staged = self.app("download/JPet.app", "new")
        self.error = self.root / "last-error.txt"
        self.launches = self.root / "launches.txt"
        # Mock only Launch Services. Copy, signature checks, moves and rollback
        # execute the actual production helper against the real filesystem.
        launcher = self.root / "launch.sh"
        launcher.write_text('#!/bin/bash\n'
                            'echo "$1" >> "$JPET_TEST_LAUNCHES"\n'
                            'if [ "${JPET_TEST_FAIL_LAUNCH:-0}" = 1 ] && '
                            '[ "$(wc -l < "$JPET_TEST_LAUNCHES" | tr -d " ")" = 1 ]; then exit 1; fi\n')
        launcher.chmod(0o700)
        self.script = self.root / "helper.sh"
        self.script.write_text((ROOT / "resources/updater/macos.sh").read_text().replace(
            '/usr/bin/open -n "$target"', '"$JPET_TEST_LAUNCHER" "$target"'))
        self.environment = dict(os.environ, JPET_TEST_LAUNCHER=str(launcher), JPET_TEST_LAUNCHES=str(self.launches))

    def tearDown(self):
        self.temp.cleanup()

    def app(self, relative, version):
        app = self.root / relative
        executable = app / "Contents/MacOS/JPet"
        executable.parent.mkdir(parents=True)
        source = self.root / "main.c"
        source.write_text("int main(void) { return 0; }\n")
        subprocess.run(["clang", str(source), "-o", str(executable)], check=True, capture_output=True)
        with (app / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump({"CFBundleExecutable": "JPet", "CFBundleIdentifier": "cn.vjoi.jpet.test",
                          "CFBundlePackageType": "APPL", "CFBundleVersion": "1.0.0"}, stream)
        (app / "Contents/version.txt").write_text(version)
        subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app)], check=True, capture_output=True)
        return app

    def run_helper(self, parent=2147483647, fail_launch=False):
        env = dict(self.environment, JPET_TEST_FAIL_LAUNCH="1" if fail_launch else "0")
        return subprocess.run(["/bin/bash", str(self.script), str(parent), str(self.staged),
                               str(self.target), str(self.error)], env=env, capture_output=True)

    def test_replace_waits_for_exit_and_preserves_data(self):
        data = self.root / "user-data/save.dat"
        data.parent.mkdir(); data.write_text("user save")
        with subprocess.Popen(["sleep", "1"]) as parent:
            reaper = threading.Thread(target=parent.wait)
            reaper.start()
            start = time.monotonic()
            result = self.run_helper(parent.pid)
            reaper.join()
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertGreaterEqual(time.monotonic() - start, .8)
        self.assertEqual((self.target / "Contents/version.txt").read_text(), "new")
        self.assertEqual(data.read_text(), "user save")
        self.assertFalse(self.error.exists())
        self.assertEqual(self.launches.read_text().splitlines(), [str(self.target)])

    def test_launch_failure_restores_old_version(self):
        result = self.run_helper(fail_launch=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.target / "Contents/version.txt").read_text(), "old")
        self.assertTrue(self.error.exists())
        self.assertEqual(len(self.launches.read_text().splitlines()), 2)

    def test_invalid_signature_keeps_old_version(self):
        (self.staged / "Contents/version.txt").write_text("tampered")
        self.assertNotEqual(self.run_helper().returncode, 0)
        self.assertEqual((self.target / "Contents/version.txt").read_text(), "old")


if __name__ == "__main__":
    unittest.main()
