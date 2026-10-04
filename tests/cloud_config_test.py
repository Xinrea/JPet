"""Check the client endpoint generated from production Cloudflare configuration."""
import json
from pathlib import Path
import re
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "cmake" / "CloudConfig.cmake"


class CloudConfigTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="jpet-cloud-config-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.config = self.directory / "wrangler.jsonc"
        self.header = self.directory / "JPetCloudConfig.hpp"

    def generate(self, config=None, url=None):
        args = ["cmake", f"-DJPET_CLOUD_HEADER={self.header}"]
        if config is not None:
            self.config.write_text(config, encoding="utf-8")
            args.append(f"-DJPET_CLOUD_CONFIG={self.config}")
        if url is not None:
            args.append(f"-DJPET_CLOUD_URL={url}")
        return subprocess.run(args + ["-P", str(MODULE)], capture_output=True, text=True)

    def test_production_config_compiles_only_the_public_url(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        header = self.header.read_text(encoding="utf-8")
        self.assertIn('ServiceUrl[] = "https://s.jpet.powerlive.io";', header)
        config = (ROOT / "cloud" / "wrangler.jsonc").read_text(encoding="utf-8")
        for resource_id in re.findall(r'"(?:account_id|database_id)"\s*:\s*"([^"]+)"', config):
            self.assertNotIn(resource_id, header)

    def test_jsonc_selects_the_enabled_top_level_custom_domain(self):
        result = self.generate('''{
          // Ordinary Worker routes and disabled domains do not select the client URL.
          "routes": [
            {"pattern": "worker.example.com/*"},
            {"pattern": "disabled.example.com", "custom_domain": true, "enabled": false},
            {"pattern": "game.example.com", "custom_domain": true},
          ],
          /* Development environments do not override production. */
          "env": {"dev": {"routes": [{"pattern": "dev.example.com", "custom_domain": true}]}},
        }''')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('"https://game.example.com"', self.header.read_text(encoding="utf-8"))

    def test_development_url_is_selected_at_build_time(self):
        for url in ("http://127.0.0.1:8787", "http://localhost:8787", "https://game.example.com/api"):
            with self.subTest(url=url):
                result = self.generate(config='{"routes": []}', url=url)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(f'"{url}"', self.header.read_text(encoding="utf-8"))

    def test_rejects_unsafe_or_invalid_build_urls(self):
        for url in ("http://game.example.com", "https://user:secret@example.com",
                    'https://example.com/"', "https://example.com/?secret=value", ""):
            with self.subTest(url=url):
                result = self.generate(config='{"routes": []}', url=url)
                self.assertNotEqual(result.returncode, 0)

    def test_rejects_missing_ambiguous_or_invalid_domains(self):
        for routes in ([], [{"pattern": "game.example.com/*"}],
                       [{"pattern": "one.example.com", "custom_domain": True},
                        {"pattern": "two.example.com", "custom_domain": True}],
                       [{"pattern": "https://game.example.com", "custom_domain": True}]):
            with self.subTest(routes=routes):
                result = self.generate(json.dumps({"routes": routes}))
                self.assertNotEqual(result.returncode, 0)

    def test_build_regenerates_the_url_after_config_changes(self):
        source = self.directory / "source"
        source.mkdir()
        build = self.directory / "build"
        config = lambda domain: json.dumps({"routes": [{"pattern": domain, "custom_domain": True}]})
        self.config.write_text(config("first.example.com"), encoding="utf-8")
        (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.27)
project(CloudConfigTest NONE)
include("{MODULE.as_posix()}")
jpet_configure_cloud("${{CMAKE_CURRENT_BINARY_DIR}}/JPetCloudConfig.hpp")
add_custom_target(cloud_config ALL DEPENDS "${{CMAKE_CURRENT_BINARY_DIR}}/JPetCloudConfig.hpp")
''', encoding="utf-8")
        configured = subprocess.run(
            ["cmake", "-S", str(source), "-B", str(build), f"-DJPET_CLOUD_CONFIG={self.config}"],
            capture_output=True, text=True)
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        header = build / "JPetCloudConfig.hpp"
        self.assertIn('"https://first.example.com"', header.read_text(encoding="utf-8"))
        # Some build generators compare file modification times at second precision.
        time.sleep(1.1)
        self.config.write_text(config("second.example.com"), encoding="utf-8")
        rebuilt = subprocess.run(["cmake", "--build", str(build), "--config", "Release"],
                                 capture_output=True, text=True)
        self.assertEqual(rebuilt.returncode, 0, rebuilt.stdout + rebuilt.stderr)
        self.assertIn('"https://second.example.com"', header.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
