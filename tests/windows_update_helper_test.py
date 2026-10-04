#!/usr/bin/env python3
"""Run the real PowerShell helper with process launch mocked, in temp folders."""
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "win32", "Windows helper")
class WindowsUpdateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="jpet-update-test-")
        self.root = pathlib.Path(self.temp.name) / "目录 with 'quotes'"
        self.root.mkdir()
        self.target = self.root / "installed/JPet"
        self.stage = self.root / "download/JPet"
        for folder, content in [(self.target, "old"), (self.stage, "new")]:
            folder.mkdir(parents=True)
            (folder / "JPet.exe").write_text(content)
        self.error = self.root / "last-error.txt"
        self.launches = self.root / "launches.txt"
        self.wrapper = self.root / "test.ps1"
        self.wrapper.write_text("""param($Helper, $Staged, $Target, $FailureLog)
function Start-Process {
    param($FilePath, $WorkingDirectory)
    [IO.File]::AppendAllText($env:JPET_TEST_LAUNCHES, "$FilePath`n")
    if ($env:JPET_TEST_FAIL_LAUNCH -eq '1' -and
        [IO.File]::ReadAllLines($env:JPET_TEST_LAUNCHES).Length -eq 1) { throw 'Simulated launch failure' }
}
$global:LASTEXITCODE = 0
& $Helper -ParentId 2147483647 -Staged $Staged -Target $Target -FailureLog $FailureLog
exit $LASTEXITCODE
""", encoding="utf-8-sig")

    def tearDown(self):
        self.temp.cleanup()

    def run_helper(self, failure=False):
        environment = dict(os.environ, JPET_TEST_LAUNCHES=str(self.launches), JPET_TEST_FAIL_LAUNCH="1" if failure else "0")
        return subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
            "-File", str(self.wrapper), "-Helper", str(ROOT / "resources/updater/windows.ps1"),
            "-Staged", str(self.stage), "-Target", str(self.target), "-FailureLog", str(self.error)],
            env=environment, capture_output=True)

    def test_replace_and_preserve_data(self):
        data = self.root / "save.dat"
        data.write_text("user save")
        (self.target / "unins000.exe").write_text("installer metadata")
        (self.target / "unins000.dat").write_text("uninstall log")
        result = self.run_helper()
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        self.assertEqual((self.target / "JPet.exe").read_text(), "new")
        self.assertEqual(data.read_text(), "user save")
        self.assertEqual((self.target / "unins000.exe").read_text(), "installer metadata")
        self.assertEqual((self.target / "unins000.dat").read_text(), "uninstall log")
        self.assertFalse(self.error.exists())

    def test_failed_restart_restores_old_version(self):
        result = self.run_helper(failure=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.target / "JPet.exe").read_text(), "old")
        self.assertTrue(self.error.exists())
        self.assertEqual(len(self.launches.read_text().splitlines()), 2)

    def test_missing_executable_keeps_old_version(self):
        (self.stage / "JPet.exe").unlink()
        self.assertNotEqual(self.run_helper().returncode, 0)
        self.assertEqual((self.target / "JPet.exe").read_text(), "old")


if __name__ == "__main__":
    unittest.main()
